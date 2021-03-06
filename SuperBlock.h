/*
 * This is the struct of superblock
 * by Weilong
*/

#include "Globals.h"

typedef struct SuperBlock
{
  /* on disk fields */

  //# of total data blocks in this file system
  LONG nDBlks;

  //# of free data blocks in this file system
  LONG nFreeDBlks;
  
  //logical id of currently cached free list block
  LONG pFreeDBlksHead;

  //index of the next free data block in the free data block list
  LONG pNextFreeDBlk;
 
  //# of total inodes in this file system
  UINT nINodes;

  //# of free inodes in this file system
  UINT nFreeINodes;

  //free inode list cache
  INT freeINodeCache[FREE_INODE_CACHE_SIZE];  

  //index of the next free inode in the free inode list
  INT pNextFreeINode;

  //INode ID for root dir
  UINT rootINodeID;

  /* in-memory fields */

  //SuperBlock cache of free data block list
  LONG freeDBlkCache[FREE_DBLK_CACHE_SIZE];

  //Modified bit
  BOOL modified;

  //Lock for synchronization

} SuperBlock;

// helper structure, used only to write superblocks to disk
typedef struct DSuperBlock
{
  LONG nDBlks;
  LONG nFreeDBlks;
  LONG pFreeDBlksHead;
  LONG pNextFreeDBlk;

  UINT nINodes;
  UINT nFreeINodes;
  INT freeINodeCache[FREE_INODE_CACHE_SIZE];
  UINT pNextFreeINode;
  
  UINT rootINodeID;

} DSuperBlock;

// writes disk fields of superblock into a block-sized buffer
// note: exactly BLK_SIZE bytes of memory must be allocated for buf
INT blockify(SuperBlock*, BYTE* buf);

// reads disk fields of superblock from a block-sized disk buffer to core
// note: this will not initialize the free data block cache, that must be loaded manually
INT unblockify(BYTE* buf, SuperBlock*);

#ifdef DEBUG
// prints out a superblock for debugging
void printSuperBlock(SuperBlock*);
#endif

#ifdef DEBUG
// prints out the free inode cache for debugging
void printFreeINodeCache(SuperBlock*);
#endif

#ifdef DEBUG
// prints out the free data block cache for debugging
void printFreeDBlkCache(SuperBlock*);
#endif
