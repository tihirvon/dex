#include "load-save.h"
#include "editor.h"
#include "buffer.h"
#include "block.h"
#include "uchar.h"
#include "lock.h"
#include "wbuf.h"

static void update_stat(int fd, struct buffer *b)
{
	struct stat st;
	fstat(fd, &st);
	b->st_size = st.st_size;
	b->st_dev = st.st_dev;
	b->st_ino = st.st_ino;
	b->_st_mtime = st.st_mtime;
	b->st_uid = st.st_uid;
	b->st_gid = st.st_gid;
	b->st_mode = st.st_mode;
}

static size_t copy_strip_cr(char *dst, const char *src, size_t size)
{
	size_t si = 0;
	size_t di = 0;

	while (si < size) {
		char ch = src[si++];
		if (ch == '\r' && si < size && src[si] == '\n')
			ch = src[si++];
		dst[di++] = ch;
	}
	return di;
}

static size_t add_block(struct buffer *b, const char *buf, size_t size)
{
	const char *start = buf;
	const char *eof = buf + size;
	const char *end;
	struct block *blk;
	unsigned int lines = 0;

	do {
		const char *nl = memchr(start, '\n', eof - start);

		end = nl ? nl + 1 : eof;
		if (end - buf > 8192) {
			if (start == buf) {
				lines += !!nl;
				break;
			}
			end = start;
			break;
		}
		start = end;
		lines += !!nl;
	} while (end < eof);

	size = end - buf;
	blk = block_new(size);
	switch (b->newline) {
	case NEWLINE_UNIX:
		memcpy(blk->data, buf, size);
		blk->size = size;
		break;
	case NEWLINE_DOS:
		blk->size = copy_strip_cr(blk->data, buf, size);
		break;
	}
	blk->nl = lines;
	b->nl += lines;
	list_add_before(&blk->node, &b->blocks);
	return size;
}

static int read_blocks(struct buffer *b, int fd)
{
	size_t pos, size = b->st_size;
	unsigned char *nl, *buf = xmmap(fd, 0, size);

	if (!buf)
		return -1;

	nl = memchr(buf, '\n', size);
	if (nl > buf && nl[-1] == '\r')
		b->newline = NEWLINE_DOS;

	pos = 0;
	while (pos < size)
		pos += add_block(b, buf + pos, size - pos);

	for (pos = 0; pos < size; pos++) {
		if (buf[pos] >= 0x80) {
			unsigned int idx = pos;
			unsigned int u = u_get_nonascii(buf, size, &idx);
			b->options.utf8 = u_is_valid(u);
			break;
		}
	}

	xmunmap(buf, size);
	return 0;
}

int load_buffer(struct buffer *b, int must_exist)
{
	const char *filename = b->abs_filename;
	int fd;

	if (options.lock_files) {
		if (lock_file(filename)) {
			b->ro = 1;
		} else {
			b->locked = 1;
		}
	}

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", filename, strerror(errno));
			return -1;
		}
		if (must_exist) {
			error_msg("File %s does not exist.", filename);
			return -1;
		}
	} else {
		update_stat(fd, b);
		if (!S_ISREG(b->st_mode)) {
			error_msg("Not a regular file %s", filename);
			close(fd);
			return -1;
		}

		if (read_blocks(b, fd)) {
			error_msg("Error reading %s: %s", filename, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);

		if (!b->ro && access(filename, W_OK)) {
			error_msg("No write permission to %s, marking read-only.", filename);
			b->ro = 1;
		}
	}
	if (list_empty(&b->blocks)) {
		struct block *blk = block_new(1);
		list_add_before(&blk->node, &b->blocks);
	} else {
		// Incomplete lines are not allowed because they are
		// special cases and cause lots of trouble.
		struct block *blk = BLOCK(b->blocks.prev);
		if (blk->size && blk->data[blk->size - 1] != '\n') {
			if (blk->size == blk->alloc) {
				blk->alloc = ROUND_UP(blk->size + 1, 64);
				xrenew(blk->data, blk->alloc);
			}
			blk->data[blk->size++] = '\n';
			blk->nl++;
			b->nl++;
		}
	}
	return 0;
}

