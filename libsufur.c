//
// Created by anurag on 26/11/23.
//


#include "libsufur.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libfdisk/libfdisk.h>
#include <libudev.h>
#include <stdlib.h>
#include <spawn.h>
#include <wait.h>
#include <libmount/libmount.h>

#include "utils.h"
#include "wtg.h"
#include "partition.h"
#include "format.h"
#include "strutils.h"


#define ISO_MNT_PATH "/mnt/sufurISO"
#define USB_MNT_PATH "/mnt/sufurUSB"
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

static int64_t get_drive_size(const char* device) {
	int error = 0;
	struct fdisk_context* cxt = fdisk_new_context();

	if (!cxt)
		return error = -1;

	error = faccessat(-1, device, F_OK, AT_EACCESS);

	if (error) {
		printf("Device does not exist\n");
		return error;
	}

	error = faccessat(-1, device, R_OK, AT_EACCESS);

	if (error) {
		printf("Please run the program as root\n");
		return error;
	}

	error = fdisk_assign_device(cxt, device, 1);

	if (error) {
		printf("Failed to assign fdisk device\n");
		return error;
	}

	// struct fdisk_table* tb = fdisk_new_table();
	// error = fdisk_get_partitions(cxt, &tb);
	//
	// if (error) {
	// 	printf("Failed to get device partition data\n");
	// 	return error;
	// }

	int64_t bytes = fdisk_get_nsectors(cxt) * fdisk_get_sector_size(cxt);
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

	fdisk_deassign_device(cxt, 0);

	return bytes;
}

