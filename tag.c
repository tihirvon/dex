#include "tag.h"
#include "ctags.h"
#include "buffer.h"
#include "list.h"
#include "ptr-array.h"
#include "completion.h"

static struct tag_file *tag_file;
static const char *current_filename; // for sorting tags

static int visibility_cmp(const struct tag *a, const struct tag *b)
{
	bool a_this_file = false;
	bool b_this_file = false;

	if (!a->local && !b->local)
		return 0;

	// Is tag visibility limited to the current file?
	if (a->local)
		a_this_file = current_filename && streq(current_filename, a->filename);
	if (b->local)
		b_this_file = current_filename && streq(current_filename, b->filename);

	// Tags local to other file than current are not interesting.
	if (a->local && !a_this_file) {
		// a is not interesting
		if (b->local && !b_this_file) {
			// b is equally uninteresting
			return 0;
		}
		// b is more interesting, sort it before a
		return 1;
	}
	if (b->local && !b_this_file) {
		// b is not interesting
		return -1;
	}

	// both are NOT UNinteresting

	if (a->local && a_this_file) {
		if (b->local && b_this_file)
			return 0;
		// a is more interesting bacause it is local symbol
		return -1;
	}
	if (b->local && b_this_file) {
		// b is more interesting bacause it is local symbol
		return 1;
	}
	return 0;
}

static int kind_cmp(const struct tag *a, const struct tag *b)
{
	if (a->kind == b->kind)
		return 0;

	// Struct member (m) is not very interesting.
	if (a->kind == 'm')
		return 1;
	if (b->kind == 'm')
		return -1;

	// Global variable (v) is not very interesting.
	if (a->kind == 'v')
		return 1;
	if (b->kind == 'v')
		return -1;

	// Struct (s), union (u)
	return 0;
}

static int tag_cmp(const void *ap, const void *bp)
{
	const struct tag *a = *(const struct tag **)ap;
	const struct tag *b = *(const struct tag **)bp;
	int ret;

	ret = visibility_cmp(a, b);
	if (ret)
		return ret;

	return kind_cmp(a, b);
}

static void sort_tags(struct ptr_array *tags)
{
	current_filename = NULL;
	if (buffer->abs_filename)
		current_filename = strrchr(buffer->abs_filename, '/') + 1;

	qsort(tags->ptrs, tags->count, sizeof(tags->ptrs[0]), tag_cmp);
}

static int tag_file_changed(const char *filename, struct tag_file *tf)
{
	struct stat st;
	stat(filename, &st);
	return st.st_mtime != tf->mtime;
}

static bool load_tag_file(void)
{
	if (tag_file && tag_file_changed("tags", tag_file)) {
		close_tag_file(tag_file);
		tag_file = NULL;
	}
	if (tag_file)
		return true;

	tag_file = open_tag_file("tags");
	return !!tag_file;
}

void free_tags(struct ptr_array *tags)
{
	int i;
	for (i = 0; i < tags->count; i++) {
		struct tag *t = tags->ptrs[i];
		free_tag(t);
		free(t);
	}
	free(tags->ptrs);
	clear(tags);
}

bool find_tags(const char *name, struct ptr_array *tags)
{
	struct tag *t;
	size_t pos = 0;

	if (!load_tag_file())
		return false;

	t = xnew(struct tag, 1);
	while (next_tag(tag_file, &pos, name, 1, t)) {
		ptr_array_add(tags, t);
		t = xnew(struct tag, 1);
	}
	free(t);
	sort_tags(tags);
	return true;
}

void collect_tags(const char *prefix)
{
	struct tag t;
	size_t pos = 0;
	char *prev = NULL;

	if (!load_tag_file())
		return;

	while (next_tag(tag_file, &pos, prefix, 0, &t)) {
		if (!prev || !streq(prev, t.name)) {
			add_completion(t.name);
			prev = t.name;
			t.name = NULL;
		}
		free_tag(&t);
	}
}
