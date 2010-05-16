#ifndef HIGHLIGHT_H
#define HIGHLIGHT_H

#include "syntax.h"

struct hl_entry {
	/*
	 * top 2 bits:     type (HL_ENTRY_*)
	 * bottom 6 bits:  high bits of index to syntax_nodes[]
	 */
	unsigned char desc_high;

	/* low bits of index to syntax_nodes[] */
	unsigned char desc_low;

	/* length of highlight in bytes */
	unsigned char len;
};

#define HL_ENTRY_NORMAL	0x00
#define HL_ENTRY_SOC	0x40
#define HL_ENTRY_EOC	0x80

static inline unsigned int hl_entry_len(const struct hl_entry *e)
{
	return e->len;
}

static inline unsigned int hl_entry_type(const struct hl_entry *e)
{
	return e->desc_high & 0xc0;
}

static inline unsigned int hl_entry_idx(const struct hl_entry *e)
{
	return ((e->desc_high & 0x3f) << 8) | e->desc_low;
}

static inline unsigned int hl_entry_desc(const struct hl_entry *e)
{
	return (e->desc_high << 8) | e->desc_low;
}

static inline unsigned int make_hl_entry_desc(unsigned int type, unsigned int idx)
{
	return (type << 8) | idx;
}

static inline unsigned int get_hl_entry_desc_high(unsigned int desc)
{
	return desc >> 8;
}

static inline unsigned int get_hl_entry_desc_low(unsigned int desc)
{
	return desc & 0xff;
}

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
	int heredoc_offset;
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

	const struct syntax_context *heredoc_context;
	regex_t heredoc_eregex;

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
};

struct hl_iterator {
	const struct hl_color *color;
	const struct hl_list *list;
	int entry_idx;
	int entry_pos;
};

static inline struct hl_list *HL_LIST(struct list_head *item)
{
	return container_of(item, struct hl_list, node);
}

void copy_syntax_context_stack(struct syntax_context_stack *dst, const struct syntax_context_stack *src);
void push_syntax_context(struct syntax_context_stack *stack, const struct syntax_context *context);

static inline void pop_syntax_context(struct syntax_context_stack *stack)
{
	stack->contexts[stack->level--] = NULL;
}

static inline void init_syntax_context_stack(struct syntax_context_stack *stack, const struct syntax_context *root)
{
	stack->contexts = NULL;
	stack->level = -1;
	stack->alloc = 0;
	stack->heredoc_offset = -1;
	push_syntax_context(stack, root);
}

void update_syntax_colors(void);
void update_all_syntax_colors(void);

int build_heredoc_eregex(struct highlighter *h, const struct syntax_context *context,
	const char *str, int len);
void merge_highlight_entry(struct list_head *head, const struct hl_entry *e);
void highlight_line(struct highlighter *h);
void free_hl_list(struct list_head *head);
void truncate_hl_list(struct list_head *head, unsigned int new_size);
void split_hl_list(struct list_head *head, unsigned int offset, struct list_head *other);
void delete_hl_range(struct list_head *head, unsigned int so, unsigned int eo);
void join_hl_lists(struct list_head *head, struct list_head *other);
void hl_iter_set_pos(struct hl_iterator *iter, struct list_head *hl_head, unsigned int offset);
void hl_iter_advance(struct hl_iterator *iter, unsigned int count);

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
