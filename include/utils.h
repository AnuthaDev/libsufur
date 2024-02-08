//
// Created by anurag on 9/12/23.
//

#ifndef UTILS_H
#define UTILS_H

#include "libsufur.h"

int is_valid_ISO(const char* path);

void uuid_swizzle(const char* uuid, unsigned char destarr[]);

int isWindowsISO();

int mount_partition(const char* partition, const char* target);

int copy_file(const char* from, const char* to);

int copy_dir_contents(char* path, char* to);

int utils_get_fdisk_context(const usb_drive* drive, struct fdisk_context** cxt, const int readonly);

int unmount_sufur();

int unmount_all_partitions(const usb_drive* drive);
#endif //UTILS_H
