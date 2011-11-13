#ifndef STATE_H
#define STATE_H

struct syntax *load_syntax_file(const char *filename, int must_exist);
struct syntax *load_syntax_by_filetype(const char *filetype);

#endif