static int write_crlf(struct wbuf *wbuf, const char *buf, int size)
{
	int written = 0;

	while (size--) {
		char ch = *buf++;
		if (ch == '\n') {
			if (wbuf_write_ch(wbuf, '\r'))
				return -1;
			written++;
		}
		if (wbuf_write_ch(wbuf, ch))
			return -1;
		written++;
	}
	return written;
}

static mode_t get_umask(void)
{
	// Wonderful get-and-set API
	mode_t old = umask(0);
	umask(old);
	return old;
}

static void check_incomplete_last_line(void)
{
	struct block *blk = BLOCK(buffer->blocks.prev);
	if (blk->size && blk->data[blk->size - 1] != '\n')
		info_msg("File saved with incomplete last line.");
}

int save_buffer(const char *filename, enum newline_sequence newline)
{
	/* try to use temporary file first, safer */
	int ren = 1;
	struct block *blk;
	char tmp[PATH_MAX];
	WBUF(wbuf);
	int rc, len;
	unsigned int size;

	if (!strncmp(filename, "/tmp/", 5))
		ren = 0;

	len = strlen(filename);
	if (len + 8 > PATH_MAX)
		ren = 0;
	if (ren) {
		memcpy(tmp, filename, len);
		tmp[len] = '.';
		memset(tmp + len + 1, 'X', 6);
		tmp[len + 7] = 0;
		wbuf.fd = mkstemp(tmp);
		if (wbuf.fd < 0) {
			// No write permission to the directory?
			ren = 0;
		} else if (buffer->st_mode) {
			// Preserve ownership and mode of the original file if possible.
			int ignore = fchown(wbuf.fd, buffer->st_uid, buffer->st_gid);
			ignore = ignore; // warning: unused variable 'ignore'
			fchmod(wbuf.fd, buffer->st_mode);
		} else {
			// new file
			fchmod(wbuf.fd, 0666 & ~get_umask());
		}
	}
	if (!ren) {
		// Overwrite the original file (if exists) directly.
		// Ownership is preserved automatically if the file exists.
		mode_t mode = buffer->st_mode;
		if (mode == 0) {
			// New file.
			mode = 0666 & ~get_umask();
		}
		wbuf.fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, mode);
		if (wbuf.fd < 0) {
			error_msg("Error opening file: %s", strerror(errno));
			return -1;
		}
	}

	rc = 0;
	size = 0;
	if (newline == NEWLINE_DOS) {
		list_for_each_entry(blk, &buffer->blocks, node) {
			rc = write_crlf(&wbuf, blk->data, blk->size);
			if (rc < 0)
				break;
			size += rc;
		}
	} else {
		list_for_each_entry(blk, &buffer->blocks, node) {
			rc = wbuf_write(&wbuf, blk->data, blk->size);
			if (rc)
				break;
			size += blk->size;
		}
	}
	if (rc < 0 || wbuf_flush(&wbuf)) {
		error_msg("Write error: %s", strerror(errno));
		if (ren)
			unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	if (!ren && ftruncate(wbuf.fd, size)) {
		error_msg("Truncate failed: %s", strerror(errno));
		close(wbuf.fd);
		return -1;
	}
	if (ren && rename(tmp, filename)) {
		error_msg("Rename failed: %s", strerror(errno));
		unlink(tmp);
		close(wbuf.fd);
		return -1;
	}
	update_stat(wbuf.fd, buffer);
	close(wbuf.fd);

	buffer->save_change_head = buffer->cur_change_head;
	buffer->ro = 0;
	buffer->newline = newline;

	check_incomplete_last_line();
	return 0;
}
