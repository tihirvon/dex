#include "term.h"
#include "common.h"

static const char str_cap_names[NR_STR_CAPS * 2] =
	"ac"
	"ae"
	"as"
	"ce"
	"ke"
	"ks"
	"te"
	"ti"
	"ve"
	"vi";

static const char key_cap_names[(NR_SPECIAL_KEYS + 2) * 2] =
	"kI"
	"kD"
	"kh"
	"@7"
	"kP"
	"kN"
	"kl"
	"kr"
	"ku"
	"kd"
	"k1"
	"k2"
	"k3"
	"k4"
	"k5"
	"k6"
	"k7"
	"k8"
	"k9"
	"k;"
	"F1"
	"F2"
	"#4" // MOD_SHIFT | KEY_LEFT
	"%i"; // MOD_SHIFT | KEY_RIGHT

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

static void bool_cap(const char *cap)
{
	if (str_has_prefix(cap, "ut"))
		term_cap.ut = true;
}

static char *int_cap(char *cap)
{
	char *val = cap + 3;
	int ival = 0;

	while (isdigit(*val)) {
		ival *= 10;
		ival += *val++ - '0';
	}

	if (str_has_prefix(cap, "Co"))
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
					if (!isdigit(ch))
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
	struct term_keymap *km;
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
		while (isdigit(ch)) {
			digits++;
			end++;
			if (digits == 3)
				break;
			ch = *end;
		}
		if (digits == 0)
			end++;
	}

	if (str_has_prefix(cap, "tc")) {
		char *term = xstrslice(val, 0, end - val);

		process(buf, size, term);
		free(term);
		return end;
	}

	for (i = 0; i < NR_STR_CAPS; i++) {
		if (memcmp(str_cap_names + i * 2, cap, 2) == 0) {
			if (term_cap.strings[i] == NULL)
				term_cap.strings[i] = unescape(val, end - val);
			return end;
		}
	}
	for (i = 0; i < NR_SPECIAL_KEYS + 2; i++) {
		if (memcmp(key_cap_names + i * 2, cap, 2) == 0) {
			km = &term_cap.keymap[i];
			if (km->code == NULL) {
				km->code = unescape(val, end - val);
			}
			return end;
		}
	}
	return end;
}

static void init_keymap(void)
{
	struct term_keymap *km;
	int i;

	term_cap.keymap_size = NR_SPECIAL_KEYS + 2;
	term_cap.keymap = xnew(struct term_keymap, term_cap.keymap_size);
	for (i = 0; i < NR_SPECIAL_KEYS; i++) {
		km = &term_cap.keymap[i];
		km->key = KEY_SPECIAL_MIN + i;
		km->code = NULL;
	}
	km = &term_cap.keymap[i];
	km->key = MOD_SHIFT | KEY_LEFT;
	km->code = NULL;
	i++;
	km = &term_cap.keymap[i];
	km->key = MOD_SHIFT | KEY_RIGHT;
	km->code = NULL;
}

static int process(const char *buf, int size, const char *term)
{
	char *entry = termcap_find(buf, size, term);
	char *s;

	if (entry == NULL)
		return -1;

	init_keymap();
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
	long size;
	int rc;

	size = read_file(filename, &buf);
	if (size < 0)
		return -1;
	rc = process(buf, size, term);
	free(buf);
	return rc;
}
