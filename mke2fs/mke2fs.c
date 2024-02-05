//
// Created by thakur on 5/2/24.
//


#include <ext2fs/ext2fs.h>
#include <ntfs-3g/layout.h>

#include "include/constants.h"


int mke2fs(const char* device, int version) {
	io_manager manager = default_io_manager;
	ext2_filsys ext2fs = NULL;
	uint8_t* buf = NULL;
	int count;

	blk64_t size = 0, cur;
	ext2fs_get_device_size2(device, KIB, &size);
	printf("SIze: %lld\n", size);
	size *= KIB;

	int blocksize = 4096;
	struct ext2_super_block features = { 0 };
	for (features.s_log_block_size = 0; EXT2_BLOCK_SIZE_BITS(&features) <= EXT2_MAX_BLOCK_LOG_SIZE; features.s_log_block_size++) {
		if (EXT2_BLOCK_SIZE(&features) == blocksize)
			break;
	}

	features.s_log_cluster_size = features.s_log_block_size;
	size /= blocksize;

	ext2fs_blocks_count_set(&features, size);
	ext2fs_r_blocks_count_set(&features, (blk64_t)(0.05 * size));
	features.s_rev_level = 1;
	features.s_inode_size = 256;
	features.s_inodes_count = ((ext2fs_blocks_count(&features) >> 3) > UINT32_MAX) ?
			UINT32_MAX : (uint32_t)(ext2fs_blocks_count(&features) >> 3);

	ext2fs_set_feature_dir_index(&features);
	ext2fs_set_feature_filetype(&features);
	ext2fs_set_feature_large_file(&features);
	ext2fs_set_feature_sparse_super(&features);
	ext2fs_set_feature_xattr(&features);
	if(version != 2)
		ext2fs_set_feature_journal(&features);


	features.s_default_mount_opts = EXT2_DEFM_XATTR_USER | EXT2_DEFM_ACL;

	ext2fs_initialize(device, EXT2_FLAG_EXCLUSIVE | EXT2_FLAG_64BITS, &features, manager, &ext2fs);

	buf = calloc(16, ext2fs->io->block_size);
	io_channel_write_blk64(ext2fs->io, 0, 16, buf);
	free(buf);
	unsigned char* desiuuid = "123456789abcdefa";
	for (int i = 0;i<16;i++) {
		ext2fs->super->s_uuid[i] = desiuuid[i];
	}
	ext2fs_init_csum_seed(ext2fs);
	ext2fs->super->s_def_hash_version = EXT2_HASH_HALF_MD4;

	ext2fs->super->s_hash_seed[0] = 12;
	ext2fs->super->s_hash_seed[1] = 24;
	ext2fs->super->s_hash_seed[2] = 32;
	ext2fs->super->s_hash_seed[3] = 126;

	ext2fs->super->s_max_mnt_count = -1;
	ext2fs->super->s_creator_os = EXT2_OS_LINUX;
	ext2fs->super->s_errors = EXT2_ERRORS_CONTINUE;

	strcpy(ext2fs->super->s_volume_name, "sufury");

	ext2fs_allocate_tables(ext2fs);

	ext2fs_convert_subcluster_bitmap(ext2fs, &ext2fs->block_map);

	for (int i = 0; i < (int)ext2fs->group_desc_count; i++) {

		cur = ext2fs_inode_table_loc(ext2fs, i);
		count = ext2fs_div_ceil((ext2fs->super->s_inodes_per_group - ext2fs_bg_itable_unused(ext2fs, i))
			* EXT2_INODE_SIZE(ext2fs->super), EXT2_BLOCK_SIZE(ext2fs->super));
		ext2fs_zero_blocks2(ext2fs, cur, count, &cur, &count);
	}

	ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	ext2fs->umask = 077;
	ext2fs_mkdir(ext2fs, EXT2_ROOT_INO, 0, "lost+found");


	// Create bitmaps
	for (int i = EXT2_ROOT_INO + 1; i < (int)EXT2_FIRST_INODE(ext2fs->super); i++)
		ext2fs_inode_alloc_stats(ext2fs, i, 1);
	ext2fs_mark_ib_dirty(ext2fs);

	ext2fs_mark_inode_bitmap2(ext2fs->inode_map, EXT2_BAD_INO);

	ext2fs_inode_alloc_stats(ext2fs, EXT2_BAD_INO, 1);
	ext2fs_update_bb_inode(ext2fs, NULL);

	if(version != 2) {
		int journal_size = ext2fs_default_journal_size(ext2fs_blocks_count(ext2fs->super));
		journal_size /= 2;	// That journal init is really killing us!
		// Even with EXT2_MKJOURNAL_LAZYINIT, this call is absolutely dreadful in terms of speed...
		// ext2fs_add_journal_inode(ext2fs, journal_size, EXT2_MKJOURNAL_NO_MNT_CHECK | ((Flags & FP_QUICK) ? EXT2_MKJOURNAL_LAZYINIT : 0));
		ext2fs_add_journal_inode(ext2fs, journal_size, EXT2_MKJOURNAL_NO_MNT_CHECK | EXT2_MKJOURNAL_LAZYINIT);
	}

	ext2fs_close_free(&ext2fs);

	return 0;
}

