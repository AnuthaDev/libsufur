//
// Created by thakur on 9/2/24.
//

#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
static int has_func = 0;
void* data = NULL;

void (*func_ptr)(char*, void*);

void log_set_func(void (*ptr)(char*, void* obj), void* obj) {
	has_func = 1;
	func_ptr = ptr;
	data = obj;
}

void log_i(const char* format, ...) {
	char* buf = (char *) malloc(4096);
	va_list argList;
	va_start(argList, format);
	vsnprintf(buf, 4096, format, argList);
	va_end(argList);

	if (has_func) {
		func_ptr(buf, data);
	} else {
		printf("%s\n", buf);
	}
	free(buf);
}
