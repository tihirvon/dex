#include "window.h"
#include "util.h"
#include "term.h"
#include "filetype.h"
#include "highlight.h"
#include "commands.h"

struct view *view;
struct buffer *buffer;
struct view *prev_view;
enum undo_merge undo_merge;

/*
 * Temporary buffer for searching and editing.
 */
char *line_buffer;
size_t line_buffer_len;
static size_t line_buffer_alloc;

/*
 * Contains one line including LF.
 * Used by syntax highlighter only.
 */
char *hl_buffer;
size_t hl_buffer_len;
static size_t hl_buffer_alloc;

void init_selection(struct selection_info *info)
{
	uchar u;

	info->si = view->cursor;
	info->ei = view->sel;
	info->so = block_iter_get_offset(&info->si);
	info->eo = block_iter_get_offset(&info->ei);
	info->swapped = 0;
	info->nr_lines = 1;
	info->nr_chars = 0;
	if (info->so > info->eo) {
		struct block_iter bi = info->si;
		unsigned int o = info->so;
		info->si = info->ei;
		info->ei = bi;
		info->so = info->eo;
		info->eo = o;
		info->swapped = 1;
	}
	if (block_iter_eof(&info->ei)) {
		if (info->so == info->eo)
			return;
		info->eo -= buffer->prev_char(&info->ei, &u);
	}
	if (view->sel_is_lines) {
		info->so -= block_iter_bol(&info->si);
		info->eo += count_bytes_eol(&info->ei);
	} else {
		// character under cursor belongs to the selection
		info->eo += buffer->next_char(&info->ei, &u);
	}
}

void fill_selection_info(struct selection_info *info)
{
	struct block_iter bi = info->si;
	int nr_bytes = info->eo - info->so;
	uchar u, prev_char = 0;

	while (nr_bytes && buffer->next_char(&bi, &u)) {
		if (prev_char == '\n')
			info->nr_lines++;
		info->nr_chars++;
		prev_char = u;
		if (buffer->utf8)
			nr_bytes -= u_char_size(u);
		else
			nr_bytes--;
	}
}

