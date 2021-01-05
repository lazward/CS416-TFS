/*
 *  Copyright (C) 2020 CS416 Rutgers CS
 *	Tiny File System
 *	File:	tfs.c
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

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
struct superblock * superblock ; // superblock
bitmap_t inoBitmap ; // inode bitmap
bitmap_t blknoBitmap ; // data bitmap

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bio_read(superblock->i_bitmap_blk, inoBitmap) ;
	
	// Step 2: Traverse inode bitmap to find an available slot
	int i ;
	for (i = 0 ; i < MAX_INUM ; i++) {

		if (get_bitmap(inoBitmap, i) == 0) {

			break ;

		} 

	}

	if (i >= MAX_INUM) {

		return -1 ;

	}

	// Step 3: Update inode bitmap and write to disk 
	set_bitmap(inoBitmap, i) ;
	bio_write(superblock->i_bitmap_blk, inoBitmap) ;

	return i;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	bio_read(superblock->d_bitmap_blk, blknoBitmap) ;
	
	// Step 2: Traverse data block bitmap to find an available slot
	int i ; 
	for (i = 0 ; i < MAX_DNUM ; i++) {

		if (get_bitmap(blknoBitmap, i) == 0) {

			break ;

		} 

	}

	if (i >= MAX_DNUM) {

		return -1 ;

	}

	// Step 3: Update data block bitmap and write to disk 
	set_bitmap(blknoBitmap, i) ;
	bio_write(superblock->d_bitmap_blk, blknoBitmap) ;

	return superblock->d_start_blk + i ;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number
  int block = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode))) ;

  // Step 2: Get offset of the inode in the inode on-disk block
  int offset = ino % (BLOCK_SIZE / sizeof(struct inode)) ;

  // Step 3: Read the block from disk and then copy into inode structure
  struct inode * tempblock = (struct inode *)malloc(BLOCK_SIZE) ;
  bio_read(block, (void *)tempblock) ;
  tempblock = tempblock + offset ;
  *inode = *tempblock ;
  tempblock = tempblock - offset ;
  free(tempblock) ;

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	int block = superblock->i_start_blk + (ino / (BLOCK_SIZE / sizeof(struct inode))) ;
	
	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = ino % (BLOCK_SIZE / sizeof(struct inode)) ;

	// Step 3: Write inode to disk 
	struct inode * tempblock = (struct inode *)malloc(BLOCK_SIZE) ;
  	bio_read(block, (void *)tempblock) ;
  	tempblock = tempblock + offset ;
  	*tempblock = *inode ;
  	tempblock = tempblock - offset ;
	bio_write((const int)block, (const void *)tempblock) ;
  	free(tempblock) ;

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	//printf("called dir find\n") ;

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  struct inode * currenti = (struct inode *)malloc(sizeof(struct inode)) ;
  readi(ino, currenti) ;

  // Step 2: Get data block of current directory from inode
  struct dirent * currentd = (struct dirent *)malloc(BLOCK_SIZE * sizeof(struct inode)) ;
  //bio_read(currenti->direct_ptr, currentd) ;

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure
  int i, j ;

  for (i = 0 ; i < 16 ; i++) {

	if (currenti->direct_ptr[i] == 0) { // Not found, so return early

		return -1 ;

	}

	bio_read(currenti->direct_ptr[i], currentd) ;

	for (j = 0 ; j < (BLOCK_SIZE / sizeof(struct dirent)) ; j++) {

	  	if (currentd->valid) {

			//printf("%s and %s i = %d, j = %d\n", currentd->name, fname, i, j) ;
		  	if (strcmp(currentd->name, fname) == 0 ) { // Found

			  *dirent = *currentd ;
			  return 0 ;

		  	}

	  	}

	  currentd++ ;

  	}

  }

	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	//printf("CALLED DIR ADD NAME = %s\n", fname) ;

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    struct dirent * currentd = (struct dirent *)malloc(BLOCK_SIZE * sizeof(struct inode)) ;
	
	// Step 2: Check if fname (directory name) is already used in other entries
	int i, j ;

	for (i = 0 ; i < 16 ; i++) {

		if (dir_inode.direct_ptr[i] == 0) {

			break ;

		}

		bio_read(dir_inode.direct_ptr[i], currentd) ;

		for (j = 0 ; j < (BLOCK_SIZE / sizeof(struct dirent)) ; j++) {

	    	if (currentd->valid) {

		  		if (strcmp(currentd->name, fname) == 0) { // name is already used

					//printf("found, failed\n") ;
					return -1 ;

				}

			}

		currentd++ ;

		}

	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	struct dirent * entry ;

	for (i = 0 ; i < 16 ; i++) {

		if (dir_inode.direct_ptr[i] == 0) { // Allocate a new data block for this directory if it does not exist

			//printf("allocate new block at i = %d\n", i) ;
			dir_inode.direct_ptr[i] = get_avail_blkno() ;
			struct inode * newBlock = (struct inode *)malloc(BLOCK_SIZE) ;
			bio_write(dir_inode.direct_ptr[i], (void *)newBlock) ;
			free(newBlock) ;
			dir_inode.vstat.st_blocks++ ;
			if (i < 15) {

				dir_inode.direct_ptr[i + 1] = 0 ;

			}

		}

		bio_read(dir_inode.direct_ptr[i], currentd) ;
		entry = currentd ;
		//printf("new currentd\n") ;

		for (j = 0 ; j < (BLOCK_SIZE / sizeof(struct dirent)) ; j++) {

			if (entry->valid == 0) {

				entry->ino = f_ino ;
				strncpy(entry->name, fname, name_len + 1) ;
				//printf("ENTRY NAME = %s j = %d\n", entry->name, j) ;
				entry->valid = 1 ;
				break ;

			}

			entry++ ;

		}

		if (entry->valid == 1) {

			//printf("ENTRY NAME = %s i = %d\n", entry->name, i) ;
			break ;

		}

	}

	// Update directory inode
	struct inode * update = (struct inode *)malloc(sizeof(struct inode)) ;
	*update = dir_inode ;
	update->size = update->size + sizeof(struct dirent) ;
	update->vstat.st_size = update->vstat.st_size + sizeof(struct dirent) ;
	time(&update->vstat.st_mtime) ;

	// Write directory entry
	writei(update->ino, update) ;
	bio_write(dir_inode.direct_ptr[i], currentd) ;

	//printf("DIR ADD FINISHED\n") ;

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	struct dirent * currentd = (struct dirent *)malloc(BLOCK_SIZE) ;
	
	// Step 2: Check if fname exist
	struct dirent * entry = (struct dirent *)malloc(sizeof(struct dirent)) ;
	int i, j ;

	for (i = 0 ; i < 16 ; i++) {

		if (dir_inode.direct_ptr[i] == 0) {

			return -1 ;

		}

		bio_read(dir_inode.direct_ptr[i], currentd) ;
		entry = currentd ;

		for (j = 0 ; j < (BLOCK_SIZE / sizeof(struct dirent)) ; j++) {

	    	if (entry->valid) {

		  		if (strcmp(entry->name, fname) == 0) { // name is already used

					break ;
					
				}

			}

		entry++ ;

		}

		if (strcmp(entry->name, fname) == 0) { // name is already used

			break ;
					
		}

	}

	if (i >= 16) {

		return -1 ;

	}

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	struct inode * update = (struct inode *)malloc(sizeof(struct inode)) ;
	*update = dir_inode ;
	update->size = update->size - sizeof(struct dirent) ;
	update->vstat.st_size = update->vstat.st_size - sizeof(struct dirent) ;
	entry->valid = 0 ;
	writei(update->ino, update) ;
	bio_write(dir_inode.direct_ptr[i], (const void *)currentd) ;

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	//printf("reached get node\n") ;
	//printf("path: %s\n", path) ;
	//ino = 0 ; 
	char * delim = (char *)malloc(2 * sizeof(char)) ;
	delim[0] = '/' ;
	delim[1] = '\0' ; // Doing both in one line doesn't work
	char * str = strtok(path, delim) ;
	struct dirent * entry = (struct dirent *)malloc(sizeof(struct dirent)) ;
	entry->ino = 0 ;

	if (strcmp(path, delim) == 0) { // root

		//printf("ROOT\n") ;
		str = NULL ;

	}

	while (str != NULL) {

		//printf("loop %s\n", str) ;
		if (dir_find(entry->ino, (const char *)str, (size_t)strlen(str), entry) == -1) {

			return -1 ;

		}
		str = strtok(NULL, delim) ;

	}

	//printf("reached readi\n") ;

	readi(entry->ino, inode) ;

	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path) ;

	// write superblock information
	superblock = (struct superblock *)malloc(BLOCK_SIZE) ;
	superblock->magic_num = MAGIC_NUM ;
	superblock->max_inum = MAX_INUM ;
	superblock->max_dnum = MAX_DNUM ;
	superblock->i_bitmap_blk = 1 ;
	superblock->d_bitmap_blk = 2 ;
	superblock->i_start_blk = 3 ;
	superblock->d_start_blk = 3 + ((sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE) ;
	bio_write(0, superblock) ;
	
	// initialize inode bitmap
	inoBitmap = (bitmap_t)malloc(BLOCK_SIZE) ;

	// initialize data block bitmap
	blknoBitmap = (bitmap_t)malloc(BLOCK_SIZE) ;

	// update bitmap information for root directory
	set_bitmap(inoBitmap, 0) ;
	bio_write(superblock->i_bitmap_blk, inoBitmap) ;
	set_bitmap(blknoBitmap, 0) ;
	bio_write(superblock->d_bitmap_blk, blknoBitmap) ;

	// update inode for root directory
	struct inode * rootNode = (struct inode *)malloc(BLOCK_SIZE) ;
	bio_read(superblock->i_start_blk, rootNode) ;
	rootNode->ino = 0 ;
	rootNode->valid = 1 ;
	rootNode->link = 0 ;
	rootNode->indirect_ptr[0] = 0 ;
	rootNode->direct_ptr[0] = superblock->d_start_blk ;
	rootNode->direct_ptr[1] = 0 ;
	rootNode->type = 1 ;

	struct stat * r = (struct stat *)malloc(sizeof(struct stat)) ;
	r->st_mode = S_IFDIR | 0755 ;
	r->st_nlink = 2 ; // Need two links, "." for itself and ".." for the parent
	time(&r->st_mtime) ;
	r->st_blocks = 1 ;
	r->st_blksize = BLOCK_SIZE ;
	rootNode->vstat = *r ;
	bio_write(superblock->i_start_blk, rootNode) ;
	free(rootNode) ;

	struct dirent * rootDir = (struct dirent *)malloc(BLOCK_SIZE) ;
	rootDir->ino = 0 ;
	rootDir-> valid = 1 ;
	char c[2] ;
	c[0] = '.' ;
	c[1] = '\0' ;
	strncpy(rootDir->name, c, 2) ; // Current directory
	//printf("rootDir name = %s\n", rootDir->name) ;
	struct dirent * parent = rootDir + 1 ;
	parent->ino = 0 ;
	parent->valid = 1 ;
	char p[3] ;
	p[0] = '.' ;
	p[1] = '.' ;
	p[2] = '\0' ;
	strncpy(parent->name, p, 3) ; // Parent directory
	//printf("parent name = %s\n", (rootDir + 1)->name) ;
	bio_write(superblock->d_start_blk, rootDir) ;
	free(rootDir) ;

	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	//printf("INIT CALLED\n") ;

	// Step 1a: If disk file is not found, call mkfs
	if(dev_open(diskfile_path) == -1) {

		//printf("running mkfs\n") ;
		tfs_mkfs() ;
		return NULL ;

	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk
  superblock = (struct superblock *)malloc(BLOCK_SIZE) ;
  bio_read(0, superblock) ;
  inoBitmap = (bitmap_t)malloc(BLOCK_SIZE) ;
  bio_read(superblock->i_bitmap_blk, inoBitmap) ;
  blknoBitmap = (bitmap_t)malloc(BLOCK_SIZE) ;
  bio_read(superblock->d_bitmap_blk, blknoBitmap) ;


	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(superblock) ;
	free(inoBitmap) ;
	free(blknoBitmap) ;

	// Step 2: Close diskfile
	dev_close() ;

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	//printf("reached attr\n") ;
	// Step 1: call get_node_by_path() to get inode from path
	struct inode * in = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, in) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 2: fill attribute of file into stbuf from inode
	*stbuf = in->vstat ;
/*
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);
		stbuf->st_uid = getuid() ; // st_uid getuid()
		stbuf->st_gid = getgid() ;// st_gid getgid()
		stbuf->st_atime = time(NULL) ;
		stbuf->st_mtime = time(NULL) ;
		// st_nlink
		// st_size
		// st_mtime
		// st_mode
		*/
		

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * in = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, in) == 0) {

		if (in->valid) {

		free(in) ;
		return 0 ;

		}

	}
	free(in) ;

	// Step 2: If not find, return -1

    return -1;
	

}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * in = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, in) != 0) {

		return -ENOENT ; // “No such file or directory.”

	} 
	
	// Step 2: Read directory entries from its data blocks, and copy them to filler
	int i ;
	for (i = 0 ; i < 16 ; i++) {

		if (in->direct_ptr[i] == 0) {

			break ;

		}

		struct dirent * entry = (struct dirent *)malloc(BLOCK_SIZE) ;
		bio_read(in->direct_ptr[i], entry) ;
		int j ;
		for (j = 0 ; j < (BLOCK_SIZE / sizeof(struct dirent)) ; j++) {

			if (entry->valid == 1) {

				struct inode * k = (struct inode *)malloc(sizeof(struct inode)) ;
				readi(entry->ino, k) ;
				filler(buffer, entry->name, &k->vstat, 0) ; // 3. Call the filler function with arguments of buf, the null-terminated filename, the address of your struct stat (or NULL if you have none), and the offset of the next directory entry.

			}

			entry++ ;

		}
	}


	/*

	1. Find the first directory entry following the given offset (see below).
	2. Optionally, create a struct stat that describes the file as for getattr (but FUSE only looks at st_ino and the file-type bits of st_mode).
	3. Call the filler function with arguments of buf, the null-terminated filename, the address of your struct stat (or NULL if you have none), and the offset of the next directory entry.
	4. If filler returns nonzero, or if there are no more files, return 0.
	5. Find the next file in the directory.
	6. Go back to step 2.

	*/


	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	//printf("MKDIR CALLED\n") ;

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char * directoryPath = (char *)malloc(strlen(path) + 1) ;
	strcpy(directoryPath, path) ;
	dirname(directoryPath) ;
	//printf("dirPath: %s\n", directoryPath) ;
	char * baseName = (char *)malloc(strlen(path) + 1) ;
	strcpy(baseName, path) ;
	//printf("Before baseName %s\n", baseName) ;
	baseName = basename(baseName) ;
	//printf("baseName: %s\n", baseName) ;

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * parentNode = (struct inode *)malloc(sizeof(struct inode)) ;
	//printf("getting parent\n") ;
	if (get_node_by_path(directoryPath, 0, parentNode) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail = get_avail_ino() ;

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	dir_add(*parentNode, avail, (const char *)baseName, (size_t)strlen(baseName)) ;

	// Step 5: Update inode for target directory
	struct inode * update = (struct inode *)malloc(sizeof(struct inode)) ;
	update->valid = 1 ;
	update->ino = avail ;
	update->link = 0 ;
	update->direct_ptr[0] = get_avail_blkno() ;
	struct inode * tempblock = (struct inode *)malloc(BLOCK_SIZE) ;
	bio_write(update->direct_ptr[0], (void *)tempblock) ;
	free(tempblock) ;
	update->direct_ptr[1] = 0 ;
	update->indirect_ptr[0] = 0 ;
	update->type = 1 ;
	update->size = sizeof(struct dirent) * 2; // Unix convention
	struct stat * r = (struct stat *)malloc(sizeof(struct stat)) ;
	r->st_mode = S_IFDIR | 0755 ; // Directory
	r->st_nlink = 1 ;
	r->st_ino = update->ino ;
	time(&r->st_mtime) ;
	r->st_blocks = 1 ;
	r->st_blksize = BLOCK_SIZE ;
	r->st_size = update->size ;
	update->vstat = *r ;
	free(r) ;

	// Step 6: Call writei() to write inode to disk
	writei(avail, update) ;

	struct dirent * rootDir = (struct dirent *)malloc(BLOCK_SIZE) ;
	rootDir->ino = avail ;
	rootDir-> valid = 1 ;
	char c[2] ;
	c[0] = '.' ;
	c[1] = '\0' ;
	strncpy(rootDir->name, c, 2) ; // Current directory
	//printf("rootDir name = %s\n", rootDir->name) ;
	struct dirent * parent = rootDir + 1 ;
	parent->ino = parent->ino ;
	parent->valid = 1 ;
	char p[3] ;
	p[0] = '.' ;
	p[1] = '.' ;
	p[2] = '\0' ;
	strncpy(parent->name, p, 3) ; // Parent directory
	//printf("parent name = %s\n", (rootDir + 1)->name) ;
	bio_write(update->direct_ptr[0], (const void *)rootDir) ;
	free(rootDir) ;
	
	//printf("MKDIR FINISHED\n") ;

	return 0;
}

