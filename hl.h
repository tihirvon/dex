#ifndef HL_H
#define HL_H

struct hl_color **hl_line(const char *line, int len, int line_nr, int *next_changed);
void hl_fill_start_states(int line_nr);
void hl_insert(int first, int lines);
void hl_delete(int first, int lines);

#endif
