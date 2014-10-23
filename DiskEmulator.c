/*
 * This is the disk emulator implementation file.
 * by Weilong
 */

#include "DiskEmulator.h"
#include "Utility.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
void initDisk(DiskArray *disk, UINT diskSize)
{
  FILE *mapFile;
  mapFile = fopen("diskFile", "w+r");
  if (mapFile == NULL)
  {
        printf("Disk map file open failed!\n");
	exit(1);
  }
  //truncate the file to the right size
  ftruncate(fileno(mapFile), diskSize);
  
  //create the memory map
  disk->_dsk_dskArray = mmap(NULL, diskSize, PROT_READ|PROT_WRITE, MAP_SHARED, fileno(mapFile), 0);

  //no need for file stream anymore
  fclose(mapFile);

  if (disk->_dsk_dskArray == MAP_FAILED)
  {
	printf("mmap failed!\n");
	exit(1);
  }

  memset(disk->_dsk_dskArray, 0, diskSize);
  disk->_dsk_size = diskSize;
  disk->_dsk_numBlk = diskSize / BLK_SIZE;
}

void destroyDisk(DiskArray *disk)
{
  //Sync operation force consistancy of the mapped file
  msync(disk->_dsk_dskArray, disk->_dsk_size, MS_SYNC);
  munmap(disk->_dsk_dskArray, disk->_dsk_size);
}

UINT bid2Offset(UINT bid)
{
  return bid * BLK_SIZE;
}

UINT readBlk(DiskArray *disk, UINT bid, BYTE *buf)
{
  if (bid > disk->_dsk_numBlk - 1) {
    _err_last = _dsk_readOutOfBoundry;
    THROW();
    return -1;
  }
  UINT offset = bid2Offset(bid);
  for (UINT i=0; i<BLK_SIZE; i++)
    *(buf + i) = *(disk->_dsk_dskArray + offset + i);
  return 0;
}

UINT writeBlk(DiskArray *disk, UINT bid, BYTE *buf)
{
  if (bid > disk->_dsk_numBlk - 1) {
    _err_last = _dsk_writeOutOfBoundry;
    THROW();
    return -1;
  }
  UINT offset = bid2Offset(bid);
  for (UINT i=0; i<BLK_SIZE; i++)
    *(disk->_dsk_dskArray + offset + i) = *(buf + i);
  return 0;
}
