project('libsufur', 'c')

libfdisk = dependency('fdisk')
libmount = dependency('mount')
libudev = dependency('libudev')
libhivex = dependency('hivex')
wimlib = dependency('wimlib')
libntfs = dependency('libntfs-3g')

headers = include_directories('include')
mkntfs_sources = ['mkntfs/attrdef.c', 'mkntfs/boot.c', 'mkntfs/mkntfs.c', 'mkntfs/sd.c', 'mkntfs/utils.c']
sources = ['libsufur.c', 'regcreate.c', 'wtg.c', 'partition.c', 'format.c', 'utils.c'] + mkntfs_sources
libraries = [libfdisk, libmount, libudev, libhivex, wimlib, libntfs]


libsufur = shared_library('sufur', sources, dependencies : libraries, include_directories : headers)
executable('sufur_test', ['sufur_test.c'], link_with : libsufur)
