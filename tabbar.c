#include "tabbar.h"
#include "window.h"
#include "uchar.h"

static int first_tab_idx;

static int filename_width(const char *filename)
{
	int w = 0, i = 0;

	if (term_utf8) {
		while (filename[i])
			w += u_char_width(u_buf_get_char(filename, i + 4, &i));
	} else {
		// latin1 is subset of unicode
		while (filename[i])
			w += u_char_width(filename[i++]);
	}
	return w;
}

static int tab_title_width(int number, const char *filename)
{
	return 3 + number_width(number) + filename_width(filename);
}

static void update_tab_title_width(struct view *v, int tab_number)
{
	int w = tab_title_width(tab_number, buffer_filename(v->buffer));

	v->tt_width = w;
	v->tt_truncated_width = w;
}

static void update_first_tab_idx(void)
{
	int min_first_idx, max_first_idx, w;

	w = 0;
	for (max_first_idx = window->views.count; max_first_idx > 0; max_first_idx--) {
		struct view *v = window->views.ptrs[max_first_idx - 1];
		w += v->tt_truncated_width;
		if (w > window->w)
			break;
	}

	w = 0;
	for (min_first_idx = window->views.count; min_first_idx > 0; min_first_idx--) {
		struct view *v = window->views.ptrs[min_first_idx - 1];
		if (w || v == view)
			w += v->tt_truncated_width;
		if (w > window->w)
			break;
	}

	if (first_tab_idx < min_first_idx)
		first_tab_idx = min_first_idx;
	if (first_tab_idx > max_first_idx)
		first_tab_idx = max_first_idx;
}

int calculate_tabbar(void)
{
	int extra, i, truncated_count, total_w = 0;

	for (i = 0; i < window->views.count; i++) {
		struct view *v = window->views.ptrs[i];

		if (v == view) {
			// make sure current tab is visible
			if (first_tab_idx > i)
				first_tab_idx = i;
		}
		update_tab_title_width(v, i + 1);
		total_w += v->tt_width;
	}

	if (total_w <= window->w) {
		// all tabs fit without truncating
		first_tab_idx = 0;
		return first_tab_idx;
	}

	// truncate all wide tabs
	total_w = 0;
	truncated_count = 0;
	for (i = 0; i < window->views.count; i++) {
		struct view *v = window->views.ptrs[i];
		int truncated_w = 20;

		if (v->tt_width > truncated_w) {
			v->tt_truncated_width = truncated_w;
			total_w += truncated_w;
			truncated_count++;
		} else {
			total_w += v->tt_width;
		}
	}

	if (total_w > window->w) {
		// not all tabs fit even after truncating wide tabs
		update_first_tab_idx();
		return first_tab_idx;
	}

	// all tabs fit after truncating wide tabs
	extra = window->w - total_w;

	// divide extra space between truncated tabs
	while (extra > 0) {
		int extra_avg = extra / truncated_count;
		int extra_mod = extra % truncated_count;

		for (i = 0; i < window->views.count; i++) {
			struct view *v = window->views.ptrs[i];
			int add = v->tt_width - v->tt_truncated_width;
			int avail;

			if (add == 0)
				continue;

			avail = extra_avg;
			if (extra_mod) {
				// this is needed for equal divide
				if (extra_avg == 0) {
					avail++;
					extra_mod--;
				}
			}
			if (add > avail)
				add = avail;
			else
				truncated_count--;

			v->tt_truncated_width += add;
			extra -= add;
		}
	}

	first_tab_idx = 0;
	return first_tab_idx;
}
