project('libsufur', 'c')
libfdisk = dependency('fdisk')
libudev = dependency('libudev')
lib = shared_library('sufur', 'libsufur.c', dependencies : [libfdisk, libudev])
executable('sufur_test', ['sufur_test.c'], link_with: lib)
