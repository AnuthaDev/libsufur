//
// Created by thakur on 9/2/24.
//

#ifndef LOG_H
#define LOG_H


void log_set_func(void(*ptr)(char*, void *obj), void *obj);
void log_i(const char *format, ...);


#endif //LOG_H
