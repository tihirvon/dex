#include "change.h"
#include "buffer.h"

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

void record_insert(unsigned int len)
{
	struct change *change = (struct change *)buffer->cur_change_head;

	BUG_ON(!len);
	if (undo_merge == UNDO_MERGE_INSERT && change && !change->del_count) {
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
	if (change && !change->ins_count) {
		if (undo_merge == UNDO_MERGE_DELETE) {
			xrenew(change->buf, change->del_count + len);
			memcpy(change->buf + change->del_count, buf, len);
			change->del_count += len;
			free(buf);
			return;
		}
		if (undo_merge == UNDO_MERGE_BACKSPACE) {
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

void begin_change_chain(void)
{
	BUG_ON(change_barrier);

	/*
	 * Allocate change chain barrier but add it to the change tree only if
	 * there will be any real changes
	 */
	change_barrier = alloc_change();
	undo_merge = UNDO_MERGE_NONE;
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
	move_offset(change->offset);
	if (!change->ins_count) {
		// convert delete to insert
		do_insert(change->buf, change->del_count);
		if (change->move_after)
			move_offset(change->offset + change->del_count);
		change->ins_count = change->del_count;
		change->del_count = 0;
		update_preferred_x();
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
		update_preferred_x();
	}
}

void undo(void)
{
	struct change_head *head = buffer->cur_change_head;
	struct change *change;

	undo_merge = UNDO_MERGE_NONE;
	if (!head->next)
		return;

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
		if (count > 1) {
			info_msg("Undid %d changes.", count);
			update_flags |= UPDATE_FULL;
		}
	} else {
		reverse_change(change);
	}
	buffer->cur_change_head = head->next;
}

void redo(void)
{
	struct change_head *head = buffer->cur_change_head;
	struct change *change;

	undo_merge = UNDO_MERGE_NONE;
	if (!head->prev)
		return;

	head = head->prev[head->nr_prev - 1];
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
		if (count > 1) {
			info_msg("Redid %d changes.", count);
			update_flags |= UPDATE_FULL;
		}
	} else {
		reverse_change(change);
	}
	buffer->cur_change_head = head;
}
