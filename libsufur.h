//
// Created by anurag on 26/11/23.
//

#ifndef LIBSUFUR_H
#define LIBSUFUR_H

#include <stdint.h>

typedef struct usb_drive {
	char* devnode;
	int64_t size;
	char* vendor_name;
	char* model_name;
} usb_drive;

int enumerate_usb_mass_storage(usb_drive**);


int make_bootable(const usb_drive*, const char*, int isWin2GO);

#endif //LIBSUFUR_H