static int tfs_rmdir(const char *path) {

	//printf("RMDIR CALLED\n") ;

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char * directoryPath = (char *)malloc(strlen(path) + 1) ;
	strcpy(directoryPath, path) ;
	dirname(directoryPath) ;
	//printf("dirPath: %s\n", directoryPath) ;
	char * baseName = (char *)malloc(strlen(path) + 1) ;
	strcpy(baseName, path) ;
	//printf("Before baseName %s\n", baseName) ;
	baseName = basename(baseName) ;
	//printf("baseName: %s\n", baseName) ;

	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode * target = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, target) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 3: Clear data block bitmap of target directory
	int i ;
	for (i = 0 ; i < 16 ; i++) {

		if (target->direct_ptr[i] == 0) {

			break ;

		}

		unset_bitmap(blknoBitmap, target->direct_ptr[i] - superblock->d_start_blk) ;
		target->direct_ptr[i] = 0 ;

	}

	bio_write(superblock->d_bitmap_blk, blknoBitmap) ;

	// Step 4: Clear inode bitmap and its data block
	target->valid = 0 ;
	unset_bitmap(inoBitmap, target->ino) ;
	bio_write(superblock->i_bitmap_blk, inoBitmap) ;
	writei(target->ino, target) ;
	free(target) ;

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode * parent = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(directoryPath, 0, parent) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	dir_remove(*parent, (const char *)baseName, (size_t)strlen(baseName)) ;

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	//printf("PATH: %s\n", path) ;

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char * directoryPath = (char *)malloc(strlen(path) + 1) ;
	strcpy(directoryPath, path) ;
	dirname(directoryPath) ;
	//printf("dirPath: %s\n", directoryPath) ;
	char * baseName = (char *)malloc(strlen(path) + 1) ;
	strcpy(baseName, path) ;
	//printf("Before baseName %s\n", baseName) ;
	baseName = basename(baseName) ;
	//printf("baseName: %s\n", baseName) ;

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * parent = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(directoryPath, 0, parent) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail = get_avail_ino() ;

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	dir_add(*parent, avail, (const char *)baseName, strlen(baseName)) ;

	// Step 5: Update inode for target file
	struct inode * update = (struct inode *)malloc(sizeof(struct inode)) ;
	update->valid = 1 ;
	update->ino = avail ;
	update->link = 0 ;
	update->direct_ptr[0] = get_avail_blkno() ;
	update->direct_ptr[1] = 0 ;
	update->indirect_ptr[0] = 0 ;
	update->type = 0 ;
	update->size = 0 ;
	struct stat * ustat = (struct stat *)malloc(sizeof(struct stat)) ;
	ustat->st_mode = S_IFREG | 0666 ; // File
	ustat->st_nlink = 1 ;
	ustat->st_ino = update->ino ;
	ustat->st_size = update->size ;
	ustat->st_blocks = 1 ;
	time(&ustat->st_mtime) ;
	update->vstat = *ustat ;

	// Step 6: Call writei() to write inode to disk
	writei(avail, update) ;

	//printf("written\n") ;

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	//printf("CALLED OPEN PATH = %s\n", path) ;

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * in = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, in) == 0) {

		if (in->valid) {

		free(in) ;
		return 0 ;

		}

	}
	free(in) ;

	// Step 2: If not find, return -1

    return -1;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	//printf("READ CALLED\n") ;

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode * node = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, node) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int i ;
	char * b = (char *)malloc(BLOCK_SIZE) ;
	int fileSize = size + offset ;
	int bytes = ((node->vstat.st_blocks * BLOCK_SIZE) - offset) ;
	for (i = 0 ; i < (int)size && i < bytes ; i++) {

		if (offset >= fileSize) {

			break ;

		}

		int blk = offset / BLOCK_SIZE ;
		int off ;

		if (blk > 15) { // Large file support

			off = (blk - 16) % (BLOCK_SIZE / sizeof(int)) ; // have to change offset first since block number gets changed
			blk = (blk - 16) / (BLOCK_SIZE / sizeof(int)) ;
			bio_read(node->indirect_ptr[blk], b) ;
			int * w = *(int *)(b + (off * sizeof(int))) ;
			bio_read(w, b) ;

		} else {

			bio_read(node->direct_ptr[blk], b) ;
			
		}
		char * a = b ;
		int j = 0 ;
		for ( ; i < (int)size && i < bytes ; i++) {

			if (offset >= fileSize || j >= BLOCK_SIZE) {

				break ;

			}

			char c = *b ; // Need this or readi gets invalid argument
			*buffer = c ;
			buffer++ ;
			offset++ ;
			j++ ;
			b++ ;

		}

		b = a ;

	}

	// Step 3: copy the correct amount of data from offset to buffer
	i-- ;

	// Note: this function should return the amount of bytes you copied to buffer
	//printf("READ returning %d\n", i ) ;
	return i ;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	//printf("WRITE CALLED path = %s\n", path) ;

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode * node = (struct inode *)malloc(sizeof(struct inode)) ;
	
	if (get_node_by_path(path, 0, node) != 0) {

		//printf("Failed?\n") ;
		return -ENOENT ;

	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int i ;
	char * b = (char *)malloc(BLOCK_SIZE) ;
	int bytesWritten = 0 ;
	for (i = offset ; i < (offset + size) ; i++) {

		int blk = i / BLOCK_SIZE ;
		int w = 0 ;

		//printf("blk = %d\n", blk) ;

		if (blk > 15) { // Large file support

			int off = (blk - 16) % (BLOCK_SIZE / sizeof(int)) ; // have to set offset first since block number gets changed
			blk = (blk - 16) / (BLOCK_SIZE / sizeof(int)) ; // indirect block number
			//printf("large = %d %d\n", off, blk) ;
			if (node->indirect_ptr[blk] == 0) {

				node->indirect_ptr[blk] = get_avail_blkno() ;
				//printf("NEWER BLOCK %d\n", node->indirect_ptr[blk]) ;
				if (blk < 7) {

					node->indirect_ptr[blk + 1] = 0 ;

				}

				struct inode * newBlock = (struct inode *)malloc(BLOCK_SIZE) ;
				bio_write(node->indirect_ptr[blk], newBlock) ;
				free(newBlock) ;

			}

			bio_read(node->indirect_ptr[blk], b) ;
			//printf("read b = %s\n", b) ;
			b = b + (off * sizeof(int)) ;
			int temp = *(int *)b ;

			if (temp == 0) {
				
				//printf("temp = %d b = %s\n", temp, b) ;
				*(int *)b = get_avail_blkno() ; // indirect pointer points to block of direct pointers
				temp = *(int *)b ;
				node->vstat.st_blocks++ ;
				//printf("new block %d\n", temp) ;

			}

			b = b - (off * sizeof(int)) ;
			bio_write(node->indirect_ptr[blk], b) ;
			bio_read(temp, b) ;
			w = temp ; 

		} else {

			if (node->direct_ptr[blk] == 0) {

				node->direct_ptr[blk] = get_avail_blkno() ;
				if (blk < 15) {

					node->direct_ptr[blk + 1] = 0 ;

				}
				struct inode * newBlock = (struct inode *)malloc(BLOCK_SIZE) ;
				bio_write(node->direct_ptr[blk], newBlock) ;
				node->vstat.st_blocks++ ;
				free(newBlock) ;

			}

			bio_read(node->direct_ptr[blk], b) ;
			w = node->direct_ptr[blk] ;

		}

		char * a = b ;
		int j = 0 ;
		
		for ( ; i < (offset + size) ; i++) {

			*b = *buffer ;
			node->size++ ;
			node->vstat.st_size++ ;
			b++ ;
			j++ ;
			buffer++ ;
			bytesWritten++ ;
			time(&node->vstat.st_mtime) ;

			if (j >= BLOCK_SIZE) {

				break ;

			}

		}
		
		bio_write(w, a) ;
		b = a ;

	}

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk
	writei(node->ino, node) ;

	// Note: this function should return the amount of bytes you write to disk

	//printf("TFS WRITE COMPLETE\n") ;
	free(node) ;
	return bytesWritten;
}

