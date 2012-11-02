#include "regexp.h"
#include "error.h"
#include "common.h"

bool regexp_match_nosub(const char *pattern, const char *buf, unsigned int len)
{
	regmatch_t m;
	regex_t re;
	int rc;

	rc = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE | REG_NOSUB);
	if (rc) {
		error_msg("Invalid regexp: %s", pattern);
		return 0;
	}
	rc = regexp_exec(&re, buf, len, 1, &m, 0);
	regfree(&re);
	return rc;
}

bool regexp_match(const char *pattern, const char *buf, long size, struct ptr_array *m)
{
	regex_t re;
	int err;
	bool ret;

	err = regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE);
	if (err) {
		error_msg("Invalid regexp: %s", pattern);
		return false;
	}
	ret = regexp_exec_sub(&re, buf, size, m, 0);
	regfree(&re);
	return ret;
}

bool regexp_compile(regex_t *re, const char *pattern, int flags)
{
	int err = regcomp(re, pattern, flags);

	if (err) {
		char msg[1024];
		regerror(err, re, msg, sizeof(msg));
		error_msg("%s: %s", msg, pattern);
		return false;
	}
	return true;
}

bool regexp_exec(const regex_t *re, const char *buf, long size, long nr_m, regmatch_t *m, int flags)
{
#ifdef REG_STARTEND
	BUG_ON(!nr_m);
	m[0].rm_so = 0;
	m[0].rm_eo = size;
	return !regexec(re, buf, nr_m, m, flags | REG_STARTEND);
#else
	// buffer must be null-terminated string if REG_STARTED is not supported
	char *tmp = xnew(char, size + 1);
	int ret;

	BUG_ON(!nr_m);
	memcpy(tmp, buf, size);
	tmp[size] = 0;
	ret = !regexec(re, tmp, nr_m, m, flags);
	free(tmp);
	return ret;
#endif
}

bool regexp_exec_sub(const regex_t *re, const char *buf, long size, struct ptr_array *matches, int flags)
{
	regmatch_t m[16];
	bool ret = regexp_exec(re, buf, size, ARRAY_COUNT(m), m, flags);
	int i;

	if (!ret)
		return false;
	for (i = 0; i < ARRAY_COUNT(m); i++) {
		if (m[i].rm_so == -1)
			break;
		ptr_array_add(matches, xstrslice(buf, m[i].rm_so, m[i].rm_eo));
	}
	return true;
}
