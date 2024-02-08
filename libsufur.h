// ।। जय श्री राम ।।
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

typedef struct iso_props {
	char* path;
	int isWindowsISO;
	int isWin2GO;
} iso_props;

int enumerate_usb_mass_storage(usb_drive**);

int get_iso_properties(iso_props *props);

int make_bootable(const usb_drive*, const iso_props* props);

#endif //LIBSUFUR_H
