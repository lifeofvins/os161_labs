/*
 * Author: Vincenzo Sagristano
 */

/*
 * cwdtest - test for getcwd syscall.
 *
 * This test is thought to perform three kind of calls:
 * - chdir
 * - mkdir
 * - getcwd
 * 
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define CWDBUFSIZE 256
#define MODEHARDCODED 511

int main()
{
	char cwdbuffer[CWDBUFSIZE];
	getcwd(cwdbuffer, CWDBUFSIZE);
	printf("mode: %d\n", MODEHARDCODED);
	printf("Current working directory: %s\n", cwdbuffer);
	if (mkdir("./testfolder", (mode_t)777))
		printf("ops\n");
	if (!chdir("./testfolder\n"))
		printf("Operation not possible.\n");
	getcwd(cwdbuffer, CWDBUFSIZE);
	printf("Current working directory: %s\n", cwdbuffer);

	return 0;
}
