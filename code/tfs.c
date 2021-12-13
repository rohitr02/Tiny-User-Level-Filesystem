/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.c
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */
#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>

#include "block.h"
#include "tfs.h"

#define INODE_BITMAP 1
#define DATA_BITMAP 2
#define INODE_TABLE 3

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
int disk = -1;
struct superblock* superBlock;
int inodesPerBlock;
int entries_per_dblock;



int get_avail_blkno_or_ino(int block_num, int max_num) {
	int available_no = -1;
	bitmap_t bitMap = malloc(BLOCK_SIZE); //***********FLAG

	// Step 1: Read data block bitmap from disk
	bio_read(block_num, bitMap);
	
	// Step 2: Traverse data block bitmap to find an available slot
	for(int i = 0; i < max_num; i++) {
		if(get_bitmap(bitMap,i) == 0) {
			available_no = i;
			break;
		}
	}

	// Step 3: Update data block bitmap and write to disk 
	if(available_no != -1) {
		set_bitmap(bitMap, available_no);
		bio_write(block_num, bitMap);
	}

	free(bitMap);
	return available_no;
}

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	return get_avail_blkno_or_ino(1, superBlock->max_inum);
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	return get_avail_blkno_or_ino(2, superBlock->max_dnum);
}

int readi_or_writei(bool read, uint16_t ino, struct inode *inode) {
	int ret = 0;
	
	// Step 1: Get the on-disk block number
  	int block_num = superBlock->i_start_blk + (ino / inodesPerBlock);
  	void* block = malloc(BLOCK_SIZE); //*********** FLAG

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = (ino % inodesPerBlock) * sizeof(struct inode);

	// Step 3: Read or write
	ret = bio_read(block_num, block);
	if(read) {
		struct inode* inode_ptr = (struct inode*) malloc(sizeof(struct inode)); //*********** FLAG
		memcpy(inode_ptr, block + offset, sizeof(struct inode));
		*inode =* inode_ptr;
		free(block);
	}
	else { // write
		struct inode* inode_address = (struct inode*)(block+offset);
		*inode_address =* inode;
		ret = bio_write(block_num, block);
	}

	return ret;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	return readi_or_writei(true, ino, inode);
}

int writei(uint16_t ino, struct inode *inode) {
	return readi_or_writei(false, ino, inode);
}

/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	int ret = -1;
	bool matchFound = false;
	struct dirent* entry;

	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode* directory_inode = malloc(sizeof(struct inode)); //*********** FLAG
	int inode = readi(ino, directory_inode);
	if(inode < 0) return inode;

	// Step 2: Get data block of current directory from inode
	int* directory_data = directory_inode->direct_ptr;

	// Step 3: Read directory's data block and check each directory entry.
	struct dirent* data_block = calloc(1,BLOCK_SIZE); //*********** FLAG
	int block_index = -1;

	for(int i = 0; i < 16; i++) {
		if(directory_data[i] == -1) continue;

		bio_read(superBlock->d_start_blk + directory_data[i], data_block);

		for(int j = 0; j < entries_per_dblock; j++) {
			struct dirent* temp = data_block + j;
			if(temp == NULL || temp->valid == 0) continue;

			if(strcmp(temp->name,fname) == 0) {
				matchFound = true;
				block_index = j;
				entry = temp;
				break;
			}
		}

		if(matchFound) break;
	}
	
	//If the name matches, then copy directory entry to dirent structure
	if(matchFound) {
		ret = directory_data[block_index];
		*dirent =* entry;
	}

  	free(directory_inode);
	free(data_block);
	return ret;
}












int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {
	
	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}











