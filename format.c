//
// Created by thakur on 19/1/24.
//

#include "mke2fs.h"
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


int format_ext2(const char* part_node) {
    const char *argv[] = {"mkfs.ext2", part_node};
    const int argc = 2;
    return mke2fs(argc, argv);
}

int format_ext3(const char* part_node) {
    const char *argv[] = {"mkfs.ext3", part_node};
    const int argc = 2;
    return mke2fs(argc, argv);
}

int format_ext4(const char* part_node) {
    const char *argv[] = {"mkfs.ext4", part_node};
    const int argc = 2;
    return mke2fs(argc, argv);
}