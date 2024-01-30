//
// Created by anurag on 9/12/23.
//

#ifndef UTILS_H
#define UTILS_H
#define __USE_XOPEN_EXTENDED 500

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdlib.h>
#include <libfdisk/libfdisk.h>
#include <sys/wait.h>
#include <ftw.h>
#include <dirent.h>
#include <stdbool.h>

#include "constants.h"

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

/**
 * The job of this function is to convert this:
 * A6B212E7-FB5D-C24B-B464-251C90953F69
 * into this:
 * unsigned char bytes[16] = {e7, 12, b2, a6, 5d, fb, 4b, c2, b4, 64, 25, 1c, 90, 95, 3f, 69};
 *
 * This is some weird-endian order used by Microsoft in BCD
 *
 * If you can understand what this is, please teach me :-)
 */
static void uuid_swizzle(const char* uuid, char destarr[]) {
    char swizzler[3] = {0};

    // First Part
    swizzler[0] = uuid[6];
    swizzler[1] = uuid[7];
    destarr[0] = (unsigned) strtol(swizzler, NULL, 16);

    swizzler[0] = uuid[4];
    swizzler[1] = uuid[5];
    destarr[1] = (unsigned) strtol(swizzler, NULL, 16);

    swizzler[0] = uuid[2];
    swizzler[1] = uuid[3];
    destarr[2] = (unsigned) strtol(swizzler, NULL, 16);

    swizzler[0] = uuid[0];
    swizzler[1] = uuid[1];
    destarr[3] = (unsigned) strtol(swizzler, NULL, 16);

    // Second Part
    swizzler[0] = uuid[11];
    swizzler[1] = uuid[12];
    destarr[4] = (unsigned) strtol(swizzler, NULL, 16);

    swizzler[0] = uuid[9];
    swizzler[1] = uuid[10];
    destarr[5] = (unsigned) strtol(swizzler, NULL, 16);

    // Third Part
    swizzler[0] = uuid[16];
    swizzler[1] = uuid[17];
    destarr[6] = (unsigned) strtol(swizzler, NULL, 16);

    swizzler[0] = uuid[14];
    swizzler[1] = uuid[15];
    destarr[7] = (unsigned) strtol(swizzler, NULL, 16);

    // Fourth Part
    char mover[3] = {0};

    mover[0] = uuid[19];
    mover[1] = uuid[20];
    destarr[8] = (unsigned) strtol(mover, NULL, 16);

    mover[0] = uuid[21];
    mover[1] = uuid[22];
    destarr[9] = (unsigned) strtol(mover, NULL, 16);

    // Fifth Part
    for(int i = 24, j=10; i<=34;i+=2,j++) {
        mover[0] = uuid[i];
        mover[1] = uuid[i+1];
        destarr[j] = (unsigned) strtol(mover, NULL, 16);
    }
}

// This is a laughably naive jugaad, but it works for now
// TODO: Fix this
static int isWindowsISO() {
    const int error = faccessat(-1, ISO_MNT_PATH"/sources/install.wim", F_OK, AT_EACCESS);

    if (error) {
        printf("Not Windows ISO\n");
        return error;
    }

    return 1;
}


static int mount_partition(const char* partition, const char* target) {
    int error = 0;

    // TODO: Missing a call to fdisk_deassign_device


    mkdir(target, 0700);

    pid_t pid;
    const char *argv[] = {"mount", partition, target, (char*)0};

    char * const environ[] = {NULL};
    int status = posix_spawn(&pid, "/usr/bin/mount", NULL, NULL, argv, environ);
    if(status != 0) {
        fprintf(stderr, strerror(status));
        return 1;
    }

    wait(NULL);

    return 0;
}

static char *copy_to_path;
static char *copy_from_path;
static bool copy_busy = false;

static bool copy_file(const char* from, char* to) {
    FILE *ff = fopen(from, "r");

    if(!ff) {
        perror("Can't open source file");
        return false;
    }

    FILE *ft = fopen(to, "w");

    if(!ft) {
        perror("Can't open dest file");
        fclose(ff);
        return false;
    }

    char buffer[4 * MIB];
    size_t r = 0;

    while(r = fread(buffer, 1, sizeof(buffer), ff)){
        fwrite(buffer, 1, r, ft);
    }

    fclose(ff);
    fclose(ft);

    return true;
}
static int cp_callback(const char* fpath, const struct stat *sb, int type_flag, struct FTW *ftwbuf) {
    printf("%s\n", fpath );
    char to_location[1024];
    sprintf(to_location, "%s/%s", copy_to_path, fpath + strlen(copy_from_path) + 1);

    if(type_flag & FTW_D) {
        // is a directory...
        if(ftwbuf->level == 0) {
            // level == 0 means we are in the base directory, don't create that...
            return 0;
        }

        if(mkdir(to_location, 0775)) {
            perror("Failed to create directory\n");
            return -1;
        }
    }else if (!copy_file(fpath, to_location)) {
        return -1;
    }

    return 0;
}
// copy path/* into to/
static int copy_dir_contents(char *path, char *to) {
    if(copy_busy) {
        fprintf(stderr, "copy busy!\n");
        return false;
    }
    copy_busy = true;
    copy_to_path = to;
    copy_from_path = path;

    int ret = nftw(path, cp_callback, 64, FTW_PHYS);

    copy_busy = false;

    return ret == 0;
}
#endif //UTILS_H
