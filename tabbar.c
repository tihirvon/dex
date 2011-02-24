#include "tabbar.h"
#include "window.h"
#include "term.h"
#include "uchar.h"

static int leftmost_tab_idx;

static unsigned int number_width(unsigned int n)
{
	unsigned int width = 0;

	do {
		n /= 10;
		width++;
	} while (n);
	return width;
}

static void update_tab_title_width(struct view *v, int idx)
{
	const char *filename = v->buffer->filename;
	unsigned int w;

	if (!filename)
		filename = "(No name)";

	w = 3 + number_width(idx + 1);
	if (term_flags & TERM_UTF8) {
		unsigned int i = 0;
		while (filename[i])
			w += u_char_width(u_buf_get_char(filename, i + 4, &i));
	} else {
		w += strlen(filename);
	}

	v->tt_width = w;
	v->tt_truncated_width = w;
}

int calculate_tab_bar(void)
{
	struct view *v;
	int trunc_min_w = 20;
	int count = 0, total_len = 0;
	int trunc_count = 0, max_trunc_w = 0;

	list_for_each_entry(v, &window->views, node) {
		if (v == view) {
			/* make sure current tab is visible */
			if (leftmost_tab_idx > count)
				leftmost_tab_idx = count;
		}
		update_tab_title_width(v, count);
		if (v->tt_width > trunc_min_w) {
			max_trunc_w += v->tt_width - trunc_min_w;
			trunc_count++;
		}
		total_len += v->tt_width;
		count++;
	}

	if (total_len <= window->w) {
		leftmost_tab_idx = 0;
	} else {
		int extra = total_len - window->w;

		if (extra <= max_trunc_w) {
			/* All tabs fit to screen after truncating some titles */
			int avg = extra / trunc_count;
			int mod = extra % trunc_count;

			list_for_each_entry(v, &window->views, node) {
				int w = v->tt_width - trunc_min_w;
				if (w > 0) {
					w = avg;
					if (mod) {
						w++;
						mod--;
					}
				}
				if (w > 0)
					v->tt_truncated_width = v->tt_width - w;
			}
			leftmost_tab_idx = 0;
		} else {
			/* Need to truncate all long titles but there's still
			 * not enough space for all tabs */
			int min_left_idx, max_left_idx, w;

			list_for_each_entry(v, &window->views, node) {
				w = v->tt_width - trunc_min_w;
				if (w > 0)
					v->tt_truncated_width = v->tt_width - w;
			}

			w = 0;
			max_left_idx = count;
			list_for_each_entry_reverse(v, &window->views, node) {
				w += v->tt_truncated_width;
				if (w > window->w)
					break;
				max_left_idx--;
			}

			w = 0;
			min_left_idx = count;
			list_for_each_entry_reverse(v, &window->views, node) {
				if (w || v == view)
					w += v->tt_truncated_width;
				if (w > window->w)
					break;
				min_left_idx--;
			}
			if (leftmost_tab_idx < min_left_idx)
				leftmost_tab_idx = min_left_idx;
			if (leftmost_tab_idx > max_left_idx)
				leftmost_tab_idx = max_left_idx;
		}
	}
	return leftmost_tab_idx;
}
