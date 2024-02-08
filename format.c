//
// Created by thakur on 19/1/24.
//

#include "mkntfs.h"
#include "mkvfat.h"
#include "mke2fs.h"
#include "mkexfat.h"

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

int format_exfat(const char* part_node) {
    const char *argv[] = {"mkfs.exfat", part_node, (char*)0};
    const int argc = 2;
    return mkexfat(argc, argv);
}

int format_ext2(const char* part_node) {
    return mke2fs(part_node, 2);
}

int format_ext3(const char* part_node) {
    return mke2fs(part_node, 3);
}

int format_ext4(const char* part_node) {
    return mke2fs(part_node, 4);
}