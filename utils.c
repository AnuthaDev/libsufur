//
// Created by thakur on 2/2/24.
//

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libfdisk/libfdisk.h>
#include <libmount/libmount.h>

#define __USE_XOPEN_EXTENDED
#include <ftw.h>

#include "constants.h"

#include "utils.h"

static const char* get_filename_ext(const char* filename) {
	const char* dot = strrchr(filename, '.');
	if (!dot || dot == filename) return "";
	return dot + 1;
}

static int is_regular_file(const char* path) {
	struct stat path_stat;
	return !stat(path, &path_stat) && S_ISREG(path_stat.st_mode);
}

int is_valid_ISO(const char* path) {
	return is_regular_file(path) && !strcasecmp("iso", get_filename_ext(path));
}

/**
 * The job of this function is to convert this:
 * A6B212E7-FB5D-C24B-B464-251C90953F69
 * into this:
 * unsigned char bytes[16] = {e7, 12, b2, a6, 5d, fb, 4b, c2, b4, 64, 25, 1c, 90, 95, 3f, 69};
 *
 * This is some weird-endian order used by Microsoft in BCD
 *
 * If you can understand what this is, please teach me :-)
 */
void uuid_swizzle(const char* uuid, unsigned char destarr[]) {
	char swizzler[3] = {0};

	// First Part
	swizzler[0] = uuid[6];
	swizzler[1] = uuid[7];
	destarr[0] = (unsigned) strtol(swizzler, NULL, 16);

	swizzler[0] = uuid[4];
	swizzler[1] = uuid[5];
	destarr[1] = (unsigned) strtol(swizzler, NULL, 16);

	swizzler[0] = uuid[2];
	swizzler[1] = uuid[3];
	destarr[2] = (unsigned) strtol(swizzler, NULL, 16);

	swizzler[0] = uuid[0];
	swizzler[1] = uuid[1];
	destarr[3] = (unsigned) strtol(swizzler, NULL, 16);

	// Second Part
	swizzler[0] = uuid[11];
	swizzler[1] = uuid[12];
	destarr[4] = (unsigned) strtol(swizzler, NULL, 16);

	swizzler[0] = uuid[9];
	swizzler[1] = uuid[10];
	destarr[5] = (unsigned) strtol(swizzler, NULL, 16);

	// Third Part
	swizzler[0] = uuid[16];
	swizzler[1] = uuid[17];
	destarr[6] = (unsigned) strtol(swizzler, NULL, 16);

	swizzler[0] = uuid[14];
	swizzler[1] = uuid[15];
	destarr[7] = (unsigned) strtol(swizzler, NULL, 16);

	// Fourth Part
	char mover[3] = {0};

	mover[0] = uuid[19];
	mover[1] = uuid[20];
	destarr[8] = (unsigned) strtol(mover, NULL, 16);

	mover[0] = uuid[21];
	mover[1] = uuid[22];
	destarr[9] = (unsigned) strtol(mover, NULL, 16);

	// Fifth Part
	for (int i = 24, j = 10; i <= 34; i += 2, j++) {
		mover[0] = uuid[i];
		mover[1] = uuid[i + 1];
		destarr[j] = (unsigned) strtol(mover, NULL, 16);
	}
}


int mount_ISO(const char* isopath) {
	mkdir(ISO_MNT_PATH, 0700);

	struct libmnt_context* mntcxt = mnt_new_context();

	mnt_context_set_source(mntcxt, isopath);
	mnt_context_set_target(mntcxt, ISO_MNT_PATH);

	const int error = mnt_context_mount(mntcxt);
	if (error) {
		printf("Error while mounting ISO. Aborting!\n");
		return error;
	}

	mnt_free_context(mntcxt);
	return 0;
}

int mount_partition(const char* partition, const char* target) {
	int error = 0;

	// TODO: Missing a call to fdisk_deassign_device
	// EDIT: Maybe not


	mkdir(target, 0700);

	struct libmnt_context* mntcxt = mnt_new_context();

	mnt_context_set_source(mntcxt, partition);
	mnt_context_set_target(mntcxt, target);

	error = mnt_context_mount(mntcxt);
	if (error) {
		printf("Error while mounting partition %s. Aborting!\n", partition);
		return error;
	}

	mnt_free_context(mntcxt);

	return 0;
}

