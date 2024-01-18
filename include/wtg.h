//
// Created by thakur on 18/1/24.
//

#ifndef WTG_H
#define WTG_H



int createBootBCD(char disk_bits[16], char esp_part_bits[16], char boot_part_bits[16]);

int createRecBCD(char disk_bits[16], char esp_part_bits[16]);

// int wtg_format_drive(const usb_drive* drive);
#endif //WTG_H
