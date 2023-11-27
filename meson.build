project('sufur', 'c')
libfdisk = dependency('fdisk')
libudev = dependency('libudev')
executable('sufur', ['main.c', 'libsufur.c'], dependencies : [libfdisk, libudev])
