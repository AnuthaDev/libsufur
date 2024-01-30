//
// Created by thakur on 18/1/24.
//

#ifndef PARTITION_H
#define PARTITION_H

#include <libfdisk.h>

#include "constants.h"

int create_gpt_label(struct fdisk_context* cxt);

int create_fst_partition(struct fdisk_context* cxt);

int create_dual_fst_partitions(struct fdisk_context* cxt);

int create_windows_to_go_partitions(struct fdisk_context* cxt, unsigned char uuidarray[3][16]);

#endif //PARTITION_H
