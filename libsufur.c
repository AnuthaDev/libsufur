//
// Created by anurag on 26/11/23.
//


#include "libsufur.h"

#include <ctype.h>
#include <errno.h>
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

	// TODO: These strings may become invalid if we free structs above, make them robust
	format_ntfs(ntfs_part_node);
	format_vfat(vfat_part_node);


	return error;
}

static int prepare_windows_to_go_drive(const usb_drive* drive, unsigned char uuidarray[3][16]) {

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
	create_windows_to_go_partitions(cxt, uuidarray);

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

	// TODO: Missing a call to fdisk_deassign_device


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
	// pid_t pid;
	// // TODO: why should it be "/." and not "/" or "/*". I forgot ¯\_(ツ)_/¯
	// char *argv[] = {"cp", "-r", ISO_MNT_PATH "/.", USB_MNT_PATH, (char*)0};
	//
	// char * const environ[] = {NULL};
	// int status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv, environ);
	// if(status != 0) {
	// 	fprintf(stderr, strerror(status));
	// 	return 1;
	// }
	//
	// wait(NULL);
	copy_dir_contents(ISO_MNT_PATH, USB_MNT_PATH);
	return 0;
}

static int flash_uefi_ntfs_img(const usb_drive *drive) {
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

	const int vfat_part_no = 1;
	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, vfat_part_no);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	printf("\nFlashing Partion: %s\n", part_node);


	pid_t pid;
	char *argv[] = {"cp", "uefi-ntfs.img", part_node , (char*)0};

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

static int lock_drive(const usb_drive* drive) {
	struct fdisk_context* cxt = fdisk_new_context();
	fdisk_assign_device(cxt, drive->devnode, 1);

	struct fdisk_table* tb = fdisk_new_table();
	fdisk_get_partitions(cxt, &tb);

	int nums = fdisk_table_get_nents(tb);
	for(int i = 0;i<nums;i++) {
		struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, i);
		char* part_node = NULL;
		fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
		struct libmnt_context *mntcxt = mnt_new_context();

		mnt_context_set_target(mntcxt, part_node);

		printf("Device: %s\n", part_node);

		int error = mnt_context_umount(mntcxt);
		if(error) {
			printf("Error while unmounting partitions. Aborting!\n");
			fdisk_deassign_device(cxt, 1);
			return error;
		}

		// This needs investigation
		// int statuserr = mnt_context_get_status(mntcxt);
		// printf("Status: %d\n", statuserr);

		mnt_free_context(mntcxt);
	}

	fdisk_deassign_device(cxt, 1);
	return 0;
}

int make_windows_bootable(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);


	printf("Unmounting Drive partitions\n");
	int error = lock_drive(drive);
	if (error) {
		printf("Error: %d\n", error);
		return error;
	}

	printf("Formatting USB drive\n");
	prepare_dual_fst_drive(drive);

	printf("Mounting Device\n");
	mount_device(drive);

	printf("Copying ISO files\n");
	copy_ISO_files();

	printf("Unmounting All");
	unmount_ALL();

	printf("Flashing UEFI:NTFS");
	flash_uefi_ntfs_img(drive);
	printf("Done\n");

	return 0;

}