/*

[][][][][][][][][][][][][][][]
[][]][][][][][][][[][][][][][][][]] 

*/
// GO THROUGH THIS ONE AGAIN, STILL NEEDS MORE TRANSLATION
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	struct dirent *remove = malloc(sizeof(struct dirent)); //*********** FLAG
	int data_block_num = dir_find(dir_inode.ino, fname, name_len, remove);
	if(data_block_num < 0 || remove->valid == 0) return data_block_num;
	
	struct dirent* data_block = malloc(BLOCK_SIZE); //*********** FLAG
	bio_read(superBlock->d_start_blk + data_block_num, data_block);
	
	bool valid_entries = false;
	
	for(int i = 0; i < entries_per_dblock; i++){
		struct dirent* current_dir = data_block + i;
		
		if(current_dir->ino == remove->ino) current_dir->valid = 0;
		else if(current_dir->valid && !valid_entries) valid_entries = true;
	}

	bio_write(superBlock->d_start_blk + data_block_num, data_block);
	
	free(data_block);
	if(!valid_entries) return 0;

	for(int i = 0; i < 16; i++){
		
		//search for the pointer to the data block, so we can remove it
		if(dir_inode.direct_ptr[i] == data_block_num){
			

			struct inode* parent_inode = malloc(sizeof(struct inode));
			readi(dir_inode.ino, parent_inode);
			parent_inode->direct_ptr[i]--;
			writei(dir_inode.ino,parent_inode);

			bitmap_t data_bitmap = malloc(BLOCK_SIZE);
			//bio_read(DATA_BITMAP_BLOCK, data_bitmap);
			unset_bitmap(data_bitmap, dir_inode.ino);
			bio_write(DATA_BITMAP, data_bitmap);

			free(parent_inode);
			free(data_bitmap);
			break;
		}
	}

	return 0;
}

















/* 
 * namei operation
 */

int get_dir_and_file_paths(const char *path, char* dir_path, char* file_path){
	//*********** FLAG there might be issues here with len of path -- if it fails then save path len as var and use that

	// ignore the initial slash if it exists
	if(path[0] == '/') path++;


	//*********** FLAG comment this if it doesnt work
	//if there is only a file path (no slashes)
	if(strchr(path, '/') == NULL){
		strncpy(file_path, path, strlen(path));
		return -1;
	}
	
	// if there is a directory path and a file path
	for(int i = strlen(path)-1; i >= 0; i--){
		if(path[i] == '/' && path[i+1] != '\0' && strlen(path)-i-1 > 0){
			if(*(path + strlen(path)-1) == '/') strncpy(file_path, path+i+1, strlen(path)-i-2);
			else strncpy(file_path, path+i+1, strlen(path)-i-1);
			strncpy(dir_path, path, i);
			return 0;
		}
	}
	
	//*********** FLAG uncomment this if it doesnt work
	// // if there is only a file path (there were no slashes)
	// strncpy(file_path, path, strlen(path));
	return -1;
}

bool separate_dirs(char* dir_path, char* root_dir, char* sub_dir){
	if(dir_path[0] == '/') dir_path++;

	for(int i = 0; i < strlen(dir_path); i++){
		if(dir_path[i] == '/'){
			strncpy(root_dir, dir_path, i);
			strncpy(sub_dir, dir_path+i+1, strlen(dir_path)-i-1);
			return true;
		}
	}
	return false;
}

void free_dir_and_file_paths(const char* dir_path, char* file_path, char* root_dir, char* sub_dir, struct dirent* dir){
	free((char*)dir_path);
	free(file_path);
	free(root_dir);
	free(sub_dir);
	free(dir);
}

void update_dir_paths(char* dir_path, char* root_dir, char* sub_dir){
	memset(dir_path, 0, 1024);
	strcpy(dir_path, sub_dir);
	memset(sub_dir, 0, 1024);
	memset(root_dir,0,512);
}

