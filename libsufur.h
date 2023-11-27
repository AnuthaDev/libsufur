//
// Created by anurag on 26/11/23.
//

#ifndef LIBSUFUR_H
#define LIBSUFUR_H

#include <stdint.h>

int enumerate_usb_mass_storage();

struct usb_drive {
	char* devpath;
	uint64_t size;
	char* vendor_name;
	char* model_name;
};


#endif //LIBSUFUR_H
