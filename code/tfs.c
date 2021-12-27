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

	for(int i = 0; i < DIRECT_PTRS; i++) {
		if(directory_data[i] == -1) continue;

		bio_read(superBlock->d_start_blk + directory_data[i], data_block);

		for(int j = 0; j < entries_per_dblock; j++) {
			struct dirent* temp = data_block + j;
			if(temp == NULL || temp->valid == 0) continue;

			if(strcmp(temp->name,fname) == 0) {
				matchFound = true;
				block_index = i;
				entry = temp;
				break;
			}
		}

		if(matchFound) break;
	}
	
	//If the name matches, then copy directory entry to dirent structure
	if(matchFound) {
		ret = directory_data[block_index];
		*dirent = *entry;
	}

  	free(directory_inode);
	free(data_block);
	return ret;
}



int dir_check_entries(int *dir_ino_data, int *empty_unallocated_block, struct dirent *data_block, 
	int *found_empty_slot, int *empty_block_index, int *empty_dirent_index, char *fname ) 
{
	int d_block_idx; // index of the direct pointer
	int dirent_index; // get the index of the dir-entry within the data block

	for(d_block_idx = 0; d_block_idx < DIRECT_PTRS; d_block_idx++)
	{
		// check the data bitmap to see if this data block is empty
		if (dir_ino_data[d_block_idx] == -1)
		{
			// If empty, then we set the unallocated block to this index and skip to the next data block
			if (*empty_unallocated_block == -1)
			{
				*empty_unallocated_block = d_block_idx;
			}
			// skip to next data block
			continue;
		}

		// read the allocated data block containing dir entries
		bio_read(superBlock->d_start_blk + dir_ino_data[d_block_idx], data_block);

		for(dirent_index = 0; dirent_index < entries_per_dblock; dirent_index++)
		{
			// get next dir entry (pointer addition is increments of dir entry)
			struct dirent *dir_entry = data_block + dirent_index;
			if (dir_entry == NULL || dir_entry->valid == 0)
			{
				// found an empty slot in an allocated block. Takes priority over unallocated block.
				if (*found_empty_slot == 0)
				{
					*found_empty_slot = 1;
					*empty_block_index = d_block_idx;
					*empty_dirent_index = dirent_index;
				}
				// continue because we don't care about null or invalid entries' names
				continue;
			}
			// compare the name of the entry with the new entry name
			if (strcmp(dir_entry->name, fname) == 0)
			{
				// found matching entry, error out
				return -1;
			}
		}
	}
	return 0;
}

int dir_add_entry(int *found_empty_slot, int *dir_ino_data, int *empty_block_index, struct dirent *data_block, 
int *empty_dirent_index, int *empty_unallocated_block, struct dirent *new_entry, uint16_t f_ino)
{
	// empty dir-entry in allocated data block takes priority over unallocated data block
	if (*found_empty_slot == 1)
	{
		// read the data block that contains an empty dir-entry
		bio_read(superBlock->d_start_blk + dir_ino_data[*empty_block_index], data_block);
		// store the new entry in the empty entry
		data_block[*empty_dirent_index] = *new_entry;
		// write back to disk
		bio_write(superBlock->d_start_blk + dir_ino_data[*empty_block_index], data_block);
		free(new_entry);
		return 0;
	}
	// Allocate a new data block for this directory if it does not exist
	else if (*empty_unallocated_block > -1 && dir_ino_data[*empty_unallocated_block] == -1)
	{
		// get next available block num and set the bit to 1
		int block_num = get_avail_blkno(); // get_avail_blkno will set the d_bitmap to 1 for this block

		// make the inode's direct pointer point to this data block
		dir_ino_data[*empty_unallocated_block] = block_num;
		struct dirent *new_data_block = malloc(BLOCK_SIZE);
		new_data_block[0] = *new_entry;

		// write to disk
		bio_write(superBlock->d_start_blk + block_num, new_data_block);
		free(new_data_block);
		free(new_entry);
		return 0;
	}
	// No free data blocks in this directory
	// update the bitmap
	bitmap_t i_bitmap = malloc(BLOCK_SIZE);
	bio_read(INODE_BITMAP, i_bitmap);
	unset_bitmap(i_bitmap, f_ino);
	bio_write(INODE_BITMAP, i_bitmap);

	free(i_bitmap);
	return -1;
}

