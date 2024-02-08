//
// Created by thakur on 18/1/24.
//

#ifndef PARTITION_H
#define PARTITION_H

#include <libfdisk.h>

#include "constants.h"

int create_gpt_label(struct fdisk_context* cxt);

int partition_fst_create(struct fdisk_context* cxt);

int partition_fst_ntfs_create(struct fdisk_context* cxt);

int partition_w2go_create(struct fdisk_context* cxt, unsigned char uuidarray[3][16]);

#endif //PARTITION_H
