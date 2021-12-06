#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include "f_hdr.h"

#define SECTOR_SIZE  512

#define BUFFER_SIZE  (2 * SECTOR_SIZE + 1)

static void
getcwd_test() {
	char *cwd;
	char *buf = NULL;
	cwd = getcwd(buf, BUFFER_SIZE);
	if (cwd == NULL) {
		printf("Error during getcwd\n");
		return;
	}
	printf("Current working directory: %s\n", buf);
	return;
}

int
main()
{
    getcwd_test();
	return 0;
}


