//
// Created by anurag on 9/12/23.
//

#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <sys/stat.h>

static const char* get_filename_ext(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (!dot || dot == filename) return "";
    return dot + 1;
}

static int is_regular_file(const char* path) {
    struct stat path_stat;
    return !stat(path, &path_stat) && S_ISREG(path_stat.st_mode);
}

static int is_valid_ISO(const char* path) {
    return is_regular_file(path) && !strcasecmp("iso", get_filename_ext(path));
}


#endif //UTILS_H