char *buffer_get_bytes(unsigned int *lenp)
{
	struct block *blk = view->cursor.blk;
	unsigned int offset = view->cursor.offset;
	unsigned int len = *lenp;
	unsigned int count = 0;
	char *buf = NULL;
	unsigned int alloc = 0;

	while (count < len) {
		unsigned int avail = blk->size - offset;
		if (avail > 0) {
			unsigned int c = len - count;

			if (c > avail)
				c = avail;
			alloc += c;
			xrenew(buf, alloc);
			memcpy(buf + count, blk->data + offset, c);
			count += c;
		}
		if (blk->node.next == &buffer->blocks)
			break;
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
	*lenp = count;
	return buf;
}

static void line_buffer_add(size_t pos, const char *src, size_t count)
{
	size_t size = pos + count + 1;

	if (line_buffer_alloc < size) {
		line_buffer_alloc = ALLOC_ROUND(size);
		xrenew(line_buffer, line_buffer_alloc);
	}
	memcpy(line_buffer + pos, src, count);
}

void fetch_eol(const struct block_iter *bi)
{
	struct block *blk = bi->blk;
	unsigned int offset = bi->offset;
	size_t pos = 0;

	while (1) {
		unsigned int avail = blk->size - offset;
		char *src = blk->data + offset;
		char *ptr = memchr(src, '\n', avail);

		if (ptr) {
			line_buffer_add(pos, src, ptr - src);
			pos += ptr - src;
			break;
		}
		line_buffer_add(pos, src, avail);
		pos += avail;

		if (blk->node.next == bi->head)
			break;
		blk = BLOCK(blk->node.next);
		offset = 0;
	}
	line_buffer_add(pos, "", 1);
	line_buffer_len = pos;
}

static void hl_buffer_add(size_t pos, const char *src, size_t count)
{
	size_t size = pos + count + 1;

	if (hl_buffer_alloc < size) {
		hl_buffer_alloc = ALLOC_ROUND(size);
		xrenew(hl_buffer, hl_buffer_alloc);
	}
	memcpy(hl_buffer + pos, src, count);
}

/*
 * Only available for highlighter and screen updates.
 * Never use while editing the buffer.  Use fetch_eol() when doing changes.
 */
void fetch_line(struct block_iter *bi)
{
	size_t pos = 0;

	while (1) {
		unsigned int avail = bi->blk->size - bi->offset;
		char *src = bi->blk->data + bi->offset;
		char *ptr = memchr(src, '\n', avail);

		if (ptr) {
			unsigned int count = ptr - src + 1;
			hl_buffer_add(pos, src, count);
			pos += count;
			bi->offset += count;
			break;
		}
		hl_buffer_add(pos, src, avail);
		pos += avail;
		bi->offset += avail;

		if (bi->blk->node.next == bi->head)
			break;
		bi->blk = BLOCK(bi->blk->node.next);
		bi->offset = 0;
	}
	hl_buffer_add(pos, "", 1);
	hl_buffer_len = pos;
}

unsigned int buffer_offset(void)
{
	return block_iter_get_offset(&view->cursor);
}

void move_offset(unsigned int offset)
{
	block_iter_goto_offset(&view->cursor, offset);
}

static struct buffer *buffer_new(void)
{
	struct buffer *b;

	b = xnew0(struct buffer, 1);
	list_init(&b->blocks);
	b->cur_change_head = &b->change_head;
	b->save_change_head = &b->change_head;
	b->utf8 = !!(term_flags & TERM_UTF8);

	b->options.auto_indent = options.auto_indent;
	b->options.expand_tab = options.expand_tab;
	b->options.indent_width = options.indent_width;
	b->options.tab_width = options.tab_width;
	b->options.text_width = options.text_width;
	b->options.trim_whitespace = options.trim_whitespace;
	b->options.ws_error = options.ws_error;
	b->options.filetype = xstrdup("none");

	b->newline = options.newline;
	list_init(&b->hl_head);
	return b;
}

static void buffer_set_callbacks(struct buffer *b)
{
	if (b->utf8) {
		b->next_char = block_iter_next_uchar;
		b->prev_char = block_iter_prev_uchar;
	} else {
		b->next_char = block_iter_next_byte;
		b->prev_char = block_iter_prev_byte;
	}
}

struct view *open_empty_buffer(void)
{
	struct buffer *b = buffer_new();
	struct block *blk;
	struct view *v;

	// at least one block required
	blk = block_new(ALLOC_ROUND(1));
	list_add_before(&blk->node, &b->blocks);

	buffer_set_callbacks(b);
	v = window_add_buffer(b);
	v->cursor.head = &v->buffer->blocks;
	v->cursor.blk = BLOCK(v->buffer->blocks.next);
	return v;
}

static void read_crlf_blocks(struct buffer *b, const char *buf)
{
	size_t size = b->st.st_size;
	size_t pos = 0;

	while (pos < size) {
		size_t count = size - pos;
		struct block *blk;
		int s, d;

		if (count > BLOCK_MAX_SIZE)
			count = BLOCK_MAX_SIZE;

		blk = block_new(count);
		d = 0;
		for (s = 0; s < count; s++) {
			char ch = buf[pos + s];
			if (ch != '\r')
				blk->data[d++] = ch;
			if (ch == '\n')
				blk->nl++;
		}
		blk->size = d;
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
		pos += count;
	}
}

static void read_lf_blocks(struct buffer *b, const char *buf)
{
	size_t size = b->st.st_size;
	size_t pos = 0;

	while (pos < size) {
		size_t count = size - pos;
		struct block *blk;

		if (count > BLOCK_MAX_SIZE)
			count = BLOCK_MAX_SIZE;

		blk = block_new(count);
		blk->size = count;
		blk->nl = copy_count_nl(blk->data, buf + pos, blk->size);
		b->nl += blk->nl;
		list_add_before(&blk->node, &b->blocks);
		pos += count;
	}
}

static int read_blocks(struct buffer *b, int fd)
{
	size_t pos, size = b->st.st_size;
	char *nl, *buf = xmmap(fd, 0, size);

	if (!buf)
		return -1;

	nl = memchr(buf, '\n', size);
	if (nl > buf && nl[-1] == '\r') {
		b->newline = NEWLINE_DOS;
		read_crlf_blocks(b, buf);
	} else {
		read_lf_blocks(b, buf);
	}

	for (pos = 0; pos < size; pos++) {
		if ((unsigned char)buf[pos] >= 0x80) {
			char str[5];
			int len = 4, idx = 0;
			uchar u;

			if (len > size - pos)
				len = size - pos;
			memcpy(str, buf + pos, len);
			str[len] = 0;
			u = u_get_char(buf, &idx);
			b->utf8 = !(u & U_INVALID_MASK);
			break;
		}
	}

	xmunmap(buf, b->st.st_size);
	return 0;
}

static void free_changes(struct buffer *b)
{
	struct change_head *ch = &b->change_head;

top:
	while (ch->nr_prev)
		ch = ch->prev[ch->nr_prev - 1];

	// ch is leaf now
	while (ch->next) {
		struct change_head *next = ch->next;

		free(((struct change *)ch)->buf);
		free(ch);

		ch = next;
		if (--ch->nr_prev)
			goto top;

		// we have become leaf
		free(ch->prev);
	}
}

void free_buffer(struct buffer *b)
{
	struct list_head *item;

	item = b->blocks.next;
	while (item != &b->blocks) {
		struct list_head *next = item->next;
		struct block *blk = BLOCK(item);

		free(blk->data);
		free(blk);
		item = next;
	}
	free_changes(b);
	free_hl_list(&b->hl_head);

	free(b->filename);
	free(b->abs_filename);
	free_local_options(&b->options);
	free(b);
}

static struct view *find_view(const char *abs_filename)
{
	struct window *w;
	struct view *v;

	// open in current window?
	list_for_each_entry(v, &window->views, node) {
		const char *f = v->buffer->abs_filename;
		if (f && !strcmp(f, abs_filename))
			return v;
	}

	// open in any other window?
	list_for_each_entry(w, &windows, node) {
		if (w == window)
			continue;
		list_for_each_entry(v, &w->views, node) {
			const char *f = v->buffer->abs_filename;
			if (f && !strcmp(f, abs_filename))
				return v;
		}
	}
	return NULL;
}

void guess_filetype(struct buffer *b)
{
	const char *ft = NULL;

	if (BLOCK(b->blocks.next)->size) {
		struct block_iter bi;

		bi.blk = BLOCK(b->blocks.next);
		bi.head = &b->blocks;
		bi.offset = 0;
		fetch_eol(&bi);
		ft = find_ft(b->abs_filename, line_buffer);
	} else if (b->abs_filename) {
		ft = find_ft(b->abs_filename, NULL);
	}
	if (ft) {
		free(b->options.filetype);
		b->options.filetype = xstrdup(ft);
	}
}

static struct syntax *load_syntax(const char *filetype)
{
	char filename[1024];
	struct syntax *syn;

	snprintf(filename, sizeof(filename), "%s/.editor/syntax/%s", home_dir, filetype);
	if (read_config(filename)) {
		snprintf(filename, sizeof(filename), "%s/editor/syntax/%s", DATADIR, filetype);
		if (read_config(filename))
			return NULL;
	}
	syn = find_syntax(filetype);
	if (syn)
		update_syntax_colors();
	return syn;
}

struct view *open_buffer(const char *filename, unsigned int flags)
{
	struct buffer *b;
	struct view *v;
	char *absolute;

	absolute = path_absolute(filename);
	if (!absolute) {
		error_msg("Failed to make absolute path: %s", strerror(errno));
		return NULL;
	}

	// already open?
	v = find_view(absolute);
	if (v) {
		free(absolute);
		if (v->window != window) {
			// open the buffer in other window to current window
			struct view *new = window_add_buffer(v->buffer);
			new->cursor = v->cursor;
			return new;
		}
		// the file was already open in current window
		return v;
	}

	b = buffer_new();
	b->filename = xstrdup(filename);
	b->abs_filename = absolute;

	if (flags & OF_LOAD_BUFFER && load_buffer(b, flags & OF_FILE_MUST_EXIST)) {
		free_buffer(b);
		return NULL;
	}
	v = window_add_buffer(b);
	if (flags & OF_LOAD_BUFFER) {
		v->cursor.head = &v->buffer->blocks;
		v->cursor.blk = BLOCK(v->buffer->blocks.next);
	}
	return v;
}

int load_buffer(struct buffer *b, int must_exist)
{
	int fd = open(b->filename, O_RDONLY);

	if (fd < 0) {
		if (errno != ENOENT) {
			error_msg("Error opening %s: %s", b->filename, strerror(errno));
			return -1;
		}
		if (must_exist) {
			error_msg("File %s does not exist.", b->filename);
			return -1;
		}
	} else {
		fstat(fd, &b->st);
		if (!S_ISREG(b->st.st_mode)) {
			error_msg("Can't open %s %s", get_file_type(b->st.st_mode), b->filename);
			close(fd);
			return -1;
		}

		if (read_blocks(b, fd)) {
			error_msg("Error reading %s: %s", b->filename, strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);

		if (access(b->abs_filename, W_OK))
			b->ro = 1;
	}
	if (list_empty(&b->blocks)) {
		struct block *blk = block_new(ALLOC_ROUND(1));
		list_add_before(&blk->node, &b->blocks);
	}
	guess_filetype(b);
	filetype_changed(b);
	buffer_set_callbacks(b);
	return 0;
}

void filetype_changed(struct buffer *b)
{
	free_hl_list(&b->hl_head);

	/* even "none" can have syntax */
	b->syn = find_syntax(b->options.filetype);
	if (!b->syn)
		b->syn = load_syntax(b->options.filetype);
	highlight_buffer(b);
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
		info_msg("Incomplete last line");
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
	mode_t mode = buffer->st.st_mode ? buffer->st.st_mode : 0666 & ~get_umask();

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
			ren = 0;
		} else {
			fchmod(wbuf.fd, mode);
		}
	}
	if (!ren) {
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
	fstat(wbuf.fd, &buffer->st);
	close(wbuf.fd);

	buffer->save_change_head = buffer->cur_change_head;
	buffer->ro = 0;
	buffer->newline = newline;

	check_incomplete_last_line();
	return 0;
}

static void init_highlighter(struct highlighter *h, struct buffer *b)
{
	memset(h, 0, sizeof(*h));
	h->headp = &b->hl_head;
	h->syn = b->syn;
}

static void init_highlighter_heredoc(struct highlighter *h)
{
	const struct syntax_context *c;
	struct block_iter bi;
	unsigned int offset;
	int eflags = 0;
	regmatch_t m[2];

	if (!h->stack.level) {
		BUG_ON(h->stack.heredoc_offset >= 0);
		return;
	}
	c = h->stack.contexts[h->stack.level];
	if (!(c->any.flags & SYNTAX_FLAG_HEREDOC)) {
		BUG_ON(h->stack.heredoc_offset >= 0);
		return;
	}

	BUG_ON(h->stack.heredoc_offset < 0);

	bi.head = &buffer->blocks;
	bi.blk = BLOCK(buffer->blocks.next);
	bi.offset = 0;
	block_iter_goto_offset(&bi, h->stack.heredoc_offset);
	offset = block_iter_bol(&bi);
	fetch_line(&bi);

	if (offset > 0)
		eflags |= REG_NOTBOL;
	if (regexec(&c->sregex, hl_buffer + offset, 2, m, eflags)) {
		return;
	}
	if (m[1].rm_so >= 0) {
		const char *str = hl_buffer + m[1].rm_so + offset;
		int str_len = m[1].rm_eo - m[1].rm_so;

		build_heredoc_eregex(h, c, str, str_len);
	}
}

static int verify_count;
static int verify_counter;

static void verify_hl_list(struct list_head *head, const char *suffix)
{
#if DEBUG_SYNTAX
	struct hl_list *list;
	int i;
#if DEBUG_SYNTAX > 1
	char buf[128];
	FILE *f;

	snprintf(buf, sizeof(buf), "/tmp/verify-%d-%d-%s", verify_count, verify_counter++, suffix);
	f = fopen(buf, "w");
	list_for_each_entry(list, head, node) {
		for (i = 0; i < list->count; i++) {
			static const char *names[] = { " ", "{", "}" };
			struct hl_entry *e = &list->entries[i];
			union syntax_node *n = idx_to_syntax_node(hl_entry_idx(e));
			fprintf(f, "%3d %s %s\n", hl_entry_len(e), names[hl_entry_type(e) >> 6], n->any.name);
		}
	}
	fclose(f);
#endif

	list_for_each_entry(list, head, node) {
		BUG_ON(!list->count);
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			BUG_ON(!hl_entry_len(e));
		}
	}
#endif
}

