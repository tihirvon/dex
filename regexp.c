#include "regexp.h"
#include "editor.h"

int regexp_match_nosub(const char *pattern, const char *buf, unsigned int len)
{
	regmatch_t m;
	regex_t re;
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE | REG_NOSUB);
	if (rc) {
		regfree(&re);
		return 0;
	}
	rc = buf_regexec(&re, buf, len, 1, &m, 0);
	regfree(&re);
	return !rc;
}

#define REGEXP_SUBSTRINGS 8

char *regexp_matches[REGEXP_SUBSTRINGS + 1];

int regexp_match(const char *pattern, const char *str)
{
	regmatch_t m[REGEXP_SUBSTRINGS];
	regex_t re;
	int err, ret;

	err = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		regfree(&re);
		return 0;
	}
	ret = !regexec(&re, str, REGEXP_SUBSTRINGS, m, 0);
	regfree(&re);
	if (ret) {
		int i;
		for (i = 0; i < REGEXP_SUBSTRINGS; i++) {
			if (m[i].rm_so == -1)
				break;
			regexp_matches[i] = xstrndup(str + m[i].rm_so, m[i].rm_eo - m[i].rm_so);
		}
		regexp_matches[i] = NULL;
	}
	return ret;
}

void free_regexp_matches(void)
{
	int i;

	for (i = 0; i < REGEXP_SUBSTRINGS; i++) {
		free(regexp_matches[i]);
		regexp_matches[i] = NULL;
	}
}

int regexp_compile(regex_t *regexp, const char *pattern, int flags)
{
	int err = regcomp(regexp, pattern, flags);

	if (err) {
		char msg[1024];
		regerror(err, regexp, msg, sizeof(msg));
		error_msg("%s", msg);
		return 0;
	}
	return 1;
}

int buf_regexec(const regex_t *regexp, const char *buf,
	unsigned int size, size_t nr_m, regmatch_t *m, int flags)
{
	BUG_ON(!nr_m);
#ifdef REG_STARTEND
	m[0].rm_so = 0;
	m[0].rm_eo = size;
	return regexec(regexp, buf, nr_m, m, flags | REG_STARTEND);
#else
	// buffer must be null-terminated string if REG_STARTED is not supported
	char *tmp = xnew(char, size + 1);
	int ret;

	memcpy(tmp, buf, size);
	tmp[size] = 0;
	ret = regexec(regexp, tmp, nr_m, m, flags);
	free(tmp);
	return ret;
#endif
}
