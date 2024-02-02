//
// Created by thakur on 18/1/24.
//

#include "partition.h"
#include "utils.h"

int create_gpt_label(struct fdisk_context* cxt) {
	fdisk_create_disklabel(cxt, "gpt");
	fdisk_write_disklabel(cxt);

	return 0;
}

int create_fst_partition(struct fdisk_context* cxt) {
	struct fdisk_partition* part = fdisk_new_partition();
	fdisk_partition_partno_follow_default(part, 1);
	fdisk_partition_start_follow_default(part, 1);
	fdisk_partition_end_follow_default(part, 1);

	fdisk_partition_set_name(part, "sufur_success");

	const struct fdisk_label* lb = fdisk_get_label(cxt, NULL);
	struct fdisk_parttype* type = fdisk_label_get_parttype_from_string(lb, MSFT_BASIC_DATA_PART);

	fdisk_partition_set_type(part, type);

	fdisk_add_partition(cxt, part, NULL);

	int error = fdisk_write_disklabel(cxt);

	if (error) {
		printf("Failed to format device\n");
		return error;
	}

	fdisk_reread_partition_table(cxt);

	return 0;
}


int create_dual_fst_partitions(struct fdisk_context* cxt) {
	int error = 0;

	const struct fdisk_label* label = fdisk_get_label(cxt, NULL);

	struct fdisk_partition* fat32_part = fdisk_new_partition();
	fdisk_partition_set_partno(fat32_part, 1);
	// 2048: Size of partition
	// 2048: Extra space so libfdisk can align at 1MiB boundary
	fdisk_partition_set_start(fat32_part, fdisk_get_nsectors(cxt) - 2048 - 2048);
	fdisk_partition_set_size(fat32_part, 2048);
	fdisk_partition_end_follow_default(fat32_part, 1);

	fdisk_partition_set_name(fat32_part, "sufur_fat32");

	struct fdisk_parttype* typefat = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(fat32_part, typefat);

	error = fdisk_add_partition(cxt, fat32_part, NULL);


	struct fdisk_partition* ntfs_part = fdisk_new_partition();
	fdisk_partition_set_partno(ntfs_part, 0);
	fdisk_partition_start_follow_default(ntfs_part, 1);
	fdisk_partition_end_follow_default(ntfs_part, 1);

	fdisk_partition_set_name(ntfs_part, "sufur_ntfs");

	struct fdisk_parttype* type = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(ntfs_part, type);

	error = fdisk_add_partition(cxt, ntfs_part, NULL);


	fdisk_write_disklabel(cxt);
	fdisk_reread_partition_table(cxt);

	return error;
}

int create_windows_to_go_partitions(struct fdisk_context* cxt, unsigned char uuidarray[3][16]) {
	int error = 0;

	const int disk_idx = 0, esp_idx = 1, boot_idx = 2;


	struct fdisk_labelitem* item = fdisk_new_labelitem();
	fdisk_get_disklabel_item(cxt, GPT_LABELITEM_ID, item);
	const char* diskuuid = NULL;
	fdisk_labelitem_get_data_string(item, &diskuuid);
	printf("Disk UUID: %s\n", diskuuid);


	uuid_swizzle(diskuuid, uuidarray[disk_idx]);
	for (int i = 0; i < 16; i++) {
		printf("%02x ", uuidarray[disk_idx][i]);
	}
	printf("\n");

	fdisk_unref_labelitem(item);


	const struct fdisk_label* label = fdisk_get_label(cxt, NULL);

	struct fdisk_partition* esp_part = fdisk_new_partition();
	fdisk_partition_partno_follow_default(esp_part, 1);
	fdisk_partition_start_follow_default(esp_part, 1);
	fdisk_partition_set_size(esp_part, (256 * MIB) / fdisk_get_sector_size(cxt));

	fdisk_partition_set_name(esp_part, "sufur_esp");

	struct fdisk_parttype* esp_type = fdisk_label_get_parttype_from_string(label, EFI_SYSTEM_PART);
	fdisk_partition_set_type(esp_part, esp_type);

	fdisk_add_partition(cxt, esp_part, NULL);

	fdisk_get_partition(cxt, 0, &esp_part);
	const char* espuuid = fdisk_partition_get_uuid(esp_part);
	printf("ESP UUID: %s\n", espuuid);


	uuid_swizzle(espuuid, uuidarray[esp_idx]);
	for (int i = 0; i < 16; i++) {
		printf("%02x ", uuidarray[esp_idx][i]);
	}
	printf("\n");


	struct fdisk_partition* ntfs_part = fdisk_new_partition();
	fdisk_partition_partno_follow_default(ntfs_part, 1);
	fdisk_partition_start_follow_default(ntfs_part, 1);
	fdisk_partition_end_follow_default(ntfs_part, 1);

	fdisk_partition_set_name(ntfs_part, "sufur_ntfs");

	struct fdisk_parttype* type = fdisk_label_get_parttype_from_string(label, MSFT_BASIC_DATA_PART);
	fdisk_partition_set_type(ntfs_part, type);

	error = fdisk_add_partition(cxt, ntfs_part, NULL);

	fdisk_get_partition(cxt, 1, &ntfs_part);
	const char* windrvuuid = fdisk_partition_get_uuid(ntfs_part);
	printf("WinDrive UUID: %s\n", windrvuuid);


	uuid_swizzle(windrvuuid, uuidarray[boot_idx]);
	for (int i = 0; i < 16; i++) {
		printf("%02x ", uuidarray[boot_idx][i]);
	}
	printf("\n");


	fdisk_write_disklabel(cxt);
	fdisk_reread_partition_table(cxt);

	return error;
}