static void full_debug(void)
{
#if DEBUG_SYNTAX > 1
	struct hl_list *list;
	unsigned int pos = 0;
	static int counter;
	char buf[128];
	FILE *f;
	int i;
	struct block_iter save = view->cursor;

	view->cursor.blk = BLOCK(buffer->blocks.next);
	view->cursor.offset = 0;

	snprintf(buf, sizeof(buf), "/tmp/hl-%d", counter++);
	f = fopen(buf, "w");
	list_for_each_entry(list, &buffer->hl_head, node) {
		BUG_ON(!list->count);
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			unsigned int len = hl_entry_len(e);
			char *bytes = buffer_get_bytes(&len);
			union syntax_node *n = idx_to_syntax_node(hl_entry_idx(e));
			xrenew(bytes, len + 1);
			bytes[len] = 0;
			switch (hl_entry_type(e)) {
			case HL_ENTRY_NORMAL:
				fprintf(f, "[%s]%s", n->any.name, bytes);
				break;
			case HL_ENTRY_SOC:
				fprintf(f, "<%s>%s", n->any.name, bytes);
				break;
			case HL_ENTRY_EOC:
				fprintf(f, "</%s>%s", n->any.name, bytes);
				break;
			}
			free(bytes);
			pos += hl_entry_len(e);
			move_offset(pos);
		}
	}
	fclose(f);
	view->cursor = save;
