//
// Created by thakur on 19/1/24.
//

#ifndef FORMAT_H
#define FORMAT_H

int format_vfat(const char* part_node);

int format_ntfs(const char* part_node);

int format_exfat(const char* part_node);

int format_ext2(const char* part_node);

int format_ext3(const char* part_node);

int format_ext4(const char* part_node);

#endif //FORMAT_H
