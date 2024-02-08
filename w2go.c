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
#include "w2go.h"

#include <sys/stat.h>

#include "format.h"
#include "partition.h"
#include "utils.h"

static int wimapply_w2go(const usb_drive* drive) {
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


static int prepare_w2go_drive(const usb_drive* drive, unsigned char uuidarray[3][16]) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 0);
	if (error) {
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	partition_w2go_create(cxt, uuidarray);

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* esp_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* esp_part_node = NULL;
	error = fdisk_partition_to_string(esp_pt, cxt, FDISK_FIELD_DEVICE, &esp_part_node);
	if (error) {
		printf("Error getting ESP partition device\n");
		return error;
	}
	printf("\nESP Device: %s\n", esp_part_node);


	struct fdisk_partition* ntfs_pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* ntfs_part_node = NULL;
	error = fdisk_partition_to_string(ntfs_pt, cxt, FDISK_FIELD_DEVICE, &ntfs_part_node);
	printf("\nNTFS Device: %s\n", ntfs_part_node);

	// This must come before format call, otherwise fdisk conflicts with mkfs
	fdisk_deassign_device(cxt, 0);

	// TODO: This strings may become invalid if we free structs above, make them robust
	format_vfat(esp_part_node);
	format_ntfs(ntfs_part_node);


	return error;
}


int make_windows_to_go(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);

	printf("Formatting USB drive\n");

	int error = unmount_all_partitions(drive);
	if (error) {
		printf("Error: %d\n", error);
		return error;
	}
	unsigned char uuidarray[3][16] = {0};
	prepare_w2go_drive(drive, uuidarray);

	wimapply_w2go(drive);


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

	struct fdisk_partition* ntfs_pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* ntfs_part_node = NULL;
	error = fdisk_partition_to_string(ntfs_pt, cxt, FDISK_FIELD_DEVICE, &ntfs_part_node);
	if (error) {
		printf("Error getting ntfs partition device\n");
		return error;
	}

	printf("\nMounting Partion: %s\n", ntfs_part_node);
	mount_partition(ntfs_part_node, WTG_NTFS_MNT_PATH);


	struct fdisk_partition* esp_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* esp_part_node = NULL;
	error = fdisk_partition_to_string(esp_pt, cxt, FDISK_FIELD_DEVICE, &esp_part_node);
	if (error) {
		printf("Error getting esp partition device\n");
		return error;
	}

	printf("\nMounting Partion: %s\n", esp_part_node);
	mount_partition(esp_part_node, WTG_ESP_MNT_PATH);

	fdisk_deassign_device(cxt, 1);


	mkdir(WTG_ESP_MNT_PATH "/EFI", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Boot", 0700);

	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/Resources", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/Fonts", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Recovery", 0700);

	// WARNING: Don't leave a trailing slash at end of path, it will cause wrong filename
	copy_dir_contents(WTG_NTFS_MNT_PATH "/Windows/Boot/EFI", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot");
	copy_dir_contents(WTG_NTFS_MNT_PATH "/Windows/Boot/Resources", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/Resources");
	copy_dir_contents(WTG_NTFS_MNT_PATH "/Windows/Boot/Fonts", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/Fonts");

	copy_file(WTG_NTFS_MNT_PATH "/Windows/Boot/EFI/bootmgfw.efi", WTG_ESP_MNT_PATH "/EFI/Boot/bootx64.efi");
	copy_file(WTG_NTFS_MNT_PATH "/Windows/System32/config/BCD-Template", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/BCD");

	createBootBCD(WTG_ESP_MNT_PATH"/EFI/Microsoft/Boot/BCD", uuidarray[0], uuidarray[1], uuidarray[2]);


	// Remaining:
	// copy sysprep unattend.xml
	// unmount all



	printf("Unmounting All\n");
	unmount_sufur();

	printf("Done\n");

	return 0;
}
