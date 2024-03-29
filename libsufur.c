//
// Created by anurag on 26/11/23.
//


#include "libsufur.h"

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <libfdisk/libfdisk.h>
#include <libudev.h>
#include <stdlib.h>
#include <string.h>
#include <libmount/libmount.h>
#include <sys/stat.h>

#include "utils.h"
#include "w2go.h"
#include "partition.h"
#include "format.h"
#include "log.h"
// #include "strutils.h"


// static size_t unhexmangle_to_buffer(const char *s, char *buf, size_t len);

static struct udev_device*
get_child(
	struct udev* udev, struct udev_device* parent, const char* subsystem) {
	struct udev_device* child = NULL;
	struct udev_enumerate* enumerate = udev_enumerate_new(udev);

	udev_enumerate_add_match_parent(enumerate, parent);
	udev_enumerate_add_match_subsystem(enumerate, subsystem);
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry* entry;

	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		child = udev_device_new_from_syspath(udev, path);
		break;
	}

	udev_enumerate_unref(enumerate);
	return child;
}

static int64_t get_drive_size(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;

	const int error = utils_get_fdisk_context(drive, &cxt, 1);
	if (error) {
		return 0; // 0 size denotes error while getting size
	}
	// struct fdisk_table* tb = fdisk_new_table();
	// error = fdisk_get_partitions(cxt, &tb);
	//
	// if (error) {
	// 	printf("Failed to get device partition data\n");
	// 	return error;
	// }

	const int64_t bytes = fdisk_get_nsectors(cxt) * fdisk_get_sector_size(cxt);
	// printf("Size: %lu\n", bytes);
	// printf("%lu partitions found\n", fdisk_table_get_nents(tb));

	// for (int i = 0; i < fdisk_table_get_nents(tb); i++) {
	// 	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, i);
	// 	char* data = NULL;
	// 	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &data);
	//
	// 	char* type = NULL;
	// 	fdisk_partition_to_string(pt, cxt, FDISK_FIELD_FSTYPE, &type);
	//
	// 	printf("\nType: %s\n", type);
	//
	// 	if(!data) {
	// 		printf("Cannot find information about partition\n");
	// 		return error;
	// 	}
	// 	printf("%s\n", data);
	//
	// 	struct udev_device* devc = udev_device_new_from_subsystem_sysname(udev, "block", basename(data));
	//
	// 	const char* date = udev_device_get_property_value(devc, "ID_FS_LABEL_ENC");
	//
	// 	if (!date)
	// 		printf("Partition label not found\n");
	// 	else {
	// 		char* name = strdup(date);
	//
	// 		unhexmangle_to_buffer(name, name, strlen(name) + 1);
	// 		printf("%s\n", name);
	// 	}
	// }

	fdisk_deassign_device(cxt, 1);

	return bytes;
}

int enumerate_usb_mass_storage(usb_drive** var) {
	int i = 0, count = 0;

	struct udev* udev = udev_new();
	struct udev_enumerate* enumerate = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enumerate, "scsi");
	udev_enumerate_add_match_property(enumerate, "DEVTYPE", "scsi_device");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry *entry, *iter;
	udev_list_entry_foreach(iter, devices) {
		count++;
	}

	*var = (usb_drive *) malloc(sizeof(usb_drive) * count);
	usb_drive* val = *var;


	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* scsi = udev_device_new_from_syspath(udev, path);

		struct udev_device* block = get_child(udev, scsi, "block");
		struct udev_device* scsi_disk = get_child(udev, scsi, "scsi_disk");

		const struct udev_device* usb
				= udev_device_get_parent_with_subsystem_devtype(
					scsi, "usb", "usb_device");

		if (block && scsi_disk && usb) {
			// printf("\n\nscsi: %s %s\n", udev_device_get_sysattr_value(scsi, "vendor"),
			//        udev_device_get_sysattr_value(scsi, "model"));
			const char* devnode = udev_device_get_devnode(block);

			val[i].devnode = strdup(devnode);
			val[i].size = 0;
			val[i].vendor_name = strdup(udev_device_get_sysattr_value(scsi, "vendor"));
			val[i].model_name = strdup(udev_device_get_sysattr_value(scsi, "model"));
			//printf("%s\n", devnode);
			val[i].size = get_drive_size(&val[i]);
			i++;
		}

		if (block)
			udev_device_unref(block);

		if (scsi_disk)
			udev_device_unref(scsi_disk);

		udev_device_unref(scsi);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);

	return i;
}

