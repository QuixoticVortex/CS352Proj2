#include <stdio.h>
#include <stdlib.h>

#include "lib-sfs.h"

#define SYS_KEY 8777

char *fn1[5] = {"f1.txt", "f2.txt", "f3.txt", "f4.txt", "f5.txt"};
char *fn2[5] = {"f5.txt", "f4.txt", "f3.txt", "f2.txt", "f1.txt"};
char *fn3[5] = {"f4.txt", "f2.txt", "f3.txt", "f1.txt", "f5.txt"};

void proc_code(int num_file , char **filename)
{
	int i ;
	FILE *fp [10];
	setbuf(stdout,NULL);
	printf ("Process %d: to call sfs_declare \n", getpid ());
	sfs_declare (SYS_KEY,num_file, filename);
	printf ("Process %d: has called sfs_declare \n", getpid ());
	for (i = 0; i < num_file; i++) {
		printf ("Process %d: to open file %s\n", getpid(),filename[ i ]);
		fp[ i] = sfs_fopen(filename[ i ],"r");
		printf ("Process %d: has opened file %s\n", getpid(),filename[ i ]);
		sleep (1);
	}
	for(i = 0; i < num_file; i++) {
		printf ("Process %d: plan to close file %s\n", getpid(),filename[ i ]);
		sfs_fclose (fp[ i ]);
		printf ("Process %d: has closed file %s\n", getpid(),filename[ i ]);
	}
	sfs_leave(SYS_KEY);
}

int main()
{
	printf("compiled\n");

	int sid ;
	setbuf(stdout,NULL);
	sid = sfs_init(SYS_KEY);
	if (fork()==0){
		proc_code(5,fn1);
		exit (0);
	} else {
		if (fork()==0) {
			proc_code(5,fn2);
			exit (0);
		} else {
			proc_code(5,fn3);
		}
	}
	wait(NULL);
	wait(NULL);
	sfs_destroy (sid );
}
