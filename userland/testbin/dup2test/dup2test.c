/*
 * Author: Vincenzo Sagristano
 */

/*
 * dup2test - test for dup2 syscall.
 *
 * This test aims to perform a dup2 call such that
 * it's possible to use printf/scanf to make r/w operations
 * on a file (which filename is hardcoded for the sake of simplicity).
 * 
 * Check the file in the /.//.//. location to understand if the operation worked.
 * The final content of the file should be:
 * 
 * --Using printf...--
 * "And what marks did you see by the wicket-gate?"
 * "None in particular."
 * "Good heaven! Did no one examine?"
 * "Yes, I examined, myself."
 * "And foud nothing?"
 * "It was all very confused. Sir Charles had evidently stood there for five or ten minutes."
 * "How do you know that?"
 * "Because the ash had twice dropped from his cigar."
 * 
 * --Using puts...--
 * "And what marks did you see by the wicket-gate?"
 * "None in particular."
 * "Good heaven! Did no one examine?"
 * "Yes, I examined, myself."
 * "And foud nothing?"
 * "It was all very confused. Sir Charles had evidently stood there for five or ten minutes."
 * "How do you know that?"
 * "Because the ash had twice dropped from his cigar."
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

int main()
{
	const char *out_text = "\"And what marks did you see by the wicket-gate?\"\n"
						   "\"None in particular.\"\n"
						   "\"Good heaven! Did no one examine?\"\n"
						   "\"Yes, I examined, myself.\"\n"
						   "\"And foud nothing?\"\n"
						   "\"It was all very confused. Sir Charles had evidently stood there for five or ten minutes.\"\n"
						   "\"How do you know that?\"\n"
						   "\"Because the ash had twice dropped from his cigar.\"\n";
	const char *filename = "/dup-2-test.txt";
	int fd1;

	if((fd1 = open(filename, O_CREAT | O_RDWR, "777")) == -1)
	{
		printf("Error opening %s\n", filename);
		return -1;
	}

	dup2(fd1, STDOUT_FILENO);

<<<<<<< HEAD
	printf("%s", out_text);
=======
	printf("--Using printf...--\n");
	printf("%s", out_text);
	printf("\n--Using puts...--\n");
	puts(out_text);
>>>>>>> b298c1dcab5c721e63f8d5efddec1f779be12a82
	
	return 0;
}
