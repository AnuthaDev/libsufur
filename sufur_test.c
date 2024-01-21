#include <stdio.h>

#include "libsufur.h"

int main()
{
	usb_drive *val = NULL;
	int size = enumerate_usb_mass_storage(&val);

	for (int i = 0;i<size;i++) {
		printf("\n%s %s\n", val[i].vendor_name, val[i].model_name);
		printf("%li\n", val[i].size);
		printf("%s\n", val[i].devnode);
	}

	//format_usb_drive(val);
	make_bootable(val, "/media/extradrive/Win10_22H2_English_x64v1.iso", 1 );

	//make_bootable(val, "/home/thakur/Downloads/debian-12.4.0-amd64-netinst.iso" );

	return 0;
}
