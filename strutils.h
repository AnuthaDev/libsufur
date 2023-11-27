//
// Created by anurag on 27/11/23.
//
#ifndef STRUTILS_H
#define STRUTILS_H


#include <ctype.h>

#define from_hex(c)		(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10)

static size_t unhexmangle_to_buffer(const char *s, char *buf, size_t len)
{
	size_t sz = 0;
	const char *buf0 = buf;

	if (!s)
		return 0;

	while(*s && sz < len - 1) {
		if (*s == '\\' && sz + 3 < len - 1 && s[1] == 'x' &&
				isxdigit(s[2]) && isxdigit(s[3])) {

			*buf++ = from_hex(s[2]) << 4 | from_hex(s[3]);
			s += 4;
			sz += 4;
				} else {
					*buf++ = *s++;
					sz++;
				}
	}
	*buf = '\0';
	return buf - buf0 + 1;
}
#endif // STRUTILS_H
