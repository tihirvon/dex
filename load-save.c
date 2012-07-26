#include "load-save.h"
#include "editor.h"
#include "buffer.h"
#include "block.h"
#include "lock.h"
#include "wbuf.h"
#include "decoder.h"
#include "encoder.h"
#include "encoding.h"
#include "error.h"

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

static void add_block(struct buffer *b, struct block *blk)
{
	b->nl += blk->nl;
	list_add_before(&blk->node, &b->blocks);
}

static struct block *add_utf8_line(struct buffer *b, struct block *blk, const unsigned char *line, size_t len)
{
	size_t size = len + 1;

	if (blk) {
		size_t avail = blk->alloc - blk->size;
		if (size <= avail)
			goto copy;

		add_block(b, blk);
	}

	if (size < 8192)
		size = 8192;
	blk = block_new(size);
copy:
	memcpy(blk->data + blk->size, line, len);
	blk->size += len;
	blk->data[blk->size++] = '\n';
	blk->nl++;
	return blk;
}

static int decode_and_add_blocks(struct buffer *b, const unsigned char *buf, size_t size)
{
	const char *e = detect_encoding_from_bom(buf, size);
	struct file_decoder *dec;
	char *line;
	ssize_t len;

	if (b->encoding == NULL) {
		if (e) {
			// UTF-16BE/LE or UTF-32BE/LE
			b->encoding = xstrdup(e);
		}
	} else if (!strcmp(b->encoding, "UTF-16")) {
		// BE or LE?
		if (e && str_has_prefix(e, b->encoding)) {
			free(b->encoding);
			b->encoding = xstrdup(e);
		} else {
			// "open -e UTF-16" but incompatible or no BOM.
			// Do what the user wants. Big-endian is default.
			free(b->encoding);
			b->encoding = xstrdup("UTF-16BE");
		}
	} else if (!strcmp(b->encoding, "UTF-32")) {
		// BE or LE?
		if (e && str_has_prefix(e, b->encoding)) {
			free(b->encoding);
			b->encoding = xstrdup(e);
		} else {
			// "open -e UTF-32" but incompatible or no BOM.
			// Do what the user wants. Big-endian is default.
			free(b->encoding);
			b->encoding = xstrdup("UTF-32BE");
		}
	}

	// Skip BOM only if it matches the specified file encoding.
	if (b->encoding && e && !strcmp(b->encoding, e)) {
		size_t bom_len = 2;
		if (str_has_prefix(e, "UTF-32"))
			bom_len = 4;
		buf += bom_len;
		size -= bom_len;
	}

	dec = new_file_decoder(b->encoding, buf, size);
	if (dec == NULL)
		return -1;

	if (file_decoder_read_line(dec, &line, &len)) {
		struct block *blk = NULL;

		if (len && line[len - 1] == '\r') {
			b->newline = NEWLINE_DOS;
			len--;
		}
		blk = add_utf8_line(b, blk, line, len);

		while (file_decoder_read_line(dec, &line, &len)) {
			if (b->newline == NEWLINE_DOS && len && line[len - 1] == '\r')
				len--;
			blk = add_utf8_line(b, blk, line, len);
		}
		if (blk)
			add_block(b, blk);
	}
	if (b->encoding == NULL) {
		e = dec->encoding;
		if (e == NULL)
			e = charset;
		b->encoding = xstrdup(e);
	}
	free_file_decoder(dec);
	return 0;
}

static int read_blocks(struct buffer *b, int fd)
{
	size_t size = b->st_size;
	unsigned long map_size = 64 * 1024;
	unsigned char *buf = NULL;
	int mapped = 0;
	ssize_t rc;

	// st_size is zero for some files in /proc.
	// Can't mmap files in /proc and /sys.
	if (size >= map_size) {
		buf = xmmap(fd, 0, size);
		if (buf)
			mapped = 1;
	}
	if (!mapped) {
		ssize_t alloc = map_size;
		ssize_t pos = 0;

		buf = xnew(char, alloc);
		while (1) {
			rc = xread(fd, buf + pos, alloc - pos);
			if (rc < 0) {
				free(buf);
				return -1;
			}
			if (rc == 0)
				break;
			pos += rc;
			if (pos == alloc) {
				alloc *= 2;
				xrenew(buf, alloc);
			}
		}
		size = pos;
	}
	rc = decode_and_add_blocks(b, buf, size);
	if (mapped) {
		xmunmap(buf, size);
	} else {
		free(buf);
	}
	return rc;
}

int load_buffer(struct buffer *b, int must_exist, const char *filename)
{
	int fd;

	if (options.lock_files) {
		if (lock_file(b->abs_filename)) {
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

	if (b->encoding == NULL)
		b->encoding = xstrdup(charset);
	return 0;
}

static mode_t get_umask(void)
{
	// Wonderful get-and-set API
	mode_t old = umask(0);
	umask(old);
	return old;
}

int save_buffer(const char *filename, const char *encoding, enum newline_sequence newline)
{
	/* try to use temporary file first, safer */
	int ren = 1;
	struct block *blk;
	char tmp[PATH_MAX];
	int fd, len;
	unsigned int size;
	struct file_encoder *enc;
	const struct byte_order_mark *bom;

	if (str_has_prefix(filename, "/tmp/"))
		ren = 0;

	len = strlen(filename);
	if (len + 8 > sizeof(tmp))
		ren = 0;
	if (ren) {
		memcpy(tmp, filename, len);
		tmp[len] = '.';
		memset(tmp + len + 1, 'X', 6);
		tmp[len + 7] = 0;
		fd = mkstemp(tmp);
		if (fd < 0) {
			// No write permission to the directory?
			ren = 0;
		} else if (buffer->st_mode) {
			// Preserve ownership and mode of the original file if possible.

			// "ignoring return value of 'fchown', declared with attribute warn_unused_result"
			//
			// Casting to void does not hide this warning when
			// using GCC and clang does not like this:
			//     int ignore = fchown(...); ignore = ignore;
			if (fchown(fd, buffer->st_uid, buffer->st_gid)) {
			}
			fchmod(fd, buffer->st_mode);
		} else {
			// new file
			fchmod(fd, 0666 & ~get_umask());
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
		fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, mode);
		if (fd < 0) {
			error_msg("Error opening file: %s", strerror(errno));
			return -1;
		}
	}

	enc = new_file_encoder(encoding, newline, fd);
	if (enc == NULL) {
		// this should never happen because encoding is validated early
		error_msg("iconv_open: %s", strerror(errno));
		close(fd);
		return -1;
	}
	size = 0;

	bom = get_bom_for_encoding(encoding);
	if (bom) {
		size = bom->len;
		if (xwrite(fd, bom->bytes, size) < 0)
			goto write_error;
	}

	list_for_each_entry(blk, &buffer->blocks, node) {
		ssize_t rc = file_encoder_write(enc, blk->data, blk->size);

		if (rc < 0)
			goto write_error;
		size += rc;
	}
	if (enc->errors) {
		// any real error hides this message
		error_msg("Warning: %d nonreversible character conversions. File saved.", enc->errors);
	}
	free_file_encoder(enc);
	if (!ren && ftruncate(fd, size)) {
		error_msg("Truncate failed: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (ren && rename(tmp, filename)) {
		error_msg("Rename failed: %s", strerror(errno));
		unlink(tmp);
		close(fd);
		return -1;
	}
	update_stat(fd, buffer);
	close(fd);
	return 0;
write_error:
	error_msg("Write error: %s", strerror(errno));
	free_file_encoder(enc);
	if (ren)
		unlink(tmp);
	close(fd);
	return -1;
}
