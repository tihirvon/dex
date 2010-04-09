#include "term.h"
#include "util.h"

static const char *str_cap_names[] = {
	"ce",
	"ks",
	"ke",
	"te",
	"ti",
	"ve",
	"vi",

	/*
	 * keys
	 */

	// bc?
	"kb",
	"kI",
	"kD",
	"kh",
	"@7",
	"kP",
	"kN",
	"kl",
	"kr",
	"ku",
	"kd",
	"k1",
	"k2",
	"k3",
	"k4",
	"k5",
	"k6",
	"k7",
	"k8",
	"k9",
	"k;",
	"F1",
	"F2",
	"#4",
	"%i",
	NULL
};

static char **str_cap_ptrs[] = {
	&term_cap.ce,
	&term_cap.ks,
	&term_cap.ke,
	&term_cap.te,
	&term_cap.ti,
	&term_cap.ve,
	&term_cap.vi,

	&term_keycodes[SKEY_BACKSPACE],
	&term_keycodes[SKEY_INSERT],
	&term_keycodes[SKEY_DELETE],
	&term_keycodes[SKEY_HOME],
	&term_keycodes[SKEY_END],
	&term_keycodes[SKEY_PAGE_UP],
	&term_keycodes[SKEY_PAGE_DOWN],
	&term_keycodes[SKEY_LEFT],
	&term_keycodes[SKEY_RIGHT],
	&term_keycodes[SKEY_UP],
	&term_keycodes[SKEY_DOWN],
	&term_keycodes[SKEY_F1],
	&term_keycodes[SKEY_F2],
	&term_keycodes[SKEY_F3],
	&term_keycodes[SKEY_F4],
	&term_keycodes[SKEY_F5],
	&term_keycodes[SKEY_F6],
	&term_keycodes[SKEY_F7],
	&term_keycodes[SKEY_F8],
	&term_keycodes[SKEY_F9],
	&term_keycodes[SKEY_F10],
	&term_keycodes[SKEY_F11],
	&term_keycodes[SKEY_F12],
	&term_keycodes[SKEY_SHIFT_LEFT],
	&term_keycodes[SKEY_SHIFT_RIGHT],
	NULL
};

static int next_entry(const char *buf, int size, int pos)
{
start:
	/* skip white space from beginning of a line */
	while (pos < size && isspace(buf[pos]))
		pos++;
	if (pos < size && buf[pos] == '#') {
		while (pos < size) {
			int ch = buf[pos++];

			if (ch == '\n')
				goto start;
		}
	}
	return pos;
}

static int skip_entry(const char *buf, int size, int pos)
{
	int prev = 0;

	while (pos < size) {
		int ch = buf[pos++];

		if (ch == '\n' && prev != '\\')
			break;
		prev = ch;
	}
	return pos;
}

static char *get_entry(const char *buf, int size, int pos)
{
	char *entry = NULL;
	int entry_size = 0, entry_len = 0, prev = 0;

	/* skip names */
	while (pos < size && buf[pos] != ':')
		pos++;

	do {
		int start, len;

		while (pos < size && (buf[pos] == '\t' || buf[pos] == ' '))
			pos++;

		start = pos;
		while (pos < size) {
			int ch = buf[pos++];

			if (ch == '\n')
				break;
			prev = ch;
		}

		len = pos - start - 1;
		if (prev == '\\')
			len--;
		if (len > 0) {
			while (entry_size - entry_len <= len) {
				entry_size = entry_size * 2 + 64;
				xrenew(entry, entry_size);
			}
			memcpy(entry + entry_len, buf + start, len);
			entry_len += len;

			entry[entry_len] = 0;
		}
	} while (prev == '\\');

	if (entry)
		entry[entry_len] = 0;
	return entry;
}

static char *termcap_find(const char *buf, int size, const char *name)
{
	int len = strlen(name), pos = 0, end;

	while (1) {
		pos = next_entry(buf, size, pos);
		if (pos == size)
			return NULL;

		/* see if @name is in the list of terminal names */
		do {
			end = pos;
			while (end < size && buf[end] != '|' && buf[end] != ':')
				end++;

			if (end - pos == len && strncasecmp(buf + pos, name, len) == 0)
				return get_entry(buf, size, pos);

			if (end == size)
				return NULL;

			pos = end + 1;
		} while (buf[end] == '|');

		pos = skip_entry(buf, size, pos);
	}
}

