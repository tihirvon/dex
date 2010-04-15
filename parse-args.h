#ifndef PARSE_ARGS_H
#define PARSE_ARGS_H

int count_strings(char **strings);
const char *parse_args(char **args, const char *flag_desc, int min, int max);

#endif