static int prepare_fst_drive(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 0);
	if (error) {
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	partition_fst_create(cxt);


	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* part_node = NULL;
	fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	printf("Formatting Device: %s\n", part_node);

	fdisk_deassign_device(cxt, 0);

	error = format_vfat(part_node);
	if(error) {
		printf("Error while formatting drive\n");
		return error;
	}

	return 0;
}

static int prepare_dual_fst_drive(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 0);
	if (error) {
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	partition_fst_ntfs_create(cxt);

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* ntfs_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* ntfs_part_node = NULL;
	error = fdisk_partition_to_string(ntfs_pt, cxt, FDISK_FIELD_DEVICE, &ntfs_part_node);
	if (error) {
		printf("Error while getting NTFS partition device\n");
		return error;
	}
	printf("\nDevice: %s\n", ntfs_part_node);


	struct fdisk_partition* vfat_pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* vfat_part_node = NULL;
	error = fdisk_partition_to_string(vfat_pt, cxt, FDISK_FIELD_DEVICE, &vfat_part_node);
	printf("\nDevice: %s\n", vfat_part_node);

	// This must come before format call, otherwise fdisk conflicts with mkfs
	fdisk_deassign_device(cxt, 0);

	// TODO: These strings may become invalid if we free structs above, make them robust
	format_ntfs(ntfs_part_node);
	format_vfat(vfat_part_node);


	return error;
}


static int mount_device(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 1);

	if (error) {
		return error;
	}

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	if (error) {
		printf("Error getting USB 0th partition device\n");
		return error;
	}
	printf("\nMounting Partition: %s\n", part_node);

	fdisk_deassign_device(cxt, 1);

	mkdir(USB_MNT_PATH, 0700);


	struct libmnt_context* mntcxt = mnt_new_context();

	mnt_context_set_source(mntcxt, part_node);
	mnt_context_set_target(mntcxt, USB_MNT_PATH);

	error = mnt_context_mount(mntcxt);
	if (error) {
		printf("Error while mounting USB. Aborting!\n");
		return error;
	}

	mnt_free_context(mntcxt);
	return 0;
}

static int copy_ISO_files() {
	return copy_dir_contents(ISO_MNT_PATH, USB_MNT_PATH);
}

static int flash_uefi_ntfs_img(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 1);
	if (error) {
		return error;
	}

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	const int vfat_part_no = 1;
	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, vfat_part_no);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	if (error) {
		printf("Error getting vfat partition device\n");
		return error;
	}
	printf("\nFlashing Partition: %s\n", part_node);

	copy_file("uefi-ntfs.img", part_node);

	return 0;
}

static int make_windows_bootable(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);
	int error = 0;

	printf("Unmounting Drive partitions\n");
	error = unmount_all_partitions(drive);
	if (error) {
		printf("Error: %d\n", error);
		return error;
	}

	printf("Formatting USB drive\n");
	prepare_dual_fst_drive(drive);

	printf("Mounting ISO\n");
	error = mount_ISO(isopath);
	if(error) {
		printf("Error while mounting ISO. Aborting!\n");
		return error;
	}

	printf("Mounting Device\n");
	mount_device(drive);

	printf("Copying ISO files\n");
	copy_ISO_files();

	printf("Unmounting All");
	unmount_sufur();

	printf("Flashing UEFI:NTFS");
	flash_uefi_ntfs_img(drive);
	printf("Done\n");

	return 0;
}


int make_bootable(const usb_drive* drive, const iso_props* props) {
	setbuf(stdout, NULL);
	int error = 0;
	const char* isopath = props->path;

	if (!is_valid_ISO(isopath)) {
		printf("Invalid ISO file\n");
		return -1;
	}

	if (props->isWindowsISO && props->isWin2GO) {
		printf("Making Windows to Go drive\n");
		return make_windows_to_go(drive, isopath);
	}
	if (props->isWindowsISO) {
		printf("Detected Windows ISO! Will use UEFI:NTFS\n");
		return make_windows_bootable(drive, isopath);
	}


	log_i("Did not detect Windows ISO!\n");
	log_i("Proceeding with simple File System Transposition\n");

	error = unmount_all_partitions(drive);
	if (error) goto Exit;

	printf("Formatting USB drive\n");
	error = prepare_fst_drive(drive);
	if (error) goto Exit;

	printf("Mounting ISO\n");
	error = mount_ISO(isopath);
	if (error) goto Exit;

	printf("Mounting Drive\n");
	error = mount_device(drive);
	if (error) goto Exit;

	printf("Copying ISO files\n");
	copy_ISO_files();

Exit:
	printf("Unmounting and exiting\n");
	unmount_sufur();
	printf("Done\n");

	return error;
}
