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


    WIMStruct *wim;
    wimlib_open_wim(ISO_MNT_PATH "/sources/install.wim", 0, &wim);
    // TODO: Add progress reporting
    wimlib_extract_image(wim, 1, part_node, WIMLIB_EXTRACT_FLAG_NTFS);
    wimlib_free(wim);

    printf("Done Applying WIM\n");

    return 0;
}
