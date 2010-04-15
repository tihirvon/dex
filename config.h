#ifndef CONFIG_H
#define CONFIG_H

extern const char *config_file;
extern int config_line;

int read_config(const char *filename, int must_exist);

#endif