int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	if(strcmp(path, "/") == 0 || strlen(path) == 0){
		readi(0, inode);
		return 0;
	}

	char* dir_path = malloc(sizeof(char) * 1024);
	char* file_path = malloc(sizeof(char) * 256);
	char* root_dir = malloc(sizeof(char) * 512);
	char* sub_dir = malloc(sizeof(char) * 1024);

	get_dir_and_file_paths(path, dir_path, file_path);
	bool slash = strlen(dir_path) == 0 ? false : true;

	uint16_t prev_dir_ino = 0;
	struct dirent* dir = malloc(sizeof(struct dirent));

	while(slash == true){
		slash = separate_dirs(dir_path, root_dir, sub_dir);
		int get_dir_find = slash == false ? dir_find(prev_dir_ino, dir_path, strlen(dir_path) + 1, dir) : dir_find(prev_dir_ino, root_dir, strlen(root_dir) + 1, dir);
		
		if(get_dir_find < 0){
			free_dir_and_file_paths(dir_path, file_path, root_dir, sub_dir, dir);
			return get_dir_find;
		}

		prev_dir_ino = dir->ino;
		update_dir_paths(dir_path, root_dir, sub_dir);

		if(slash == false) break;
	}

	int get_dir_find = dir_find(prev_dir_ino, file_path, strlen(file_path) + 1, dir);
	if(get_dir_find < 0){
		free_dir_and_file_paths(dir_path, file_path, root_dir, sub_dir, dir);
		return get_dir_find;
	}
	
	struct inode* last_inode = malloc(sizeof(struct inode));
	int get_read = readi(dir->ino, last_inode);
	if(get_read < 0){
		free_dir_and_file_paths(dir_path, file_path, root_dir, sub_dir, dir);
		return get_read;
	}

	memcpy(inode, last_inode, sizeof(struct inode));
	free_dir_and_file_paths(dir_path, file_path, root_dir, sub_dir, dir);
	return get_read;
}

static int tfs_opendir_or_tfs_open(bool dir, const char *path, struct fuse_file_info *fi) {
	int ret = 0;

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* inode = malloc(BLOCK_SIZE);
	int found = get_node_by_path(path, 0, inode);

	// Step 2: If not find, return -1
	if(found < 0 || (!dir && !inode->valid)) ret = -1;

	if(!dir && ret != -1) inode->link++;

	free(inode);
    return ret;
}













int setup_superblock() {
	struct superblock* superblock = malloc(sizeof(struct superblock));
	
	superblock->i_bitmap_blk = INODE_BITMAP;
	superblock->d_bitmap_blk = DATA_BITMAP;
	superblock->i_start_blk = INODE_TABLE;
	
	superblock->magic_num = MAGIC_NUM;
	superblock->max_inum = MAX_INUM;
	superblock->max_dnum = MAX_DNUM;


	/*------------------[NO CLUE WTF THIS IS DOING]------------------*/ 
	/*------------------[NO CLUE WTF THIS IS DOING]------------------*/ 
	/*------------------[NO CLUE WTF THIS IS DOING]------------------*/ 
	superblock->d_start_blk = superblock->i_start_blk + ceil((float)(((float)(superblock->max_inum))/((float)inodesPerBlock)));
	superBlock = superblock;
	return 0;
}

void update_inode_vals(struct inode* inode, int valid, int size, int type, int link, struct stat* vstat, mode_t mode){
	inode->ino = get_avail_ino();				
	inode->valid = valid;			
	inode->size = size;
	inode->type = type;				
	inode->link = link;				
	
	for(int i = 0; i < 16; i++){
		inode->direct_ptr[i]--;
		if(i < 8) inode->indirect_ptr[i]=-1;
	}

	vstat->st_mode = mode;
	time(&vstat->st_mtime);
	inode->vstat =* vstat;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	disk = dev_open(diskfile_path);
	
	// write superblock information
	setup_superblock();
	
	// initialize inode bitmap
	int inode_size = superBlock->max_inum / 8;
	bitmap_t inode_bitmap = calloc(1, inode_size);

	// initialize data block bitmap
	int data_size = superBlock->max_dnum / 8;
	bitmap_t data_bitmap = calloc(1, data_size);

	// update bitmap information for root directory
	bio_write(INODE_BITMAP, inode_bitmap);
	bio_write(DATA_BITMAP, data_bitmap);

	// update inode for root directory
	struct inode* root_inode = malloc(sizeof(struct inode));
	struct stat* vstat = malloc(sizeof(struct stat));

	update_inode_vals(root_inode, 1, 0, TFS_DIRECTORY, 2, vstat, __S_IFDIR | 0755);

	writei(0, root_inode);
	return 0;
}










