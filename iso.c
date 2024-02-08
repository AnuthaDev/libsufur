//
// Created by thakur on 8/2/24.
//

#include <libsufur.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <libmount.h>
#include <sys/stat.h>

#include "constants.h"
#include "utils.h"

int get_iso_properties(iso_props *props) {
	if(!props || !props->path) {
		printf("Code fatt gaya\n");
		return -1;
	}

	if(!is_valid_ISO(props->path)) {
		printf("Invalid ISO file!\n");
		return -1;
	}

	mkdir(ISO_MNT_PATH, 0700);

	struct libmnt_context* mntcxt = mnt_new_context();

	mnt_context_set_source(mntcxt, props->path);
	mnt_context_set_target(mntcxt, ISO_MNT_PATH);

	int error = mnt_context_mount(mntcxt);
	if (error) {
		printf("Error while mounting ISO %s. Aborting!\n", props->path);
		return error;
	}

	error = faccessat(-1, ISO_MNT_PATH"/sources/install.wim", F_OK, AT_EACCESS);

	if (error) {
		printf("Not Windows ISO\n");
	}else {
		props->isWindowsISO = 1;
	}

	mnt_reset_context(mntcxt);
	mnt_context_set_target(mntcxt, ISO_MNT_PATH);
	error = mnt_context_umount(mntcxt);

	if(error) {
		printf("Error while unmounting ISO %s, Aborting!\n", props->path);
		return error;
	}

	mnt_free_context(mntcxt);

	return 0;
}
