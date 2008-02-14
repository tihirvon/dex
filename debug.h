#ifndef DEBUG_H
#define DEBUG_H

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG <= 0
#define BUG(...) do { } while (0)
#define d_print(...) do { } while (0)
#else
#define BUG(...) bug(__FUNCTION__, __VA_ARGS__)
#define d_print(...) debug_print(__FUNCTION__, __VA_ARGS__)
#endif

#define __STR(a) #a
#define BUG_ON(a) \
	do { \
		if (unlikely(a)) \
			BUG("%s\n", __STR(a)); \
	} while (0)

void bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

#endif
