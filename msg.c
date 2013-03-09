#include "msg.h"
#include "window.h"
#include "ptr-array.h"
#include "search.h"
#include "move.h"
#include "error.h"

static PTR_ARRAY(file_locations);
static PTR_ARRAY(msgs);
static int msg_pos;

static struct file_location *create_location(void)
{
	struct file_location *loc;

	loc = xnew0(struct file_location, 1);
	loc->filename = buffer->abs_filename ? xstrdup(buffer->abs_filename) : NULL;
	loc->buffer_id = buffer->id;
	loc->line = view->cy + 1;
	loc->column = view->cx_char + 1;
	return loc;
}

static bool move_to_file(const char *filename, bool save_location)
{
	struct file_location *loc = NULL;
	struct view *v;

	if (save_location)
		loc = create_location();

	v = open_buffer(filename, true, NULL);
	if (!v) {
		if (loc) {
			file_location_free(loc);
		}
		return false;
	}
	if (loc)
		ptr_array_add(&file_locations, loc);

	if (view != v) {
		set_view(v);
		/* force centering view to the cursor because file changed */
		view->force_center = true;
	}
	return true;
}

void pop_location(void)
{
	struct file_location *loc;
	struct view *v;

	if (file_locations.count == 0)
		return;
	loc = file_locations.ptrs[--file_locations.count];
	v = find_view_by_buffer_id(loc->buffer_id);
	if (!v) {
		if (loc->filename) {
			v = open_buffer(loc->filename, true, NULL);
		} else {
			// Can't restore closed buffer which had no filename.
			file_location_free(loc);
			pop_location();
			return;
		}
	}
	if (v) {
		set_view(v);
		move_to_line(loc->line);
		move_to_column(loc->column);
	}
	file_location_free(loc);
}

static void free_message(struct message *m)
{
	free(m->msg);
	if (m->loc != NULL) {
		file_location_free(m->loc);
	}
	free(m);
}

static bool message_equals(const struct message *a, const struct message *b)
{
	if (!streq(a->msg, b->msg)) {
		return false;
	}
	if (a->loc == NULL) {
		return b->loc == NULL;
	}
	if (b->loc == NULL) {
		return false;
	}
	return file_location_equals(a->loc, b->loc);
}

static bool is_duplicate(const struct message *m)
{
	int i;

	for (i = 0; i < msgs.count; i++) {
		if (message_equals(m, msgs.ptrs[i])) {
			return true;
		}
	}
	return false;
}

struct message *new_message(const char *msg)
{
	struct message *m = xnew0(struct message, 1);
	m->msg = xstrdup(msg);
	return m;
}

void add_message(struct message *m)
{
	if (is_duplicate(m)) {
		free_message(m);
	} else {
		ptr_array_add(&msgs, m);
	}
}

void current_message(bool save_location)
{
	struct message *m;
	int go = 0;

	if (msg_pos == msgs.count)
		return;

	m = msgs.ptrs[msg_pos];
	if (m->loc != NULL && move_to_file(m->loc->filename, save_location))
		go = 1;

	// search_tag() can print error so do this before it
	info_msg("[%d/%ld] %s", msg_pos + 1, msgs.count, m->msg);
	if (!go)
		return;

	if (m->loc->pattern != NULL) {
		search_tag(m->loc->pattern);
	} else if (m->loc->line > 0) {
		move_to_line(m->loc->line);
		if (m->loc->column > 0) {
			move_to_column(m->loc->column);
		}
	}
}

void next_message(void)
{
	if (msg_pos + 1 < msgs.count)
		msg_pos++;
	current_message(false);
}

void prev_message(void)
{
	if (msg_pos > 0)
		msg_pos--;
	current_message(false);
}

void clear_messages(void)
{
	int i;

	for (i = 0; i < msgs.count; i++)
		free_message(msgs.ptrs[i]);
	msgs.count = 0;
	msg_pos = 0;
}

int message_count(void)
{
	return msgs.count;
}
