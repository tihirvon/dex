#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include "syntax.h"

/*
 * Most syntax nodes are very short so 8 bits seems optimal.
 *
 * We also limit number of syntax nodes to 64 which is more than enough for
 * anything sane.
 */
struct hl_entry {
	/*
	 * top 2 bits is type
	 * bottom 6 bits is index to syntax->nodes[]
	 */
	unsigned char desc;
	unsigned char len;
};

#define HL_ENTRY_NORMAL	0x00
#define HL_ENTRY_SOC	0x40
#define HL_ENTRY_EOC	0x80

#define hl_entry_len(e) (e)->len

static inline unsigned int hl_entry_type(const struct hl_entry *e)
{
	return e->desc & 0xc0;
}

static inline unsigned int hl_entry_idx(const struct hl_entry *e)
{
	return e->desc & 0x3f;
}

static inline int hl_entry_is_eoc(const struct hl_entry *e)
{
	return hl_entry_type(e) == HL_ENTRY_EOC;
}

#define HL_LIST(ptr) container_of(ptr, struct hl_list, node)

struct hl_list {
	struct list_head node;
	int count;
	struct hl_entry entries[(256 - sizeof(struct list_head) - sizeof(int)) / sizeof(struct hl_entry)];
};

struct hl_match {
	const union syntax_node *node;
	regmatch_t match;
	int eoc;
};

struct syntax_context_stack {
	const struct syntax_context **contexts;
	int level;
	unsigned int alloc;
};

struct hl_word {
	unsigned int hash;
	int offset;
	int len;
	int used;
};

struct highlighter {
	struct list_head *headp;
	const struct syntax *syn;

	const char *line;
	unsigned int line_len;
	unsigned int offset;

	struct syntax_context_stack stack;

	struct hl_match *matches;
	int nr_matches;
	int alloc;

	struct hl_word *words;
	int word_count;
	int word_alloc;
	int used_words;
};

void copy_syntax_context_stack(struct syntax_context_stack *dst, const struct syntax_context_stack *src);
void push_syntax_context(struct syntax_context_stack *stack, const struct syntax_context *context);

static inline void pop_syntax_context(struct syntax_context_stack *stack)
{
	BUG_ON(!stack->level);
	stack->contexts[stack->level--] = NULL;
}

static inline void init_syntax_context_stack(struct syntax_context_stack *stack, const struct syntax_context *root)
{
	stack->contexts = NULL;
	stack->level = -1;
	stack->alloc = 0;
	push_syntax_context(stack, root);
}

void update_syntax_colors(void);
void update_all_syntax_colors(void);

void merge_highlight_entry(struct list_head *head, const struct hl_entry *e);
void highlight_line(struct highlighter *h);
void free_hl_list(struct list_head *head);
void truncate_hl_list(struct list_head *head, unsigned int new_size);
void split_hl_list(struct list_head *head, unsigned int offset, struct list_head *other);
void delete_hl_range(struct list_head *head, unsigned int so, unsigned int eo);
void join_hl_lists(struct list_head *head, struct list_head *other);

#define DO_FOR_EACH_HL_ENTRY(entry, head, __head, __list, __i)	\
do {								\
	struct list_head *__head = head;			\
	struct hl_list *__list;					\
	int __i;						\
	list_for_each_entry(__list, __head, node) {		\
		for (__i = 0; __i < __list->count; __i++) {	\
			entry = &__list->entries[__i];		\
			do {

#define DO_END_FOR_EACH_HL_ENTRY(__head, __list)		\
			} while (0);				\
		}						\
	}							\
} while (0)

#define FOR_EACH_HL_ENTRY(entry, head) \
	DO_FOR_EACH_HL_ENTRY(entry, head, __head##entry, __list##entry, __i##entry)
#define END_FOR_EACH_HL_ENTRY(entry) \
	DO_END_FOR_EACH_HL_ENTRY(__head##entry, __list##entry)

#endif
