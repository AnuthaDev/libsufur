#include <stdio.h>

#include "libsufur.h"

int main() {
	usb_drive* val = NULL;
	int size = enumerate_usb_mass_storage(&val);

	for (int i = 0; i < size; i++) {
		printf("\n%s %s\n", val[i].vendor_name, val[i].model_name);
		printf("%li\n", val[i].size);
		printf("%s\n", val[i].devnode);
	}

	iso_props props = {};
	props.path = "/home/thakur/Downloads/debian-12.4.0-amd64-netinst.iso";
	//props.path = "/media/extradrive/Win10_22H2_English_x64v1.iso";
	if(get_iso_properties(&props)) {
		printf("Error while getting props\n");
		return -1;
	}

	//props.isWin2GO = 1;
	make_bootable(val, &props);

	return 0;
}