#endif
}

static void verify_hl_size(void)
{
#if DEBUG_SYNTAX
	struct hl_list *list;
	struct block *blk;
	unsigned int hl_size = 0;
	unsigned int size = 0;
	int i;

	list_for_each_entry(list, &buffer->hl_head, node) {
		BUG_ON(!list->count);
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			hl_size += hl_entry_len(e);
		}
	}

	list_for_each_entry(blk, &buffer->blocks, node)
		size += blk->size;

	ds_print("%d %d\n", hl_size, size);
	if (size) {
		blk = BLOCK(buffer->blocks.prev);
		if (blk->data[blk->size - 1] != '\n')
			size++;
	}
	BUG_ON(hl_size != size);
#endif
}

void highlight_buffer(struct buffer *b)
{
	struct block_iter bi;
	struct highlighter h;

	if (!b->syn)
		return;

	bi.head = &b->blocks;
	bi.blk = BLOCK(b->blocks.next);
	bi.offset = 0;

	init_highlighter(&h, b);
	init_syntax_context_stack(&h.stack, syntax_get_default_context(b->syn));
	while (!block_iter_eof(&bi)) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
}

/*
 * stop_a and stop_b must be offset at beginning of a line. really?
 *
 * positions stop_a/b are included in the context stack
 */
static void update_contexts(const struct syntax *syn, struct list_head *head,
		unsigned int stop_a, struct syntax_context_stack *a,
		unsigned int stop_b, struct syntax_context_stack *b)
{
	unsigned int pos = 0;
	unsigned int stop = stop_a;
	struct syntax_context_stack *ptr = a;
	struct hl_list *list;

	init_syntax_context_stack(ptr, syntax_get_default_context(syn));
	list_for_each_entry(list, head, node) {
		int i;
		for (i = 0; i < list->count; i++) {
			struct hl_entry *e = &list->entries[i];
			unsigned int new_pos = pos + hl_entry_len(e);
			unsigned int type = hl_entry_type(e);

			if (type == HL_ENTRY_EOC) {
				const struct syntax_context *c = ptr->contexts[ptr->level];
				if (c->any.flags & SYNTAX_FLAG_HEREDOC)
					ptr->heredoc_offset = -1;
				pop_syntax_context(ptr);
				ds_print("%3d back %s\n", pos, ptr->contexts[ptr->level]->any.name);
			}
			if (type == HL_ENTRY_SOC) {
				const struct syntax_context *c;
				c = &idx_to_syntax_node(hl_entry_idx(e))->context;
				if (c->any.flags & SYNTAX_FLAG_HEREDOC)
					ptr->heredoc_offset = pos;
				push_syntax_context(ptr, c);
				ds_print("%3d new %s\n", pos, c->any.name);
			}
			while (new_pos > stop) {
				if (!b)
					return;

				copy_syntax_context_stack(b, a);
				stop = stop_b;
				ptr = b;
				b = NULL;
			}
			pos = new_pos;
		}
	}
	BUG("unreachable\n");
}

