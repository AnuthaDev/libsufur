//
// Created by thakur on 18/1/24.
//

#include "partition.h"

int create_gpt_label(struct fdisk_context* cxt) {
    fdisk_create_disklabel(cxt, "gpt");
    fdisk_write_disklabel(cxt);

    return 0;
}

int create_fst_partition(struct fdisk_context* cxt) {
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

    return 0;
}


int create_dual_fst_partitions(struct fdisk_context* cxt) {
    int error = 0;

	struct fdisk_label* label = fdisk_get_label(cxt, NULL);

	struct fdisk_partition *fat32_part = fdisk_new_partition();
	fdisk_partition_set_partno(fat32_part, 1);
	// 2048: Size of partition
	// 2048: Extra space so libfdisk can align at 1MiB boundary
	fdisk_partition_set_start(fat32_part, fdisk_get_nsectors(cxt)-2048-2048);
	fdisk_partition_set_size(fat32_part, 2048);
	fdisk_partition_end_follow_default(fat32_part, 1);

	fdisk_partition_set_name(fat32_part, "sufur_fat32");

	struct fdisk_parttype *typefat = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(fat32_part, typefat);

	error = fdisk_add_partition(cxt, fat32_part, NULL);


	struct fdisk_partition *ntfs_part = fdisk_new_partition();
	fdisk_partition_set_partno(ntfs_part, 0);
	fdisk_partition_start_follow_default(ntfs_part, 1);
	fdisk_partition_end_follow_default(ntfs_part, 1);

	fdisk_partition_set_name(ntfs_part, "sufur_ntfs");

	struct fdisk_parttype *type = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(ntfs_part, type);

	error = fdisk_add_partition(cxt, ntfs_part, NULL);


	fdisk_write_disklabel(cxt);
	fdisk_reread_partition_table(cxt);

	return error;
}

int create_windows_to_go_partitions(struct fdisk_context* cxt) {
	int error = 0;

	struct fdisk_label* label = fdisk_get_label(cxt, NULL);

	struct fdisk_partition *esp_part = fdisk_new_partition();
	fdisk_partition_partno_follow_default (esp_part, 1 );
	fdisk_partition_start_follow_default(esp_part, 1);
	fdisk_partition_set_size(esp_part, (256 * MIB)/fdisk_get_sector_size(cxt));

	fdisk_partition_set_name(esp_part, "sufur_esp");

	struct fdisk_parttype *esp_type = fdisk_label_get_parttype_from_string(label, EFI_SYSTEM_PART);
	fdisk_partition_set_type(esp_part, esp_type);

	error = fdisk_add_partition(cxt, esp_part, NULL);


	struct fdisk_partition *ntfs_part = fdisk_new_partition();
	fdisk_partition_partno_follow_default (ntfs_part, 1 );
	fdisk_partition_start_follow_default(ntfs_part, 1);
	fdisk_partition_end_follow_default(ntfs_part, 1);

	fdisk_partition_set_name(ntfs_part, "sufur_ntfs");

	struct fdisk_parttype *type = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(ntfs_part, type);

	error = fdisk_add_partition(cxt, ntfs_part, NULL);


	fdisk_write_disklabel(cxt);
	fdisk_reread_partition_table(cxt);

	return error;
}
