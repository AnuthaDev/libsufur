project('libsufur', 'c')

libfdisk = dependency('fdisk')
libmount = dependency('mount')
libudev = dependency('libudev')
libhivex = dependency('hivex')
wimlib = dependency('wimlib')
libntfs = dependency('libntfs-3g')
libblkid = dependency('blkid')
libext2fs = dependency('ext2fs')
libcomerr = dependency('com_err')
libe2p = dependency('e2p')
libuuid = dependency('uuid')

libraries = [libfdisk, libmount, libudev, libhivex, wimlib, libntfs, libext2fs, libblkid, libcomerr, libe2p, libuuid]

#TODO: Use meson subdir for mkvfat and mkntfs
headers = include_directories('include', 'mkvfat/blkdev')

mkntfs_sources = ['mkntfs/attrdef.c', 'mkntfs/boot.c', 'mkntfs/mkntfs.c', 'mkntfs/sd.c', 'mkntfs/utils.c']
blkdev_sources = ['mkvfat/blkdev/blkdev.c', 'mkvfat/blkdev/linux_version.c']
mkvfat_sources = ['mkvfat/charconv.c', 'mkvfat/common.c', 'mkvfat/device_info.c', 'mkvfat/mkfs.fat.c']
mke2fs_sources = ['mke2fs/create_inode.c', 'mke2fs/devname.c', 'mke2fs/mke2fs.c', 'mke2fs/plausible.c', 'mke2fs/util.c']

sources = ['libsufur.c', 'regcreate.c', 'wtg.c', 'partition.c', 'format.c', 'utils.c']
sources += mkntfs_sources + blkdev_sources + mkvfat_sources + mke2fs_sources


libsufur = shared_library('sufur', sources, dependencies : libraries, include_directories : headers)
executable('sufur_test', ['sufur_test.c'], link_with : libsufur)