/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	inodesPerBlock = BLOCK_SIZE/sizeof(struct inode);
	entries_per_dblock = BLOCK_SIZE/sizeof(struct dirent);

	disk = dev_open(diskfile_path);

	// Step 1a: If disk file is not found, call mkfs
	if(disk == -1){
		if(tfs_mkfs()!=0) printf("error making disk\n");
  	// Step 1b: If disk file is found, just initialize in-memory data structures and read superblock from disk
	}else{
		superBlock = (struct superblock*) malloc(BLOCK_SIZE);
		bio_read(0, superBlock);

		if(superBlock->magic_num != MAGIC_NUM) exit(-1);
	}
	return NULL;
}

static void tfs_destroy(void *userdata) {
	// Step 1: De-allocate in-memory data structures
	free(superBlock);
	// Step 2: Close diskfile
	dev_close(disk);
}

static int tfs_getattr(const char *path, struct stat *stbuf) {
	// Step 1: call get_node_by_path() to get inode from path
	struct inode* inode = malloc(sizeof(struct inode));
	int found_node = get_node_by_path(path, 0, inode);
	if(found_node < 0) return found_node;

	// Step 2: fill attribute of file into stbuf from inode
	stbuf->st_mode = inode->vstat.st_mode;
	stbuf->st_nlink = inode->link;
	stbuf->st_mtime = inode->vstat.st_mtime;
	stbuf->st_ino = inode->ino;
	stbuf->st_size = inode->size;
	stbuf->st_gid = getgid();
	stbuf->st_uid = getuid();
	free(inode);
	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {
	return tfs_opendir_or_tfs_open(true, path, fi);
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode* inode = malloc(sizeof(struct inode));
	int found_node = get_node_by_path(path, 0, inode);
	if(found_node < 0){
		free(inode);
		return -1;
	}

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	filler(buffer, ".", NULL, offset); // Current Directory
	filler(buffer, "..", NULL, offset); // Parent Directory

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	struct dirent* dblock = malloc(BLOCK_SIZE);

	for(int i = 0; i < 16; i++){
		if(inode->direct_ptr[i] == -1) continue;

		bio_read(superBlock->d_start_blk + inode->direct_ptr[i], dblock);
		for(int j = 0; j < entries_per_dblock; j++){
			if((dblock + j)->valid == 1){
				if(filler(buffer, (dblock + j)->name, NULL, offset) != 0){
					free(inode);
					free(dblock);
					return 0;
				}
			}
		}
	}
	free(inode);
	free(dblock);
	return 0;
}

void free_tfsdir_vars(char* dir_path, char* sub_dir_path, struct inode* par_inode, struct inode* sub_dir_inode, bitmap_t inode_bitmap){
	free(dir_path);
	free(sub_dir_path);
	free(par_inode);
	free(sub_dir_inode);
	free(inode_bitmap);
}

static int tfs_mkdir(const char *path, mode_t mode) {

	char* dir_path = malloc(1024 * sizeof(char));
	char* sub_dir_path = malloc(1024 * sizeof(char));
	struct inode* par_inode = malloc(sizeof(struct inode));
	struct inode* sub_dir_inode=malloc(sizeof(struct inode));
	bitmap_t inode_bitmap = malloc(BLOCK_SIZE);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	get_dir_and_file_paths(path, dir_path, sub_dir_path);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	int get_par = get_node_by_path(dir_path, 0, par_inode);
	if(get_par < 0){
		free_tfsdir_vars(dir_path, sub_dir_path, par_inode, sub_dir_inode, inode_bitmap);
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	struct stat* vstat = malloc(sizeof(struct stat));
	update_inode_vals(sub_dir_inode, 1, 0, TFS_DIRECTORY, 2, vstat, __S_IFDIR | 0755);

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	int get_dir_add = dir_add(*par_inode, sub_dir_inode->ino, sub_dir_path, strlen(sub_dir_path));

	// Step 5: Update inode for target directory
	if(get_dir_add < 0){
		bio_read(INODE_BITMAP_BLOCK, inode_bitmap);
		unset_bitmap(inode_bitmap, sub_dir_inode->ino);
		bio_write(INODE_BITMAP_BLOCK, inode_bitmap);
		free_tfsdir_vars(dir_path, sub_dir_path, par_inode, sub_dir_inode, inode_bitmap);
		return get_dir_add;
	}

	// Step 6: Call writei() to write inode to disk
	writei(sub_dir_inode->ino,sub_dir_inode);

	get_node_by_path("foo/bar", 0, par_inode);
	free_tfsdir_vars(dir_path, sub_dir_path, par_inode, sub_dir_inode, inode_bitmap);

	return 0;
}

static int tfs_rmdir(const char *path) {
	char* dir_path = malloc(1024 * sizeof(char));
	char* sub_dir_path = malloc(1024 * sizeof(char));
	struct inode* par_inode = malloc(sizeof(struct inode));
	struct inode* target_inode=malloc(sizeof(struct inode));
	bitmap_t inode_bitmap = malloc(BLOCK_SIZE);

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	get_dir_and_file_paths(path, dir_path, sub_dir_path);

	// Step 2: Call get_node_by_path() to get inode of target directory
	int get_par = get_node_by_path(dir_path, 0, par_inode);
	if(get_par < 0){
		free_tfsdir_vars(dir_path, sub_dir_path, par_inode, target_inode, inode_bitmap);
		return -1;
	}

	int get_target = get_node_by_path(path, 0, target_inode);
	if(get_target < 0){
		free_tfsdir_vars(dir_path, sub_dir_path, par_inode, target_inode, inode_bitmap);
		return -1;
	}

	// Step 3: Clear data block bitmap of target directory
	bitmap_t dblock_bitmap = malloc(BLOCK_SIZE);
	bio_read(DATA_BITMAP_BLOCK, dblock_bitmap);
	for(int i = 0; i < 16; i++){
		if(target_inode->direct_ptr[i] != -1 && get_bitmap(dblock_bitmap, target_inode->direct_ptr[i]) == 1){
			free_tfsdir_vars(dir_path, sub_dir_path, par_inode, target_inode, inode_bitmap);
			free(dblock_bitmap);
			return -1;
		}
	}

	// Step 4: Clear inode bitmap and its data block
	bio_read(INODE_BITMAP_BLOCK, inode_bitmap);
	unset_bitmap(inode_bitmap, target_inode->ino);
	bio_write(INODE_BITMAP_BLOCK, inode_bitmap);

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(*par_inode, sub_dir_path, strlen(sub_dir_path));
	free_tfsdir_vars(dir_path, sub_dir_path, par_inode, target_inode, inode_bitmap);
	free(dblock_bitmap);
	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	char* dir_path = malloc(1024 * sizeof(char));
	char* file_path = malloc(1024 * sizeof(char));
	struct inode* par_inode = malloc(sizeof(struct inode));
	struct inode* file_inode = malloc(sizeof(struct inode));
	bitmap_t inode_bitmap = malloc(BLOCK_SIZE);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	get_dir_and_file_paths(path, dir_path, file_path);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	int get_par = get_node_by_path(dir_path, 0, par_inode);
	if(get_par < 0){
		free_tfsdir_vars(dir_path, file_path, par_inode, file_inode, inode_bitmap);
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	struct stat* vstat = malloc(sizeof(struct stat));
	update_inode_vals(file_inode, 1, 0, TFS_FILE, 1, vstat, __S_IFREG | 0666);

	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	int get_dir_add = dir_add(*par_inode, file_inode->ino, file_path, strlen(file_path));

	// Step 5: Update inode for target file
	if(get_dir_add < 0){
		bio_read(INODE_BITMAP_BLOCK, inode_bitmap);
		unset_bitmap(inode_bitmap, file_inode->ino);
		bio_write(INODE_BITMAP_BLOCK, inode_bitmap);
		free_tfsdir_vars(dir_path, file_path, par_inode, file_inode, inode_bitmap);
		return get_dir_add;
	}

	// Step 6: Call writei() to write inode to disk
	writei(file_inode->ino, file_inode);
	free_tfsdir_vars(dir_path, file_path, par_inode, file_inode, inode_bitmap);
	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	return tfs_opendir_or_tfs_open(false, path, fi);
}







static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}





static int tfs_unlink(const char *path) {
	char* dir_path = malloc(1024 * sizeof(char));
	char* file_path = malloc(1024 * sizeof(char));
	struct inode* par_inode = malloc(sizeof(struct inode));
	struct inode* target_inode = malloc(sizeof(struct inode));
	bitmap_t inode_bitmap = malloc(BLOCK_SIZE);

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	get_dir_and_file_paths(path, dir_path, file_path);

	// Step 2: Call get_node_by_path() to get inode of target file
	int get_target = get_node_by_path(path, 0, target_inode);
	if(get_target < 0){
		free_tfsdir_vars(dir_path, file_path, par_inode, target_inode, inode_bitmap);
		return -1;
	}
	if(target_inode->link -= 1 > 0){
		free_tfsdir_vars(dir_path, file_path, par_inode, target_inode, inode_bitmap);
		return 0;
	}

	// Step 3: Clear data block bitmap of target file
	bitmap_t dblock_bitmap = malloc(BLOCK_SIZE);
	bio_read(DATA_BITMAP_BLOCK, dblock_bitmap);

	// Step 4: Clear inode bitmap and its data block
	bio_read(INODE_BITMAP_BLOCK, inode_bitmap);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	int get_par = get_node_by_path(dir_path, 0, par_inode);
	if(get_par < 0){
		free_tfsdir_vars(dir_path, file_path, par_inode, target_inode, inode_bitmap);
		free(dblock_bitmap);
		return -1;
	}

	int* ptr_block = malloc(BLOCK_SIZE);
	for(int i = 0; i < 16; i++){
		if(target_inode->direct_ptr[i] != -1) unset_bitmap(dblock_bitmap, target_inode->direct_ptr[i]);
	}

	for(int i = 0; i < 8; i++){
		if(target_inode->indirect_ptr[i] != -1){
			bio_read(superBlock->d_start_blk+target_inode->indirect_ptr[i], ptr_block);
			for(int j = 0; j < BLOCK_SIZE/sizeof(int); j++){
				if(ptr_block[j] != -1) unset_bitmap(dblock_bitmap, ptr_block[j]--);
			}
			bio_write(superBlock->d_start_blk+target_inode->indirect_ptr[i], ptr_block);
			unset_bitmap(dblock_bitmap, target_inode->indirect_ptr[i]);
		}
	}

	bio_write(DATA_BITMAP_BLOCK, dblock_bitmap);
	unset_bitmap(inode_bitmap, target_inode->ino);
	bio_write(INODE_BITMAP_BLOCK, inode_bitmap);

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	dir_remove(*par_inode, file_path, strlen(file_path));
	free_tfsdir_vars(dir_path, file_path, par_inode, target_inode, inode_bitmap);
	free(dblock_bitmap);
	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

	return fuse_stat;
}