int dir_add(struct inode dir_ino, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_ino's data block and check each directory entry of dir_ino
	int *dir_ino_data = calloc(DIRECT_PTRS, sizeof(int));
	memcpy(dir_ino_data, dir_ino.direct_ptr, DIRECT_PTRS * sizeof(int));

	struct dirent *data_block = calloc(1, BLOCK_SIZE); 
	int emptySlotFound = 0;
	int emptyBlock = -1; 
	int emptyBlockIndex = 0; 
	int emptyDirentIndex = 0; 

	bitmap_t d_bitmap = malloc(BLOCK_SIZE);
	bio_read(DATA_BITMAP, d_bitmap);

	if(dir_check_entries(dir_ino_data, &emptyBlock, data_block, &emptySlotFound, &emptyBlockIndex, &emptyDirentIndex, fname) == -1) return -1;

	// Step 3: Add directory entry in dir_ino's data block and write to disk
	// create new dir entry
	struct dirent *new_entry = malloc(sizeof(struct dirent));
	new_entry->valid = 1;
	new_entry->ino = f_ino;
	strncpy(new_entry->name, fname, name_len + 1);

	if(dir_add_entry(&emptySlotFound, dir_ino_data, &emptyBlockIndex, data_block, &emptyDirentIndex, &emptyBlock, new_entry, f_ino) == -1) {
		// error out if there is no free data block
		free(new_entry);
		free(data_block);
		free(dir_ino_data);
		free(d_bitmap);
		return -1;
	}

	// Update parent directory's inode
	int parent_ino = dir_ino.ino;
	struct inode *parent_inode = malloc(sizeof(struct inode));
	readi(parent_ino, parent_inode);
	parent_inode->size += sizeof(struct dirent);

	// update the direct_ptrs
	memcpy(parent_inode->direct_ptr, dir_ino_data, DIRECT_PTRS * sizeof(int));
	time(&(parent_inode->vstat.st_mtime));
	writei(parent_ino, parent_inode);

	free(parent_inode);
	free(d_bitmap);
	free(data_block);
	free(dir_ino_data);
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	struct dirent *remove = calloc(1, sizeof(struct dirent)); //*********** FLAG
	int data_block_num = dir_find(dir_inode.ino, fname, name_len, remove);
	if(data_block_num < 0 || remove->valid == 0) return data_block_num;
	
	struct dirent* block = malloc(BLOCK_SIZE); //*********** FLAG
	bio_read(superBlock->d_start_blk + data_block_num, block);
	
	bool valid_entries = false;
	
	for(int i = 0; i < entries_per_dblock; i++){
		struct dirent* current_dir = block + i;
		if(current_dir->ino == remove->ino) current_dir->valid = 0;
		else if(current_dir->valid && !valid_entries) valid_entries = true;
	}

	bio_write(superBlock->d_start_blk + data_block_num, block);
	free(block);
	if(valid_entries) return 0;

	bool toRemove = false;
	struct inode* parent_inode;
	bitmap_t data_bitmap;
	int ptr;

	for(int i = 0; i < DIRECT_PTRS; i++){
		if(dir_inode.direct_ptr[i] == data_block_num){
			toRemove = true;
			parent_inode = malloc(sizeof(struct inode));
			data_bitmap = malloc(BLOCK_SIZE);
			ptr = i;
		}

		if(toRemove) break;
	}

	if(toRemove) {
		readi(dir_inode.ino, parent_inode);
		parent_inode->direct_ptr[ptr] = -1;
		writei(dir_inode.ino,parent_inode);

		bio_read(DATA_BITMAP, data_bitmap);
		unset_bitmap(data_bitmap, dir_inode.ino);
		bio_write(DATA_BITMAP, data_bitmap);

		free(parent_inode);
		free(data_bitmap);
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
	
	for(int i = 0; i < DIRECT_PTRS; i++){
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

	for(int i = 0; i < DIRECT_PTRS; i++){
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
		bio_read(INODE_BITMAP, inode_bitmap);
		unset_bitmap(inode_bitmap, sub_dir_inode->ino);
		bio_write(INODE_BITMAP, inode_bitmap);
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
	bio_read(DATA_BITMAP, dblock_bitmap);
	for(int i = 0; i < DIRECT_PTRS; i++){
		if(target_inode->direct_ptr[i] != -1 && get_bitmap(dblock_bitmap, target_inode->direct_ptr[i]) == 1){
			free_tfsdir_vars(dir_path, sub_dir_path, par_inode, target_inode, inode_bitmap);
			free(dblock_bitmap);
			return -1;
		}
	}

	// Step 4: Clear inode bitmap and its data block
	bio_read(INODE_BITMAP, inode_bitmap);
	unset_bitmap(inode_bitmap, target_inode->ino);
	bio_write(INODE_BITMAP, inode_bitmap);

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
		bio_read(INODE_BITMAP, inode_bitmap);
		unset_bitmap(inode_bitmap, file_inode->ino);
		bio_write(INODE_BITMAP, inode_bitmap);
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

static void write_and_release(bool write, struct inode* inode, int* direct_data_block, int* indirect_data_block, int* direct_ptr_block) {

	if(write) {
		time(& (inode->vstat.st_atime));
		writei(inode->ino,inode);
	}
	free(inode);
	free(direct_data_block);
	free(indirect_data_block);
	free(direct_ptr_block);
}

int read_directs_or_indirects(int blockOffset, int byteOffset, struct inode* inode, int* direct_data_block, int* indirect_data_block, int* direct_ptr_block, char* bufferTail, size_t size, bool direct, int start, int len, bool lastCall) {
	int bytesRead = 0;

	for(int i =  start; i < len; i++) {
		if(inode->direct_ptr[i] == -1) continue;

		if(direct) bio_read(superBlock->d_start_blk + inode->direct_ptr[i], direct_data_block);
		else bio_read(superBlock->d_start_blk + indirect_data_block[i], direct_data_block);

		direct_data_block += byteOffset;

		int bytesToRead = BLOCK_SIZE - byteOffset;
		int remainingBytes = size - bytesRead;
		if(bytesToRead < remainingBytes) {
			memcpy(bufferTail,direct_data_block, bytesToRead);
			bufferTail += bytesToRead;
			bytesRead += bytesToRead;
		}
		else {
			memcpy(bufferTail,direct_data_block, remainingBytes);
			if(!lastCall) bufferTail += remainingBytes;
			bytesRead += remainingBytes;

			write_and_release(!lastCall, inode, direct_data_block, indirect_data_block, direct_ptr_block);
			return bytesRead;
		}
		
		byteOffset=0;

	}

	return bytesRead;
}


static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode* inode = malloc(BLOCK_SIZE);
	int found_status = get_node_by_path(path, 0, inode);
	if(found_status<0) return -1;
	

	// Step 2: Based on size and offset, read its data blocks from disk
	int bytesRead = 0;
	int blockOffset = offset / BLOCK_SIZE;
	int byteOffset = offset % BLOCK_SIZE;
	int* direct_data_block = malloc(BLOCK_SIZE);
	int* indirect_data_block = malloc(BLOCK_SIZE);

	char* bufferTail = buffer;
	int* direct_ptr_block = malloc(BLOCK_SIZE);

	if(blockOffset < DIRECT_PTRS) {
		bytesRead += read_directs_or_indirects(blockOffset, byteOffset, inode, direct_data_block, indirect_data_block, direct_ptr_block, bufferTail, size, true, blockOffset, DIRECT_PTRS, false);

		if(bytesRead >= size) {
			write_and_release(true, inode, direct_data_block, indirect_data_block, direct_ptr_block);
			return bytesRead;
		}

		for(int i = 0; i < INDIRECT_PTRS; i++) {
			if(inode->direct_ptr[i] == -1) continue;
			bio_read(superBlock->d_start_blk + indirect_data_block[i], indirect_data_block);
			bytesRead += read_directs_or_indirects(blockOffset, byteOffset, inode, direct_data_block, indirect_data_block, direct_ptr_block, bufferTail, size, false, 0, (BLOCK_SIZE/sizeof(int)), false);
		}
	}
	else {
		int indirect = offset - 16*BLOCK_SIZE; 
		int offsetBlocks = indirect/BLOCK_SIZE;
		int directBlocksPerIndirect = BLOCK_SIZE/sizeof(int);
		int indirectOffsetBlocks = offsetBlocks/directBlocksPerIndirect;
		int directBlockOffset = offsetBlocks%directBlocksPerIndirect;
		byteOffset = indirectOffsetBlocks%BLOCK_SIZE;

		for(int i = indirectOffsetBlocks; i < INDIRECT_PTRS; i++) {
			if(inode->direct_ptr[i] == -1) continue;
			bio_read(superBlock->d_start_blk + indirect_data_block[i], indirect_data_block);
			bytesRead += read_directs_or_indirects(blockOffset, byteOffset, inode, direct_data_block, indirect_data_block, direct_ptr_block, bufferTail, size, false, directBlockOffset, directBlocksPerIndirect, true);
		}
	}

	// Note: this function should return the amount of bytes you copied to buffer
	return bytesRead;
}

int tfs_write_all_ptrs(int offset, struct inode *inode, int *direct_data_block,
							   size_t size, char *buffer_tail, int *indirect_data_block, int *direct_ptr_block)
{
	int num_bytes_written = 0;
	int num_block_offset = offset / BLOCK_SIZE;
	int num_byte_offset = offset % BLOCK_SIZE;
	int direct_ptr_index;
	int indirect_ptr_index;
	int prev_unallocated;
	bitmap_t d_bitmap = malloc(BLOCK_SIZE);
	bio_read(DATA_BITMAP, d_bitmap);

	for (direct_ptr_index = num_block_offset; direct_ptr_index < DIRECT_PTRS; direct_ptr_index++)
	{
		// check if the data block has been initialized
		if (inode->direct_ptr[direct_ptr_index] != -1 && get_bitmap(d_bitmap, inode->direct_ptr[direct_ptr_index]) == 1)
		{
			bio_read(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);

			// if number of bytes to read in the block is less than the desired size remaining
			if (BLOCK_SIZE - num_byte_offset <= size - num_bytes_written)
			{
				memcpy(direct_data_block, buffer_tail, BLOCK_SIZE - num_byte_offset);

				bio_write(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);
				buffer_tail += BLOCK_SIZE - num_byte_offset;
				num_bytes_written += BLOCK_SIZE - num_byte_offset;
				inode->size += BLOCK_SIZE - num_byte_offset;
			}
			else if (size - num_bytes_written == 0)
			{
				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				free(d_bitmap);
				return num_bytes_written;
			}
			else
			{
				memcpy(buffer_tail, direct_data_block, size - num_bytes_written);
				bio_write(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);
				inode->size += size - num_bytes_written;
				num_bytes_written += size - num_bytes_written;
				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				free(d_bitmap);
				return num_bytes_written;
			}
			num_byte_offset = 0;
		}
		// if the data block has not been intialized
		else
		{
			// initialize a data block
			int new_data_block = get_avail_blkno();
			inode->direct_ptr[direct_ptr_index] = new_data_block;

			// now that we have initialized, let's write the data
			bio_read(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);
			// if number of bytes to read in the block is less than the desired size remaining

			if (BLOCK_SIZE - num_byte_offset <= size - num_bytes_written)
			{
				memcpy(direct_data_block, buffer_tail, BLOCK_SIZE - num_byte_offset);
				// TODO: deal with size
				bio_write(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);
				inode->size += BLOCK_SIZE - num_byte_offset;
				buffer_tail += BLOCK_SIZE - num_byte_offset;
				num_bytes_written += BLOCK_SIZE - num_byte_offset;
			}
			else if (size - num_bytes_written == 0)
			{
				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				free(d_bitmap);
				return num_bytes_written;
			}
			else
			{
				memcpy(buffer_tail, direct_data_block, size - num_bytes_written);
				bio_write(superBlock->d_start_blk + inode->direct_ptr[direct_ptr_index], direct_data_block);

				inode->size += size - num_bytes_written;
				num_bytes_written += size - num_bytes_written;

				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				free(d_bitmap);
				return num_bytes_written;
			}

			num_byte_offset = 0;
		}
	}
	// if we have read everything, return;
	if (num_bytes_written >= size)
	{
		// do some freeing
		time(&(inode->vstat.st_mtime));
		writei(inode->ino, inode);
		free(d_bitmap);
		return num_bytes_written;
	}
	// otherwise there is still data to be read, start reading indirect blocks
	for (indirect_ptr_index = 0; indirect_ptr_index < INDIRECT_PTRS; indirect_ptr_index++)
	{
		if (inode->indirect_ptr[indirect_ptr_index] == -1)
		{
			// then unallocated
			inode->indirect_ptr[indirect_ptr_index] = get_avail_blkno();
			make_direct_ptr_block(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index]); // initialize all direct ptr entries to -1 for now
		}
		// read the entire block pointed to by indirect pointer
		bio_read(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index], indirect_data_block);

		for (direct_ptr_index = 0; direct_ptr_index < BLOCK_SIZE / sizeof(int); direct_ptr_index++)
		{
			prev_unallocated = 0;
			if (indirect_data_block[direct_ptr_index] == -1)
			{
				indirect_data_block[direct_ptr_index] = get_avail_blkno();
				// TODO: check if get_avail_blkno() fails
				bio_write(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index], indirect_data_block);
				prev_unallocated = 1;
			}
			// read the block pointed to by the direct pointers in the block pointed to by the indirect poointer
			bio_read(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);

			// if number of bytes to read in the block is less than the desired size remaining
			if (BLOCK_SIZE - num_byte_offset < size - num_bytes_written)
			{
				memcpy(direct_data_block, buffer_tail, BLOCK_SIZE - num_byte_offset);
				bio_write(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);
				if (prev_unallocated == 1)
				{
					inode->size += BLOCK_SIZE - num_byte_offset;
				}
				buffer_tail += BLOCK_SIZE - num_byte_offset;
				num_bytes_written += BLOCK_SIZE - num_byte_offset;
			}
			else
			{
				memcpy(direct_data_block, buffer_tail, size - num_bytes_written);
				bio_write(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);
				if (prev_unallocated == 1)
				{
					inode->size += size - num_bytes_written;
				}
				num_bytes_written += size - num_bytes_written;
				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				free(d_bitmap);
				return num_bytes_written;
			}
			num_byte_offset = 0;
		}
	}

	if (size - num_bytes_written > 0)
	{
		time(&(inode->vstat.st_mtime));
		writei(inode->ino, inode);
		free(d_bitmap);
		return num_bytes_written;
	}
}

