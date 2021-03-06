/*
 * This is the disk emulator header file. The emulator uses a big one-dimentional array
 * to abstract the logical blocks of a disk.
 * by Weilong
 */

#include "Globals.h"
#include <sys/stat.h>
#include <fcntl.h>

typedef struct
{
  //# of total disk space in bytes
  LONG _dsk_size;

  //# of total blocks on this disk
  UINT _dsk_numBlk;
  
  //The actual array of the disk
  INT _dsk_dskArray;
} DiskArray;

//opens a disk arrary from file
void openDisk(DiskArray *, LONG);

//initialize a disk array in memory
//args: device,
//      size
void initDisk(DiskArray *, LONG);

//destroy the in-memory disk array
void closeDisk(DiskArray *);

//convert block id to disk array offset
LONG bid2Offset(LONG);

//read a block from logical id i
//args: device, 
//      block id
INT readBlk(DiskArray *, LONG, BYTE *);

//write to a block of logical id i
//args: device, 
//      block id, 
//      content buf
INT writeBlk(DiskArray *, LONG, BYTE *);

#ifdef DEBUG
//dump the disk to a per block file
void dumpDisk();
#endif

#ifdef DEBUG
//prints the disk for debugging
void printDisk(DiskArray *);
#endif

#ifdef DEBUG
//prints a block for debugging
void printBlk(DiskArray *, UINT);
#endif
