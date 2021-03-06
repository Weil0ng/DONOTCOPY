/*
 * This is the virtual directory interface header.  It defines the basic functions for
 * manipulating files and directories
 * by Jon
 */

#include "Directory.h"
#include "FileSystem.h"
#include "sys/stat.h"
#include "sys/types.h"

// mounts a filesystem from a device
INT l2_mount(FileSystem* fs);

// unmounts a filesystem into a device
INT l2_unmount(FileSystem* fs);

// makes a new filesystem with a root directory
INT l2_initfs(LONG nDBlks, UINT nINodes, FileSystem* fs);

// getattr
INT l2_getattr(FileSystem* fs, char *path, struct stat *stbuf);

// makes a new directory
INT l2_mkdir(FileSystem* fs, char* path, uid_t uid, gid_t gid);

// makes a new file
INT l2_mknod(FileSystem* fs, char* path, uid_t uid, gid_t gid);

// reads directory contents
INT l2_readdir(FileSystem* fs, char* path, LONG offset, DirEntry* curEntry);
//UINT readdir(Dir*, DFile*);

// deletes a file or directory
INT l2_unlink(FileSystem* fs, char* path);

// mv
INT l2_rename(FileSystem* fs, char* path, char* new_path);

// chmod
INT l2_chmod(FileSystem* fs, char* path, mode_t mode);

// truncate
INT l2_truncate(FileSystem* fs, char* path, INT new_length);

// opens a file
INT l2_open(FileSystem* fs, char* path, enum FILE_OP fileOp);

// closes a file
INT l2_close(FileSystem* fs, char* path, enum FILE_OP fileOp);

// reads a file
INT l2_read(FileSystem* fs, char* path, LONG offset, BYTE* buf, LONG numBytes);

// writes to a file
INT l2_write(FileSystem* fs, char* path, LONG offset, BYTE* buf, LONG numBytes);

// updates the mod/access time of a file
INT l2_utimens(FileSystem* fs, char* path, struct timespec tv[2]);

//resolve path to inode id
INT l2_namei(FileSystem *, char *);