int wimapply_w2go(const usb_drive* drive) {
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

	struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* part_node = NULL;
	error = fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
	printf("Applying WIM to Partition: %s\n", part_node);


//wimapply WIMFILE [IMAGE] TARGET [OPTION...]
	pid_t pid;
	char *argv[] = {"wimapply", ISO_MNT_PATH"/sources/install.wim", "1", part_node, (char*)0};

	char * const environ[] = {NULL};
	int status = posix_spawn(&pid, "/usr/bin/wimapply", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	return 0;
}

int make_windows_to_go(const usb_drive* drive, const char* isopath) {
	setbuf(stdout, NULL);

	printf("Formatting USB drive\n");

	int error = lock_drive(drive);
	if(error) {
		printf("Error: %d\n", error);
		return error;
	}
	unsigned char uuidarray[3][16] = {0};
	prepare_windows_to_go_drive(drive, uuidarray);

	wimapply_w2go(drive);


	const char* device = drive->devnode;
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

	struct fdisk_partition* ntfs_pt = fdisk_table_get_partition_by_partno(tb, 1);
	char* ntfs_part_node = NULL;
	error = fdisk_partition_to_string(ntfs_pt, cxt, FDISK_FIELD_DEVICE, &ntfs_part_node);
	printf("\nMounting Partion: %s\n", ntfs_part_node);
	mount_partition(ntfs_part_node, WTG_NTFS_MNT_PATH);


	struct fdisk_partition* esp_pt = fdisk_table_get_partition_by_partno(tb, 0);
	char* esp_part_node = NULL;
	error = fdisk_partition_to_string(esp_pt, cxt, FDISK_FIELD_DEVICE, &esp_part_node);
	printf("\nMounting Partion: %s\n",esp_part_node);
	mount_partition(esp_part_node, WTG_ESP_MNT_PATH);
	// TODO: Missing a call to fdisk_deassign_device
	fdisk_deassign_device(cxt, 1);


	mkdir(WTG_ESP_MNT_PATH "/EFI", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Boot", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot", 0700);
	mkdir(WTG_ESP_MNT_PATH "/EFI/Microsoft/Recovery", 0700);

	pid_t pid;
	char * const environ[] = {NULL};

	char *argv[] = {"cp", "-r", WTG_NTFS_MNT_PATH "/Windows/Boot/EFI/.", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/", (char*)0};

	int status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);


	char *argv2[] = {"cp", "-r", WTG_NTFS_MNT_PATH "/Windows/Boot/Resources", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/", (char*)0};

	status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv2, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	char *argv3[] = {"cp", "-r", WTG_NTFS_MNT_PATH "/Windows/Boot/Fonts", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/", (char*)0};

	status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv3, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	char *argv4[] = {"cp", WTG_NTFS_MNT_PATH "/Windows/Boot/EFI/bootmgfw.efi", WTG_ESP_MNT_PATH "/EFI/Boot/bootx64.efi", (char*)0};

	status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv4, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);


	char *argv5[] = {"cp", WTG_NTFS_MNT_PATH "/Windows/System32/config/BCD-Template", WTG_ESP_MNT_PATH "/EFI/Microsoft/Boot/BCD", (char*)0};

	status = posix_spawn(&pid, "/usr/bin/cp", NULL, NULL, argv5, environ);
	if(status != 0) {
		fprintf(stderr, strerror(status));
		return 1;
	}

	wait(NULL);

	createBootBCD(WTG_ESP_MNT_PATH"/EFI/Microsoft/Boot/BCD", uuidarray[0], uuidarray[1], uuidarray[2]);

	// mount partition index 1
	// mount partition index 0, copy esp files + bcd from partidx1 to partidx0
	// apply SanPolicy to partidx1
	// copy sysprep unattend.xml
	// unmount all
	//printf("Mounting Device\n");
	//mount_device(drive);


	printf("Unmounting All");
	unmount_ALL();

	printf("Done\n");

	return 0;

}

int make_bootable(const usb_drive* drive, const char* isopath, int isWin2GO) {
	setbuf(stdout, NULL);

	if (!is_valid_ISO(isopath)) {
		printf("Invalid ISO file\n");
		return -1;
	}

	 printf("Mounting ISO\n");
	 mount_ISO(isopath);

	 printf("Checking if it is Windows ISO\n");

	 const int isWin = isWindowsISO();


	if ((isWin == 1) && isWin2GO) {
		printf("Making Windows to Go drive\n");
		return make_windows_to_go(drive, isopath);
	}else if(isWin == 1) {
		printf("Detected Windows ISO! Will use UEFI:NTFS\n");
		return make_windows_bootable(drive, isopath);
	}

	printf("Did not detect Windows ISO!\n");
	printf("Proceeding with simple File System Transposition\n");

	printf("Formatting USB drive\n");
	int error = lock_drive(drive);
	if (error) {
		printf("Error: %d\n", error);
		return error;
	}

	prepare_fst_drive(drive);


	printf("Mounting Device\n");
	mount_device(drive);

	printf("Copying ISO files\n");
	copy_ISO_files();

	printf("Unmounting All");
	unmount_ALL();

	printf("Done\n");

	return 0;

}
