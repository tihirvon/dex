#include "change.h"
#include "editor.h"
#include "buffer.h"
#include "block.h"

struct change {
	struct change_head head;
	unsigned int offset;
	unsigned int del_count;
	unsigned ins_count : 31;
	// after undoing backspace move after the text
	unsigned move_after : 1;
	// deleted bytes (inserted bytes need not to be saved)
	char *buf;
};

static enum change_merge change_merge;
static enum change_merge prev_change_merge;

static struct change *alloc_change(void)
{
	return xcalloc(sizeof(struct change));
}

static void add_change(struct change *change)
{
	struct change_head *head = buffer->cur_change_head;

	change->head.next = head;
	xrenew(head->prev, head->nr_prev + 1);
	head->prev[head->nr_prev++] = &change->head;

	buffer->cur_change_head = &change->head;
}

/* This doesn't need to be local to buffer because commands are atomic. */
static struct change *change_barrier;

static int is_change_chain_barrier(struct change *change)
{
	return !change->ins_count && !change->del_count;
}

static struct change *new_change(void)
{
	struct change *change;

	if (change_barrier) {
		/*
		 * We are recording series of changes (:replace for example)
		 * and now we have just made the first change so we have to
		 * mark beginning of the chain.
		 *
		 * We could have done this before when starting the change
		 * chain but then we may have ended up with an empty chain.
		 * We don't want to record empty changes ever.
		 */
		add_change(change_barrier);
		change_barrier = NULL;
	}

	change = alloc_change();
	add_change(change);
	return change;
}

static unsigned int buffer_offset(void)
{
	return block_iter_get_offset(&view->cursor);
}

void record_insert(unsigned int len)
{
	struct change *change = (struct change *)buffer->cur_change_head;

	BUG_ON(!len);
	if (change_merge == prev_change_merge && change_merge == CHANGE_MERGE_INSERT) {
		BUG_ON(change->del_count);
		change->ins_count += len;
		return;
	}

	change = new_change();
	change->offset = buffer_offset();
	change->ins_count = len;
}

void record_delete(char *buf, unsigned int len, int move_after)
{
	struct change *change = (struct change *)buffer->cur_change_head;

	BUG_ON(!len);
	BUG_ON(!buf);
	if (change_merge == prev_change_merge) {
		if (change_merge == CHANGE_MERGE_DELETE) {
			xrenew(change->buf, change->del_count + len);
			memcpy(change->buf + change->del_count, buf, len);
			change->del_count += len;
			free(buf);
			return;
		}
		if (change_merge == CHANGE_MERGE_ERASE) {
			xrenew(buf, len + change->del_count);
			memcpy(buf + len, change->buf, change->del_count);
			change->del_count += len;
			free(change->buf);
			change->buf = buf;
			change->offset -= len;
			return;
		}
	}

	change = new_change();
	change->offset = buffer_offset();
	change->del_count = len;
	change->move_after = move_after;
	change->buf = buf;
}

void record_replace(char *deleted, unsigned int del_count, unsigned int ins_count)
{
	struct change *change;

	BUG_ON(del_count && !deleted);
	BUG_ON(!del_count && deleted);
	BUG_ON(!del_count && !ins_count);

	change = new_change();
	change->offset = buffer_offset();
	change->ins_count = ins_count;
	change->del_count = del_count;
	change->buf = deleted;
}

void begin_change(enum change_merge m)
{
	change_merge = m;
}

void end_change(void)
{
	prev_change_merge = change_merge;
}

void begin_change_chain(void)
{
	BUG_ON(change_barrier);

	/*
	 * Allocate change chain barrier but add it to the change tree only if
	 * there will be any real changes
	 */
	change_barrier = alloc_change();
	change_merge = CHANGE_MERGE_NONE;
}

void end_change_chain(void)
{
	if (change_barrier) {
		/* There were no changes in this change chain. */
		free(change_barrier);
		change_barrier = NULL;
	} else {
		/* There were some changes. Add end of chain marker. */
		add_change(alloc_change());
	}
}

static void reverse_change(struct change *change)
{
	block_iter_goto_offset(&view->cursor, change->offset);
	if (!change->ins_count) {
		// convert delete to insert
		do_insert(change->buf, change->del_count);
		if (change->move_after)
			block_iter_skip_bytes(&view->cursor, change->del_count);
		change->ins_count = change->del_count;
		change->del_count = 0;
		free(change->buf);
		change->buf = NULL;
	} else if (change->del_count) {
		// reverse replace
		unsigned int ins_count = change->ins_count;
		char *buf = do_delete(ins_count);

		do_insert(change->buf, change->del_count);
		free(change->buf);
		change->buf = buf;
		change->ins_count = change->del_count;
		change->del_count = ins_count;
	} else {
		// convert insert to delete
		change->buf = do_delete(change->ins_count);
		change->del_count = change->ins_count;
		change->ins_count = 0;
	}
}

int undo(void)
{
	struct change_head *head = buffer->cur_change_head;
	struct change *change;

	if (!head->next)
		return 0;

	change = (struct change *)head;
	if (is_change_chain_barrier(change)) {
		int count = 0;

		while (1) {
			head = head->next;
			change = (struct change *)head;
			if (is_change_chain_barrier(change))
				break;
			reverse_change(change);
			count++;
		}
		if (count > 1)
			info_msg("Undid %d changes.", count);
	} else {
		reverse_change(change);
	}
	buffer->cur_change_head = head->next;
	return 1;
}

int redo(unsigned int change_id)
{
	struct change_head *head = buffer->cur_change_head;
	struct change *change;

	if (!head->prev) {
		/* don't complain if change_id is 0 */
		if (change_id)
			error_msg("Nothing to redo.");
		return 0;
	}

	if (change_id) {
		if (--change_id >= head->nr_prev) {
			error_msg("There are only %d possible changes to redo.", head->nr_prev);
			return 0;
		}
	} else {
		/* default to newest change  */
		change_id = head->nr_prev - 1;
		if (head->nr_prev > 1)
			info_msg("Redoing newest (%d) of %d possible changes.", change_id + 1, head->nr_prev);
	}

	head = head->prev[change_id];
	change = (struct change *)head;
	if (is_change_chain_barrier(change)) {
		int count = 0;

		while (1) {
			head = head->prev[head->nr_prev - 1];
			change = (struct change *)head;
			if (is_change_chain_barrier(change))
				break;
			reverse_change(change);
			count++;
		}
		if (count > 1)
			info_msg("Redid %d changes.", count);
	} else {
		reverse_change(change);
	}
	buffer->cur_change_head = head;
	return 1;
}

void free_changes(void *head)
{
	struct change_head *ch = head;
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
