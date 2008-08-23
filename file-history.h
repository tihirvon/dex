#ifndef FILE_HISTORY_H
#define FILE_HISTORY_H

void add_file_history(int x, int y, const char *filename);
void load_file_history(void);
void save_file_history(void);
int find_file_in_history(const char *filename, int *x, int *y);

#endif
