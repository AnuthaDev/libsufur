//
// Created by thakur on 18/1/24.
//

#ifndef WTG_H
#define WTG_H

#include "libsufur.h"

int createBootBCD(const char* path, unsigned char disk_bits[16], unsigned char esp_part_bits[16],
                  unsigned char boot_part_bits[16]);

int createRecBCD(unsigned char disk_bits[16], unsigned char esp_part_bits[16]);

int make_windows_to_go(const usb_drive* drive, const char* isopath);

// int wtg_format_drive(const usb_drive* drive);
#endif //WTG_H