static int tfs_unlink(const char *path) {

	//printf("UNLINK CALLED\n") ;

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char * directoryPath = (char *)malloc(strlen(path) + 1) ;
	strcpy(directoryPath, path) ;
	dirname(directoryPath) ;
	//printf("dirPath: %s\n", directoryPath) ;
	char * baseName = (char *)malloc(strlen(path) + 1) ;
	strcpy(baseName, path) ;
	//printf("Before baseName %s\n", baseName) ;
	baseName = basename(baseName) ;
	//printf("baseName: %s\n", baseName) ;

	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode * target = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(path, 0, target) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 3: Clear data block bitmap of target file
	int i ;

	for (i = 0 ; i < 8 ; i++) { // Large file support

		if (target->indirect_ptr[i] == 0) {

			break ;

		}

		unset_bitmap(blknoBitmap, target->indirect_ptr[i] - superblock->d_start_blk) ;
		target->indirect_ptr[i] = 0 ;

		int j ;
		int * a = (int *)malloc(BLOCK_SIZE) ;
		bio_read(target->indirect_ptr[i], a) ;

		for (j = 0 ; j < (BLOCK_SIZE / sizeof(int)) ; j++) {

			if (*a == 1) {

				unset_bitmap(blknoBitmap, *a - superblock->d_start_blk) ;

			} else {

				break ;

			}

			a++ ;

		}

	}

	for (i = 0 ; i < 16 ; i++) {

		if (target->direct_ptr[i] ==  0) {

			break ;

		}

		unset_bitmap(blknoBitmap, target->direct_ptr[i] - superblock->d_start_blk) ; 
		target->direct_ptr[i] = 0 ;

	}

	bio_write(superblock->d_bitmap_blk, blknoBitmap) ;

	// Step 4: Clear inode bitmap and its data block
	target->valid = 0 ;
	unset_bitmap(inoBitmap, target->ino) ;
	bio_write(superblock->i_bitmap_blk, inoBitmap) ;
	writei(target->ino, target) ;
	free(target) ;

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode * parent = (struct inode *)malloc(sizeof(struct inode)) ;
	if (get_node_by_path(directoryPath, 0, parent) != 0) {

		return -ENOENT ; // “No such file or directory.”

	}

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
	dir_remove(*parent, baseName, strlen(baseName)) ;

	//printf("UNLINK FINISHED\n") ;

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