static char *termcap_open(const char *filename, int *size)
{
	struct stat st;
	char *buf;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return NULL;
	if (fstat(fd, &st) == -1) {
		close(fd);
		return NULL;
	}
	*size = st.st_size;

	buf = xmmap(fd, 0, *size);
	close(fd);
	return buf;
}

static void bool_cap(const char *cap)
{
	if (strncmp(cap, "ut", 2) == 0)
		term_cap.ut = 1;
}

static char *int_cap(char *cap)
{
	char *val = cap + 3;
	int ival = 0;

	while (*val >= '0' && *val <= '9') {
		ival *= 10;
		ival += *val++ - '0';
	}

	if (strncmp(cap, "Co", 2) == 0)
		term_cap.colors = ival;
	return val;
}

static char *unescape(char *src, int len)
{
	char *dst = xnew(char, len + 1);
	int s = 0, d = 0;

	while (s < len) {
		char ch = src[s++];

		if (s < len) {
			if (ch == '\\') {
				int num = 0, digits = 0;

				while (digits < 3) {
					ch = src[s];
					if (ch < '0' || ch > '9')
						break;
					digits++;
					s++;
					num *= 8;
					num += ch - '0';
				}

				if (digits == 0) {
					s++;
					switch (tolower(ch)) {
					case 'e':
						ch = '\033';
						break;
					case 'n':
						ch = '\n';
						break;
					case 'r':
						ch = '\r';
						break;
					case 't':
						ch = '\t';
						break;
					case 'b':
						ch = '\b';
						break;
					case 'f':
						ch = '\f';
						break;
					}
				} else {
					ch = num;
				}
			} else if (ch == '^') {
				/* fucking control characters */
				ch = src[s++];

				if (ch >= 'A' && ch <= '_') {
					/* A-_ -> 0x01-0x1f */
					ch = ch - 'A' + 1;
				} else {
					/* not my problem */
				}
			}
		}
		dst[d++] = ch;
	}
	dst[d] = 0;
	return dst;
}

static int process(const char *buf, int size, const char *term);

static char *str_cap(const char *buf, int size, char *cap)
{
	char *end, *val = cap + 3;
	int i, digits;

	end = val;
	while (1) {
		char ch = *end;
		
		if (ch == 0 || ch == ':')
			break;

		end++;
		if (ch != '\\')
			continue;

		ch = *end;
		if (ch == 0)
			break;

		digits = 0;
		while (ch >= '0' && ch <= '9') {
			digits++;
			end++;
			if (digits == 3)
				break;
			ch = *end;
		}
		if (digits == 0)
			end++;
	}

	if (strncmp(cap, "tc", 2) == 0) {
		char *term = xstrndup(val, end - val);

		process(buf, size, term);
		free(term);
		return end;
	}

	for (i = 0; str_cap_names[i]; i++) {
		if (strncmp(str_cap_names[i], cap, 2) == 0) {
			if (*str_cap_ptrs[i] == NULL)
				*str_cap_ptrs[i] = unescape(val, end - val);
			break;
		}
	}
	return end;
}

static int process(const char *buf, int size, const char *term)
{
	char *entry = termcap_find(buf, size, term);
	char *s;

	if (entry == NULL)
		return -1;

	s = entry;
	while (*s) {
		char *cap;
		char delim;

		if (*s == ':') {
			s++;
			continue;
		}

		if (s[1] == 0)
			break;

		cap = s;
		s += 2;
		delim = *s++;

		switch (delim) {
		case ':':
			bool_cap(cap);
			break;
		case '#':
			s = int_cap(cap);
			break;
		case '=':
			s = str_cap(buf, size, cap);
			break;
		case 0:
			bool_cap(cap);
		default:
			goto out;
		}
	}
out:
	return 0;
}

int termcap_get_caps(const char *filename, const char *term)
{
	char *buf;
	int size, rc;

	buf = termcap_open(filename, &size);
	if (buf == NULL)
		return -1;
	rc = process(buf, size, term);
	xmunmap(buf, size);
	return rc;
}
