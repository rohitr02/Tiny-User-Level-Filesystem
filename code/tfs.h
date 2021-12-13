/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.h
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#ifndef _TFS_H
#define _TFS_H

#define MAGIC_NUM 0x5C3A
#define MAX_INUM 1024
#define MAX_DNUM 16384

#define INODE_BITMAP_BLOCK 1
#define DATA_BITMAP_BLOCK 2

typedef enum _TFS_TYPE{TFS_DIRECTORY=1,TFS_FILE=2}TFS_TYPE;


struct superblock {
	uint32_t	magic_num;			/* magic number */
	uint16_t	max_inum;			/* maximum inode number */
	uint16_t	max_dnum;			/* maximum data block number */
	uint32_t	i_bitmap_blk;		/* start address of inode bitmap */
	uint32_t	d_bitmap_blk;		/* start address of data block bitmap */
	uint32_t	i_start_blk;		/* start address of inode region */
	uint32_t	d_start_blk;		/* start address of data block region */
};

struct inode {
	uint16_t	ino;				/* inode number */
	uint16_t	valid;				/* validity of the inode */
	uint32_t	size;				/* size of the file */
	uint32_t	type;				/* type of the file */
	uint32_t	link;				/* link count */
	int			direct_ptr[16];		/* direct pointer to data block */
	int			indirect_ptr[8];	/* indirect pointer to data block */
	struct stat	vstat;				/* inode stat */
};

struct dirent {
	uint16_t ino;					/* inode number of the directory entry */
	uint16_t valid;					/* validity of the directory entry */
	char name[252];					/* name of the directory entry */
};


/*
 * bitmap operations
 */
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


//functions
int readi(uint16_t, struct inode *);
int writei(uint16_t, struct inode *);
struct inode* getInode(int);
void initialize_file_inode(struct inode* );
void initialize_dir_inode(struct inode* );
void free_tfsdir_vars(char* dir_path, char* sub_dir_path, struct inode* par_inode, struct inode* sub_dir_inode, bitmap_t inode_bitmap);


#endif
