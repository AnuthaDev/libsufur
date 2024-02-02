project('libsufur', 'c')

libfdisk = dependency('fdisk')
libmount = dependency('mount')
libudev = dependency('libudev')
libhivex = dependency('hivex')
wimlib = dependency('wimlib')

headers = include_directories('include')
sources = ['libsufur.c', 'regcreate.c', 'wtg.c', 'partition.c', 'format.c', 'utils.c']
libraries = [libfdisk, libmount, libudev, libhivex, wimlib]


libsufur = shared_library('sufur', sources, dependencies : libraries, include_directories : headers)
executable('sufur_test', ['sufur_test.c'], link_with : libsufur)
