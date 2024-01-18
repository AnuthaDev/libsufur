//
// Created by thakur on 18/1/24.
//

#ifndef PARTITION_H
#define PARTITION_H

#include <libfdisk.h>

#define MSFT_BASIC_DATA_PART "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"
#define EFI_SYSTEM_PART "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"

#define KIB (1ULL << 10)
#define MIB (1ULL << 20)
#define GIB (1ULL << 30)
#define TIB (1ULL << 40)

int create_gpt_label(struct fdisk_context* cxt);

int create_fst_partition(struct fdisk_context* cxt);

int create_dual_fst_partitions(struct fdisk_context* cxt);

int create_windows_to_go_partitions(struct fdisk_context* cxt);

#endif //PARTITION_H
