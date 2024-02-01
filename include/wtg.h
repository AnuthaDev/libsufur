//
// Created by thakur on 18/1/24.
//

#ifndef WTG_H
#define WTG_H

#include "libsufur.h"

int createBootBCD(const char* path, char disk_bits[16], char esp_part_bits[16], char boot_part_bits[16]);

int createRecBCD(char disk_bits[16], char esp_part_bits[16]);

int wimapply_w2go(const usb_drive* drive);
// int wtg_format_drive(const usb_drive* drive);
#endif //WTG_H