int tfs_write_indirect_ptrs(int offset, struct inode *inode, int *indirect_data_block,
								int *direct_data_block, size_t size, char *buffer_tail)
{
	int num_bytes_written = 0;
	int indirect_offset = offset - DIRECT_PTRS * BLOCK_SIZE; //
	int num_offset_blks = indirect_offset / BLOCK_SIZE;
	int num_direct_blks_per_indirect_ptr = BLOCK_SIZE / sizeof(int);
	int num_indirect_offset_blks = num_offset_blks / num_direct_blks_per_indirect_ptr;
	int direct_blk_offset = num_offset_blks % num_direct_blks_per_indirect_ptr;
	int num_byte_offset = indirect_offset % BLOCK_SIZE;
	int indirect_ptr_index;
	int direct_ptr_index;
	int prev_unallocated;

	if (num_indirect_offset_blks >= INDIRECT_PTRS)
	{
		return -1;
	}

	// 16*blocksize + 50*blocksize + 50bytes ==> 16+50 blocks + 50 bytes ==> 50 indirect blocks + 50 bytes ==> 50/num_direct_blks_per_indirect_ptr is the indirect index, then 50%num_direct_blks_per_indirect_ptr is the block index inside that then offset%blocksize gives the byte offset. ==>
	// otherwise there is still data to be read, start reading indirect blocks
	for (indirect_ptr_index = num_indirect_offset_blks; indirect_ptr_index < INDIRECT_PTRS; indirect_ptr_index++)
	{
		if (inode->indirect_ptr[indirect_ptr_index] == -1)
		{
			// then unallocated
			inode->indirect_ptr[indirect_ptr_index] = get_avail_blkno();
			make_direct_ptr_block(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index]); // initialize all direct ptr entries to -1 for now
		}
		// read the entire block pointed to by indirect pointer
		bio_read(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index], indirect_data_block);
		for (direct_ptr_index = direct_blk_offset; direct_ptr_index < num_direct_blks_per_indirect_ptr; direct_ptr_index++)
		{
			// initializing a block of allocated direct pointers
			prev_unallocated = 0;
			if (indirect_data_block[direct_ptr_index] == -1)
			{
				indirect_data_block[direct_ptr_index] = get_avail_blkno();
				bio_write(superBlock->d_start_blk + inode->indirect_ptr[indirect_ptr_index], indirect_data_block);
				prev_unallocated = 1;
			}
			// read the block pointed to by the direct pointers in the block pointed to by the indirect poointer
			bio_read(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);

			// if number of bytes to read in the block is less than the desired size remaining
			if (BLOCK_SIZE - num_byte_offset < size - num_bytes_written)
			{
				memcpy(direct_data_block, buffer_tail, BLOCK_SIZE - num_byte_offset);
				bio_write(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);
				if (prev_unallocated == 1)
				{
					inode->size += BLOCK_SIZE - num_byte_offset;
				}
				buffer_tail += BLOCK_SIZE - num_byte_offset;
				num_bytes_written += BLOCK_SIZE - num_byte_offset;
			}
			else
			{
				memcpy(direct_data_block, buffer_tail, size - num_bytes_written);
				bio_write(superBlock->d_start_blk + indirect_data_block[direct_ptr_index], direct_data_block);
				if (prev_unallocated == 1)
				{
					inode->size += size - num_bytes_written;
				}
				num_bytes_written += size - num_bytes_written;
				time(&(inode->vstat.st_mtime));
				writei(inode->ino, inode);
				return num_bytes_written;
			}
			num_byte_offset = 0;
		}
	}
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode *inode = malloc(BLOCK_SIZE);
	// Step 2: If not find, return -1
	if (get_node_by_path(path, 0, inode) < 0)
	{
		return -1;
	}

	int num_bytes_written = 0;
	int num_block_offset = offset / BLOCK_SIZE;
	int *direct_data_block = malloc(BLOCK_SIZE);
	int *indirect_data_block = malloc(BLOCK_SIZE);
	char *buffer_tail = buffer;
	int *direct_ptr_block = malloc(BLOCK_SIZE);

	if (num_block_offset < DIRECT_PTRS)
	{
		num_bytes_written = tfs_write_all_ptrs(offset, inode, direct_data_block,
												size, buffer_tail, indirect_data_block, direct_ptr_block);
	}
	else
	{
		num_bytes_written = tfs_write_indirect_ptrs(offset, inode, indirect_data_block, direct_data_block,
														size, buffer_tail);
	}

	free(inode);
	free(direct_data_block);
	free(indirect_data_block);
	free(direct_ptr_block);

	// Note: this function should return the amount of bytes you write to disk
	return num_bytes_written;
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
	bio_read(DATA_BITMAP, dblock_bitmap);

	// Step 4: Clear inode bitmap and its data block
	bio_read(INODE_BITMAP, inode_bitmap);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	int get_par = get_node_by_path(dir_path, 0, par_inode);
	if(get_par < 0){
		free_tfsdir_vars(dir_path, file_path, par_inode, target_inode, inode_bitmap);
		free(dblock_bitmap);
		return -1;
	}

	int* ptr_block = malloc(BLOCK_SIZE);
	for(int i = 0; i < DIRECT_PTRS; i++){
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

	bio_write(DATA_BITMAP, dblock_bitmap);
	unset_bitmap(inode_bitmap, target_inode->ino);
	bio_write(INODE_BITMAP, inode_bitmap);

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