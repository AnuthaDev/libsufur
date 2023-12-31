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

#include "utils.h"
#include "strutils.h"

#define MSFT_BASIC_DATA_PART "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"

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

static int create_fat_filesystem(struct fdisk_context* cxt) {
	int error = 0;
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


	pid_t pid2;
	char *argv2[] = {"umount", part_node, (char*)0};

	char * const environ2[] = {NULL};
	int status2 = posix_spawn(&pid2, "/usr/bin/umount", NULL, NULL, argv2, environ2);
	if(status2 != 0) {
		fprintf(stderr, strerror(status2));
		return 1;
	}

	wait(NULL);


	pid_t pid;
	char *argv[] = {"mkfs.fat", part_node, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/sbin/mkfs.fat", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

static int create_default_partition(const usb_drive* drive, struct fdisk_context* cxt) {
	int error = 0;
	struct fdisk_partition *part = fdisk_new_partition ();
	fdisk_partition_partno_follow_default (part, 1 );
	fdisk_partition_start_follow_default(part, 1);
	fdisk_partition_end_follow_default(part, 1);

	fdisk_partition_set_name(part, "sufur_success");

	struct fdisk_label* lb = fdisk_get_label(cxt, NULL);
	struct fdisk_parttype *type = fdisk_label_get_parttype_from_string(lb, MSFT_BASIC_DATA_PART);

	fdisk_partition_set_type(part, type);

	error = fdisk_add_partition(cxt, part, NULL);

	if (error) {
		printf("Failed to format device\n");
		return error;
	}

	fdisk_write_disklabel(cxt);

	fdisk_reread_partition_table(cxt);

	create_fat_filesystem(cxt);
	return 0;
}

int format_usb_drive(const usb_drive* drive) {

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

	create_default_partition(drive, cxt);

	/* TODO: Remove the deassign call inside create_fat_partition when
	 * partitions can be created without mkfs. Currently done this
	 * way because otherwise mkfs fails with error "Device busy"
	 */
	//fdisk_deassign_device(cxt, 0);

	return 0;
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

int make_bootable(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);

	printf("Formatting USB drive\n");
	format_usb_drive(drive);

	if (!is_valid_ISO(isopath)) {
		return -1;
		printf("Invalid ISO file\n");
	}

	printf("Mounting ISO\n");
	mount_ISO(isopath);

	printf("Mounting Device\n");
	mount_device(drive);

	printf("Copying ISO files\n");
	copy_ISO_files();

	printf("Unmounting All");
	unmount_ALL();

	printf("Done\n");

	return 0;


}