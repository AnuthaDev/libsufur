//
// Created by thakur on 19/1/24.
//

#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "mkntfs.h"

int format_vfat(const char* part_node) {

    pid_t pid;
    const char *argv[] = {"mkfs.fat", "-F", "32", part_node, (char*)0};

    char * const environ[] = {NULL};
    const int status = posix_spawn(&pid, "/usr/sbin/mkfs.fat", NULL, NULL, argv, environ);
    if(status != 0) {
        fprintf(stderr, strerror(status));
        return 1;
    }

    wait(NULL);

    return 0;
}

int format_ntfs(const char* part_node) {
    const char *argv[] = {"mkfs.ntfs", "-Q", part_node, (char*)0};
    const int argc = 3;
    mkntfs(argc, argv);

    return 0;
}