int enumerate_usb_mass_storage(usb_drive **var) {

	int i = 0, count = 0;

	struct udev* udev = udev_new();
	struct udev_enumerate* enumerate = udev_enumerate_new(udev);

	udev_enumerate_add_match_subsystem(enumerate, "scsi");
	udev_enumerate_add_match_property(enumerate, "DEVTYPE", "scsi_device");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry* entry, *iter;
	udev_list_entry_foreach(iter, devices) {
		count++;
	}

	*var = (usb_drive*)malloc(sizeof(usb_drive)*count);
	usb_drive *val = *var;


	udev_list_entry_foreach(entry, devices) {
		const char* path = udev_list_entry_get_name(entry);
		struct udev_device* scsi = udev_device_new_from_syspath(udev, path);

		struct udev_device* block = get_child(udev, scsi, "block");
		struct udev_device* scsi_disk = get_child(udev, scsi, "scsi_disk");

		struct udev_device* usb
				= udev_device_get_parent_with_subsystem_devtype(
					scsi, "usb", "usb_device");

		if (block && scsi_disk && usb) {
			// printf("\n\nscsi: %s %s\n", udev_device_get_sysattr_value(scsi, "vendor"),
			//        udev_device_get_sysattr_value(scsi, "model"));
			const char* devnode = udev_device_get_devnode(block);

			val[i].devnode = strdup(devnode);
			val[i].size = get_drive_size(devnode);
			val[i].vendor_name = strdup(udev_device_get_sysattr_value(scsi, "vendor"));
			val[i].model_name = strdup(udev_device_get_sysattr_value(scsi, "model"));
			//printf("%s\n", devnode);
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


static int create_windows_usb_partitions(const usb_drive* drive, struct fdisk_context* cxt) {
	int error = 0;


	struct fdisk_labelitem *item = fdisk_new_labelitem();
	fdisk_get_disklabel_item(cxt, GPT_LABELITEM_ID, item);
	const char * diskuuid = NULL;
	fdisk_labelitem_get_data_string(item,  &diskuuid);
	printf("Disk UUID: %s\n", diskuuid);


	unsigned char disk_uuid_bytes[16] = {0};

	uuid_swizzle(diskuuid, disk_uuid_bytes);
	for (int i = 0;i<16;i++) {
		printf("%02x ", disk_uuid_bytes[i]);
	}
	printf("\n");


	fdisk_unref_labelitem(item);




	struct fdisk_partition *fat32_part = fdisk_new_partition();
	fdisk_partition_set_partno(fat32_part, 1);
	// 2048: Size of partition
	// 2048: Extra space so libfdisk can align at 1MiB boundary
	fdisk_partition_set_start(fat32_part, fdisk_get_nsectors(cxt)-2048-2048);
	fdisk_partition_set_size(fat32_part, 2048);
	fdisk_partition_end_follow_default(fat32_part, 1);

	fdisk_partition_set_name(fat32_part, "sufur_fat32");

	struct fdisk_label* lbfat = fdisk_get_label(cxt, NULL);
	struct fdisk_parttype *typefat = fdisk_label_get_parttype_from_string(lbfat, EFI_SYSTEM_PART);

	fdisk_partition_set_type(fat32_part, typefat);

	error = fdisk_add_partition(cxt, fat32_part, NULL);

	fdisk_get_partition(cxt, 1, &fat32_part);
	const char * espuuid = fdisk_partition_get_uuid (fat32_part);
	printf("ESP UUID: %s\n", espuuid);

	unsigned char esp_uuid_bytes[16] = {0};

	uuid_swizzle(espuuid, esp_uuid_bytes);
	for (int i = 0;i<16;i++) {
		printf("%02x ", esp_uuid_bytes[i]);
	}
	printf("\n");



	struct fdisk_partition *ntfs_part = fdisk_new_partition();
	//fdisk_partition_partno_follow_default (ntfs_part, 1 );
	fdisk_partition_set_partno(ntfs_part, 0);
	fdisk_partition_start_follow_default(ntfs_part, 1);
	fdisk_partition_end_follow_default(ntfs_part, 1);

	fdisk_partition_set_name(ntfs_part, "sufur_ntfs");

	struct fdisk_label* lb = fdisk_get_label(cxt, NULL);
	struct fdisk_parttype *type = fdisk_label_get_parttype_from_string(lb, MSFT_BASIC_DATA_PART);

	fdisk_partition_set_type(ntfs_part, type);

	error = fdisk_add_partition(cxt, ntfs_part, NULL);

	fdisk_get_partition(cxt, 0, &ntfs_part);
	const char * windrvuuid = fdisk_partition_get_uuid (ntfs_part);
	printf("WinDrive UUID: %s\n", windrvuuid);

	unsigned char windrv_uuid_bytes[16] = {0};

	uuid_swizzle(windrvuuid, windrv_uuid_bytes);
	for (int i = 0;i<16;i++) {
		printf("%02x ", windrv_uuid_bytes[i]);
	}
	printf("\n");

	fdisk_write_disklabel(cxt);

	createBootBCD(disk_uuid_bytes, esp_uuid_bytes, windrv_uuid_bytes);
	return error;
}

static int prepare_fst_drive(const usb_drive* drive) {

	const char* device = drive->devnode;
	int error = 0;
	struct fdisk_context* cxt = fdisk_new_context();

	if (!cxt)
		return error = -1;

	error = faccessat(-1, device, F_OK, AT_EACCESS);

	if (error) {
		printf("Device does not exist\n");
		return error;
	}

	error = faccessat(-1, device, R_OK, AT_EACCESS);

	if (error) {
		printf("Please run the program as root\n");
		return error;
	}

	error = fdisk_assign_device(cxt, device, 0);

	if (error) {
		printf("Failed to assign fdisk device\n");
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	create_fst_partition(cxt);
	//create_windows_to_go_partitions(cxt);
	//create_dual_fst_partitions(cxt);

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	printf("\nDevice: %s\n", part_node);

	fdisk_deassign_device(cxt, 0);

	format_vfat(part_node);

	return error;
}

static int prepare_dual_fst_drive(const usb_drive* drive) {

	const char* device = drive->devnode;
	int error = 0;
	struct fdisk_context* cxt = fdisk_new_context();

	if (!cxt)
		return error = -1;

	error = faccessat(-1, device, F_OK, AT_EACCESS);

	if (error) {
		printf("Device does not exist\n");
		return error;
	}

	error = faccessat(-1, device, R_OK, AT_EACCESS);

	if (error) {
		printf("Please run the program as root\n");
		return error;
	}

	error = fdisk_assign_device(cxt, device, 0);

	if (error) {
		printf("Failed to assign fdisk device\n");
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	create_dual_fst_partitions(cxt);

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* ntfs_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* ntfs_part_node = NULL;
	error = fdisk_partition_to_string(ntfs_pt, cxt, FDISK_FIELD_DEVICE, &ntfs_part_node);
	printf("\nDevice: %s\n", ntfs_part_node);


	struct fdisk_partition* vfat_pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* vfat_part_node = NULL;
	error = fdisk_partition_to_string(vfat_pt, cxt, FDISK_FIELD_DEVICE, &vfat_part_node);
	printf("\nDevice: %s\n", vfat_part_node);

	// This must come before format call, otherwise fdisk conflicts with mkfs
	fdisk_deassign_device(cxt, 0);

	// TODO: This strings may become invalid if we free structs above, make them robust
	format_ntfs(ntfs_part_node);
	format_vfat(vfat_part_node);


	return error;
}

static int prepare_windows_to_go_drive(const usb_drive* drive) {

	const char* device = drive->devnode;
	int error = 0;
	struct fdisk_context* cxt = fdisk_new_context();

	if (!cxt)
		return error = -1;

	error = faccessat(-1, device, F_OK, AT_EACCESS);

	if (error) {
		printf("Device does not exist\n");
		return error;
	}

	error = faccessat(-1, device, R_OK, AT_EACCESS);

	if (error) {
		printf("Please run the program as root\n");
		return error;
	}

	error = fdisk_assign_device(cxt, device, 0);

	if (error) {
		printf("Failed to assign fdisk device\n");
		return error;
	}

	fdisk_delete_all_partitions(cxt);

	create_gpt_label(cxt);
	create_windows_to_go_partitions(cxt);

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Failed to get device partition data\n");
		return error;
	}

	struct fdisk_partition* esp_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* esp_part_node = NULL;
	error = fdisk_partition_to_string(esp_pt, cxt, FDISK_FIELD_DEVICE, &esp_part_node);
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



static int mount_ISO(const char* isopath) {
	mkdir(ISO_MNT_PATH, 0700);

	pid_t pid;
	char *argv[] = {"mount", "-o", "loop", isopath, ISO_MNT_PATH, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/bin/mount", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

static int mount_device(const usb_drive* drive) {
	const char* device = drive->devnode;
	int error = 0;
	struct fdisk_context* cxt = fdisk_new_context();

	if (!cxt)
		return error = -1;

	error = faccessat(-1, device, F_OK, AT_EACCESS);

	if (error) {
		printf("Device does not exist\n");
		return error;
	}

	error = faccessat(-1, device, R_OK, AT_EACCESS);

	if (error) {
		printf("Please run the program as root\n");
		return error;
	}

	error = fdisk_assign_device(cxt, device, 1);

	if (error) {
		printf("Failed to assign fdisk device\n");
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
	printf("\nMounting Partion: %s\n", part_node);





	mkdir(USB_MNT_PATH, 0700);

	pid_t pid;
	char *argv[] = {"mount", part_node, USB_MNT_PATH, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/bin/mount", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

static int copy_ISO_files() {
	pid_t pid;
	char *argv[] = {"cp", "-r", ISO_MNT_PATH "/.", USB_MNT_PATH, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

static int unmount_ALL() {

	pid_t pid;
	char *argv[] = {"umount", ISO_MNT_PATH, USB_MNT_PATH, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/bin/umount", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

int lock_drive(const usb_drive* drive) {
	mnt_new_context();
	// pid_t pid2;
	// char *argv2[] = {"umount", part_node, (char*)0};
	//
	// char * const environ2[] = {NULL};
	// int status2 = posix_spawn(&pid2, "/usr/bin/umount", NULL, NULL, argv2, environ2);
	// if(status2 != 0) {
	// 	fprintf(stderr, strerror(status2));
	// 	return 1;
	// }
	//
	// wait(NULL);

	return 0;
}
int make_bootable(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);

	printf("Formatting USB drive\n");
	// TODO: Unmount logic here

	lock_drive(drive);
	prepare_windows_to_go_drive(drive);

	if (!is_valid_ISO(isopath)) {
		return -1;
		printf("Invalid ISO file\n");
	}

	printf("Mounting ISO\n");
	//mount_ISO(isopath);

	printf("Mounting Device\n");
	//mount_device(drive);

	printf("Copying ISO files\n");
	//copy_ISO_files();

	printf("Unmounting All");
	//unmount_ALL();

	printf("Done\n");

	return 0;

}

int make_windows_to_go(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);

	printf("Formatting USB drive\n");
	prepare_fst_drive(drive);

	if (!is_valid_ISO(isopath)) {
		return -1;
		printf("Invalid ISO file\n");
	}

	printf("Mounting ISO\n");
	//mount_ISO(isopath);

	printf("Mounting Device\n");
	//mount_device(drive);

	printf("Copying ISO files\n");
	//copy_ISO_files();

	printf("Unmounting All");
	//unmount_ALL();

	printf("Done\n");

	return 0;

}