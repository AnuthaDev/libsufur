project('libsufur', 'c')

libfdisk = dependency('fdisk')
libmount = dependency('mount')
libudev = dependency('libudev')
libhivex = dependency('hivex')
wimlib = dependency('wimlib')
libntfs = dependency('libntfs-3g')
libext2fs = dependency('ext2fs')
libuuid = dependency('uuid')

libraries = [libfdisk, libmount, libudev, libhivex, wimlib, libntfs, libext2fs, libuuid]

#TODO: Use meson subdir for mkvfat and mkntfs
headers = include_directories('include', 'mkvfat/blkdev')

mkntfs_sources = ['mkntfs/attrdef.c', 'mkntfs/boot.c', 'mkntfs/mkntfs.c', 'mkntfs/sd.c', 'mkntfs/utils.c']
blkdev_sources = ['mkvfat/blkdev/blkdev.c', 'mkvfat/blkdev/linux_version.c']
mkvfat_sources = ['mkvfat/charconv.c', 'mkvfat/common.c', 'mkvfat/device_info.c', 'mkvfat/mkfs.fat.c']
mke2fs_sources = ['mke2fs/mke2fs.c']
mkexfat_sources = ['mkexfat/mkexfat.c', 'mkexfat/libexfat.c', 'mkexfat/exfat_dir.c', 'mkexfat/exfat_fs.c', 'mkexfat/upcase.c']

sources = ['libsufur.c', 'regcreate.c', 'w2go.c', 'partition.c', 'format.c', 'utils.c', 'iso.c', 'log.c']
sources += mkntfs_sources + blkdev_sources + mkvfat_sources + mke2fs_sources + mkexfat_sources


libsufur = shared_library('sufur', sources, dependencies : libraries, include_directories : headers)
executable('sufur_test', ['sufur_test.c'], link_with : libsufur)
