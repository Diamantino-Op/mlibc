#include <nl_types.h>
#include <errno.h>

nl_catd catopen(const char *, int) {
	return (nl_catd)-1;  // always fail — no message catalog support
}

char *catgets(nl_catd, int, int, const char *message) {
	return (char *)message;  // return the default string
}

int catclose(nl_catd) {
	errno = EBADF;
	return -1;
}