static void update_hl_eof(void)
{
	struct block_iter bi = view->cursor;
	struct syntax_context_stack a;
	struct highlighter h;
	unsigned int offset;

	block_iter_bol(&bi);
	offset = block_iter_get_offset(&bi);
	if (offset) {
		offset--;
		update_contexts(buffer->syn, &buffer->hl_head, offset, &a, 0, NULL);
		truncate_hl_list(&buffer->hl_head, offset + 1);
	} else {
		init_syntax_context_stack(&a, syntax_get_default_context(buffer->syn));
		truncate_hl_list(&buffer->hl_head, offset);
	}
	verify_hl_list(&buffer->hl_head, "truncate");

	init_highlighter(&h, buffer);
	copy_syntax_context_stack(&h.stack, &a);
	init_highlighter_heredoc(&h);

	/* highlight to eof */
	while (!block_iter_eof(&bi)) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}
	if (h.heredoc_context)
		regfree(&h.heredoc_eregex);
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
	free(a.contexts);

	verify_hl_list(&buffer->hl_head, "final");
}

static int hl_context_stacks_equal(
	const struct syntax_context_stack *a,
	const struct syntax_context_stack *b)
{
	if (a->level != b->level)
		return 0;
	if (a->heredoc_offset >= 0 || b->heredoc_offset >= 0)
		return 0;
	return !memcmp(a->contexts, b->contexts, sizeof(a->contexts[0]) * (a->level + 1));
}

