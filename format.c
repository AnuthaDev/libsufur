//
// Created by thakur on 19/1/24.
//

#include "mkntfs.h"
#include "mkvfat.h"

int format_vfat(const char* part_node) {

    const char *argv[] = {"mkfs.fat", "-F", "32", part_node, (char*)0};
    const int argc = 4;
    return mkvfat(argc, argv);

}

int format_ntfs(const char* part_node) {
    const char *argv[] = {"mkfs.ntfs", "-Q", part_node, (char*)0};
    const int argc = 3;
    return mkntfs(argc, argv);

}

