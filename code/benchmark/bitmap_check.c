#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>

/* You need to change this macro to your TFS mount point*/
#define TESTDIR "/tmp/mountdir"

#define N_FILES 100
#define BLOCKSIZE 4096
#define FSPATHLEN 256
#define ITERS 10
#define FILEPERM 0666
#define DIRPERM 0755

#define I_BITMAP_BLOCK 4096
#define D_BITMAP_BLOCK 8192

#define MAX_INUM 1024
#define MAX_DNUM 16384

#define ERR_THRESHOLD 5

typedef unsigned char* bitmap_t;

void set_bitmap(bitmap_t b, int i) {
	b[i / 8] |= 1 << (i & 7);
}

void unset_bitmap(bitmap_t b, int i) {
	b[i / 8] &= ~(1 << (i & 7));
}

uint8_t get_bitmap(bitmap_t b, int i) {
	return b[i / 8] & (1 << (i & 7)) ? 1 : 0;
}

bitmap_t bitmap;
char buf[BLOCKSIZE];

int diskfile = -1;

int main(int argc, char **argv) {

	int i, fd = 0, ret = 0;
	int icount = 0, dcount = 0, icount_after = 0, dcount_after = 0;
	struct stat st;

	bitmap = malloc(BLOCKSIZE);

	diskfile = open("DISKFILE", O_RDWR, S_IRUSR | S_IWUSR);
	if (diskfile < 0) {
		perror("disk_open failed");
		return -1;
	}

	// Read inode bitmap
	ret = pread(diskfile, bitmap, BLOCKSIZE, I_BITMAP_BLOCK);
	if (ret <= 0) {
		perror("bitmap failed");
	}

	for (i = 0; i < MAX_INUM; ++i) {
		if (get_bitmap(bitmap, i) == 1)
			++icount;
	}

	// Read data block bitmap
	ret = pread(diskfile, bitmap, BLOCKSIZE, D_BITMAP_BLOCK);
	if (ret <= 0) {
		perror("bitmap failed");
	}

	for (i = 0; i < MAX_DNUM; ++i) {
		if (get_bitmap(bitmap, i) == 1)
			++dcount;
	}
	

	// Then do a set of operations
	if ((ret = mkdir(TESTDIR "/testdir", DIRPERM)) < 0) {
		perror("mkdir");
		exit(1);
	}

	for (i = 0; i < N_FILES; ++i) {
		char subdir_path[FSPATHLEN];
		memset(subdir_path, 0, FSPATHLEN);

		sprintf(subdir_path, "%s%d", TESTDIR "/testdir/dir", i);
		if ((ret = mkdir(subdir_path, DIRPERM)) < 0) {
			perror("mkdir");
			exit(1);
		}

		if ((ret = rmdir(subdir_path)) < 0) {
			perror("rmdir");
			exit(1);
		}
	}

	if ((fd = creat(TESTDIR "/test", FILEPERM)) < 0) {
		perror("creat");
		exit(1);
	}

	for (i = 0; i < ITERS; i++) {
		//memset with some random data
		memset(buf, 0x61 + i, BLOCKSIZE);

		if (write(fd, buf, BLOCKSIZE) != BLOCKSIZE) {
			perror("write");
			exit(1);
		}
	}

	if (close(fd) < 0) {
		perror("close");
		exit(1);
	}

	if ((ret = unlink(TESTDIR "/test")) < 0) {
		perror("unlink");
		exit(1);
	}	

	if ((ret = rmdir(TESTDIR "/testdir")) < 0) {
		perror("rmdir");
		exit(1);
	}

	// Read inode bitmap
	ret = pread(diskfile, bitmap, BLOCKSIZE, I_BITMAP_BLOCK);
	if (ret <= 0) {
		perror("bitmap failed");
	}

	for (i = 0; i < MAX_INUM; ++i) {
		if (get_bitmap(bitmap, i) == 1)
			++icount_after;
	}

	memset(bitmap, 0, BLOCKSIZE);

	// Read data block bitmap
	ret = pread(diskfile, bitmap, BLOCKSIZE, D_BITMAP_BLOCK);
	if (ret <= 0) {
		perror("bitmap failed");
	}

	for (i = 0; i < MAX_DNUM; ++i) {
		if (get_bitmap(bitmap, i) == 1)
			++dcount_after;
	}
	
	if (abs(icount - icount_after) < ERR_THRESHOLD) {
		printf("inodes reclaimed successfully %d\n", icount - icount_after);
	} else {
		printf("inodes reclaimed unsuccessfuly %d\n", icount - icount_after);
	}

	if (abs(dcount - dcount_after) < ERR_THRESHOLD) {
		printf("data blocks reclaimed successfully %d\n", dcount - dcount_after);
	} else {
		printf("data blocks reclaimed unsuccessfuly %d\n", dcount - dcount_after);
	}

	printf("Benchmark completed \n");
	return 0;
}