static char* copy_to_path;
static char* copy_from_path;
static int copy_busy = 0;

// TODO: Fix the inverted error status
int copy_file(const char* from, const char* to) {
	FILE* ff = fopen(from, "r");

	if (!ff) {
		perror("Can't open source file");
		return 0;
	}

	FILE* ft = fopen(to, "w");

	if (!ft) {
		perror("Can't open dest file");
		fclose(ff);
		return 0;
	}

	char buffer[4 * MIB] = {0};
	size_t r = 0;

	while ((r = fread(buffer, 1, sizeof(buffer), ff))) {
		fwrite(buffer, 1, r, ft);
	}

	fclose(ff);
	fclose(ft);

	return 1;
}

static int cp_callback(const char* fpath, const struct stat* sb, int type_flag, struct FTW* ftwbuf) {
	char to_location[1 * KIB];
	sprintf(to_location, "%s/%s", copy_to_path, fpath + strlen(copy_from_path) + 1);

	if (type_flag & FTW_D) {
		// is a directory...
		if (ftwbuf->level == 0) {
			// level == 0 means we are in the base directory, don't create that...
			return 0;
		}

		if (mkdir(to_location, 0775)) {
			perror("Failed to create directory\n");
			return -1;
		}
	} else if (type_flag & FTW_SL) {
		printf("TODO: Handle Symlinks\n");
	} else if (!copy_file(fpath, to_location)) {
		return -1;
	}

	return 0;
}

// copy path/* into to/
// TODO: Fix this error, this is very dangerous
// WARNING: `path` and `to` must not have trailing slash
int copy_dir_contents(char* path, char* to) {
	if (copy_busy) {
		fprintf(stderr, "copy busy!\n");
		return 0;
	}
	copy_busy = 1;
	copy_to_path = to;
	copy_from_path = path;

	int ret = nftw(path, cp_callback, 64, FTW_PHYS);

	copy_busy = 0;

	return ret;
}

int utils_get_fdisk_context(const usb_drive* drive, struct fdisk_context** cxt, const int readonly) {
	const char* device = drive->devnode;
	int error = 0;
	*cxt = fdisk_new_context();

	if (!*cxt)
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

	error = fdisk_assign_device(*cxt, device, readonly);

	if (error) {
		printf("Failed to assign fdisk device\n");
		return error;
	}

	return 0;
}


int unmount_sufur() {
	struct libmnt_context* mntcxt = mnt_new_context();

	mnt_context_set_target(mntcxt, ISO_MNT_PATH);
	int error = mnt_context_umount(mntcxt);
	if (error) {
		printf("Error while unmounting ISO. Continuing\n");
	}
	mnt_reset_context(mntcxt);


	mnt_context_set_target(mntcxt, USB_MNT_PATH);
	error = mnt_context_umount(mntcxt);
	if (error) {
		printf("Error while unmounting USB. Continuing\n");
	}
	mnt_free_context(mntcxt);

	return 0;
}

int unmount_all_partitions(const usb_drive* drive) {
	struct fdisk_context* cxt = NULL;
	int error = utils_get_fdisk_context(drive, &cxt, 1);

	if (error) {
		return error;
	}

	struct fdisk_table* tb = fdisk_new_table();
	error = fdisk_get_partitions(cxt, &tb);

	if (error) {
		printf("Error while reading partition table\n");
		return error;
	}

	const int nums = fdisk_table_get_nents(tb);
	for (int i = 0; i < nums; i++) {
		struct fdisk_partition* pt = fdisk_table_get_partition_by_partno(tb, i);
		char* part_node = NULL;
		fdisk_partition_to_string(pt, cxt, FDISK_FIELD_DEVICE, &part_node);
		struct libmnt_context* mntcxt = mnt_new_context();

		mnt_context_set_target(mntcxt, part_node);

		struct libmnt_fs *ps;
		const int not_mounted = mnt_context_find_umount_fs (mntcxt, part_node, &ps);
		mnt_free_fs(ps);

		if(not_mounted == 1) {
			printf("%s not mounted\n", part_node);
			mnt_free_context(mntcxt);
			continue;
		}

		mnt_reset_context(mntcxt);
		mnt_context_set_target(mntcxt, part_node);

		printf("Device: %s\n", part_node);

		error = mnt_context_umount(mntcxt);
		if (error) {
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
