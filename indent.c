#include "indent.h"
#include "buffer.h"

// get indentation of current or previous non-whitespace-only line
char *get_indent(void)
{
	struct block_iter bi = view->cursor;

	block_iter_bol(&bi);
	do {
		struct lineref lr;
		int i;

		fill_line_ref(&bi, &lr);
		for (i = 0; i < lr.size; i++) {
			char ch = lr.line[i];

			if (ch != ' ' && ch != '\t') {
				char *str;

				if (!i)
					return NULL;
				str = xmemdup(lr.line, i + 1);
				str[i] = 0;
				return str;
			}
		}
	} while (block_iter_prev_line(&bi));
	return NULL;
}

void get_indent_info(const char *buf, int len, struct indent_info *info)
{
	int spaces = 0;
	int tabs = 0;
	int pos = 0;

	memset(info, 0, sizeof(struct indent_info));
	info->sane = 1;
	while (pos < len) {
		if (buf[pos] == ' ') {
			info->width++;
			spaces++;
		} else if (buf[pos] == '\t') {
			int tw = buffer->options.tab_width;
			info->width = (info->width + tw) / tw * tw;
			tabs++;
		} else {
			break;
		}
		info->bytes++;
		pos++;

		if (info->width % buffer->options.indent_width == 0 && info->sane)
			info->sane = use_spaces_for_indent() ? !tabs : !spaces;
	}
	info->level = info->width / buffer->options.indent_width;
	info->wsonly = pos == len;
}

int use_spaces_for_indent(void)
{
	return buffer->options.expand_tab || buffer->options.indent_width != buffer->options.tab_width;
}

static int get_current_indent_bytes(const char *buf, int cursor_offset)
{
	int tw = buffer->options.tab_width;
	int ibytes = 0;
	int iwidth = 0;
	int i;

	for (i = 0; i < cursor_offset; i++) {
		char ch = buf[i];

		if (iwidth % buffer->options.indent_width == 0) {
			ibytes = 0;
			iwidth = 0;
		}

		if (ch == '\t') {
			iwidth = (iwidth + tw) / tw * tw;
		} else if (ch == ' ') {
			iwidth++;
		} else {
			// cursor not at indentation
			return -1;
		}
		ibytes++;
	}

	if (iwidth % buffer->options.indent_width) {
		// cursor at middle of indentation level
		return -1;
	}
	return ibytes;
}

int get_indent_level_bytes_left(void)
{
	struct lineref lr;
	unsigned int cursor_offset = fetch_this_line(&view->cursor, &lr);
	int ibytes;

	if (!cursor_offset)
		return 0;

	ibytes = get_current_indent_bytes(lr.line, cursor_offset);
	if (ibytes < 0)
		return 0;
	return ibytes;
}

int get_indent_level_bytes_right(void)
{
	struct lineref lr;
	unsigned int cursor_offset = fetch_this_line(&view->cursor, &lr);
	int tw = buffer->options.tab_width;
	int i, ibytes, iwidth;

	ibytes = get_current_indent_bytes(lr.line, cursor_offset);
	if (ibytes < 0)
		return 0;

	iwidth = 0;
	for (i = cursor_offset; i < lr.size; i++) {
		char ch = lr.line[i];

		if (ch == '\t') {
			iwidth = (iwidth + tw) / tw * tw;
		} else if (ch == ' ') {
			iwidth++;
		} else {
			// no full indentation level at cursor position
			return 0;
		}

		if (iwidth % buffer->options.indent_width == 0)
			return i - cursor_offset + 1;
	}
	return 0;
}

char *alloc_indent(int count, int *sizep)
{
	char *indent;
	int size;

	if (use_spaces_for_indent()) {
		size = buffer->options.indent_width * count;
		indent = xnew(char, size);
		memset(indent, ' ', size);
	} else {
		size = count;
		indent = xnew(char, size);
		memset(indent, '\t', size);
	}
	*sizep = size;
	return indent;
}
