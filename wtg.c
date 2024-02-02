//
// Created by thakur on 18/1/24.
//

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <libfdisk/libfdisk.h>
#include <wimlib.h>

#include "constants.h"
#include "libsufur.h"
#include "wtg.h"
#include "utils.h"

int wimapply_w2go(const usb_drive* drive) {
	int error = 0;
	struct fdisk_context* cxt = NULL;
	error = utils_get_fdisk_context(drive, &cxt, 1);
	if (error) {
		return error;
	}

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	printf("Applying WIM to Partition: %s\n", part_node);

	//fdisk_unref_table(tb); // This may lead to part_node becoming invalid, test
	fdisk_deassign_device(cxt, 1);

	WIMStruct* wim;
	wimlib_open_wim(ISO_MNT_PATH "/sources/install.wim", 0, &wim);
	// TODO: Add progress reporting
	wimlib_extract_image(wim, 1, part_node, WIMLIB_EXTRACT_FLAG_NTFS);
	wimlib_free(wim);

	printf("Done Applying WIM\n");

	return 0;
}