/*
 * NOTE: This is called after delete too.
 *
 * Delete:
 *     ins_nl is 0
 *     ins_count is negative
 */
void update_hl_insert(unsigned int ins_nl, int ins_count)
{
	LIST_HEAD(new_list);
	unsigned int to_prev, to_eol;
	unsigned int offset_a;
	unsigned int offset_b;
	struct syntax_context_stack a; /* context stack before first modified line */
	struct syntax_context_stack b; /* context stack after last modified line */
	struct block_iter bi;
	struct highlighter h;
	int i, top;

	if (!buffer->syn)
		return;

	verify_hl_list(&buffer->hl_head, "unmodified");

	bi = view->cursor;
	to_eol = 0;
	for (i = 0; i < ins_nl; i++)
		to_eol += block_iter_next_line(&bi);
	to_eol += block_iter_eol(&bi);

	if (block_iter_eof(&bi)) {
		/* last line was modified */
		update_hl_eof();
		verify_hl_size();
		return;
	}

	bi = view->cursor;
	to_prev = block_iter_bol(&bi);
	top = 1;
	offset_a = block_iter_get_offset(&bi);
	if (offset_a) {
		to_prev++;
		top = 0;
		offset_a--;
	}
	offset_b = offset_a + to_prev - ins_count + to_eol;

	if (top) {
		update_contexts(buffer->syn, &buffer->hl_head, offset_b, &b, 0, NULL);
		init_syntax_context_stack(&a, b.contexts[0]);
	} else {
		update_contexts(buffer->syn, &buffer->hl_head, offset_a, &a, offset_b, &b);
	}

	init_highlighter(&h, buffer);
	h.headp = &new_list;
	copy_syntax_context_stack(&h.stack, &a);
	init_highlighter_heredoc(&h);

	/* highlight the modified lines */
	for (i = 0; i <= ins_nl; i++) {
		fetch_line(&bi);
		h.line = hl_buffer;
		h.line_len = hl_buffer_len;
		h.offset = 0;
		highlight_line(&h);
	}

	ds_print("a=%d b=%d\n", offset_a, offset_b);

	for (i = 0; i <= a.level; i++)
		ds_print("a context[%d] = %s\n", i, a.contexts[i]->any.name);

	for (i = 0; i <= b.level; i++)
		ds_print("b context[%d] = %s\n", i, b.contexts[i]->any.name);

	for (i = 0; i <= h.stack.level; i++)
		ds_print("h context[%d] = %s\n", i, h.stack.contexts[i]->any.name);

	if (!hl_context_stacks_equal(&h.stack, &b)) {
		/* Syntax context changed.  We need to highlight to EOF. */
		struct hl_entry *e;

		truncate_hl_list(&buffer->hl_head, offset_a + 1 - top);
		verify_hl_list(&buffer->hl_head, "truncate");

		verify_hl_list(h.headp, "newline");

		/* add the highlighed lines */
		FOR_EACH_HL_ENTRY(e, h.headp) {
			merge_highlight_entry(&buffer->hl_head, e);
		} END_FOR_EACH_HL_ENTRY(e);
		free_hl_list(h.headp);
		verify_hl_list(&buffer->hl_head, "add1");

		/* highlight to eof */
		h.headp = &buffer->hl_head;
		while (!block_iter_eof(&bi)) {
			fetch_line(&bi);
			h.line = hl_buffer;
			h.line_len = hl_buffer_len;
			h.offset = 0;
			highlight_line(&h);
		}

		update_flags |= UPDATE_FULL;
	} else {
		struct list_head tmp_head;
		struct hl_entry *e;

		split_hl_list(&buffer->hl_head, offset_a + 1 - top, &tmp_head);
		verify_hl_list(&buffer->hl_head, "split-left");
		verify_hl_list(&tmp_head, "split-right");

		delete_hl_range(&tmp_head, 0, offset_b - offset_a + top);
		verify_hl_list(&tmp_head, "delete");

		verify_hl_list(h.headp, "newline");

		/* add the highlighed lines */
		FOR_EACH_HL_ENTRY(e, h.headp) {
			merge_highlight_entry(&buffer->hl_head, e);
		} END_FOR_EACH_HL_ENTRY(e);
		free_hl_list(h.headp);

		/* add rest */
		join_hl_lists(&buffer->hl_head, &tmp_head);

		if (ins_nl)
			update_flags |= UPDATE_FULL;
		else
			update_flags |= UPDATE_CURSOR_LINE;
	}
	if (h.heredoc_context)
		regfree(&h.heredoc_eregex);
	free(h.words);
	free(h.matches);
	free(h.stack.contexts);
	free(a.contexts);
	free(b.contexts);
	verify_hl_list(&buffer->hl_head, "final");
	verify_hl_size();
	full_debug();
	verify_count++;
	verify_counter = 0;
}
