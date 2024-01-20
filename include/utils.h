//
// Created by anurag on 9/12/23.
//

#ifndef UTILS_H
#define UTILS_H

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>


#define ISO_MNT_PATH "/mnt/sufurISO"
#define USB_MNT_PATH "/mnt/sufurUSB"

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
        return 0;
    }

    return 1;
}

#endif //UTILS_H
