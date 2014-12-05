/**
 * FileSystem implementation
 * by Jon
 */

#include "FileSystem.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

INT makefs(UINT nDBlks, UINT nINodes, FileSystem* fs) {
    #ifdef DEBUG 
    printf("makefs(%d, %d, %p)\n", nDBlks, nINodes, (void*) fs); 
    #endif
    
    //validate file system parameters
    #ifndef DEBUG
    if(nDBlks <= 0 || nINodes <= 0) {
        fprintf(stderr, "Error: must have a positive number of data blocks/inodes!\n");
        return 1;
    }
    else if(nDBlks < FREE_DBLK_CACHE_SIZE) {
        fprintf(stderr, "Error: must have at least %d data blocks!\n", FREE_DBLK_CACHE_SIZE);
        return 1;
    }
    else if(nINodes < FREE_INODE_CACHE_SIZE) {
        fprintf(stderr, "Error: must have at least %d inodes!\n", FREE_INODE_CACHE_SIZE);
        return 1;
    }
    else if(nINodes % INODES_PER_BLK != 0) {
        fprintf(stderr, "Error: inodes must divide evenly into blocks!\n");
        return 1;
    }
    #endif

    //compute file system size 
    LONG nBytes = BLK_SIZE + nINodes * INODE_SIZE + nDBlks * BLK_SIZE;
    #ifdef DEBUG 
    printf("Computing file system size...nBytes = %d\n", nBytes); 
    #endif
    if(nBytes > MAX_FS_SIZE) {
        fprintf(stderr, "Error: file system size %d exceeds max allowed size %d!\n", nBytes, MAX_FS_SIZE);
        return 1;
    }
    //fs->nBytes = (BLK_SIZE + nINodes * INODE_SIZE + nDBlks * BLK_SIZE);
    
    fs->nBytes = nBytes;
    //compute offsets for inode/data blocks
    fs->diskINodeBlkOffset = SUPERBLOCK_OFFSET + 1;
    fs->diskDBlkOffset = fs->diskINodeBlkOffset + nINodes / INODES_PER_BLK;

    //initialize in-memory superblock
    #ifdef DEBUG 
    printf("Initializing in-memory superblock...\n"); 
    #endif
    fs->superblock.nDBlks = nDBlks;
    fs->superblock.nFreeDBlks = nDBlks;
    fs->superblock.pFreeDBlksHead = 0;
    fs->superblock.pNextFreeDBlk = FREE_DBLK_CACHE_SIZE - 1;
    
    fs->superblock.nINodes = nINodes;
    fs->superblock.nFreeINodes = nINodes;
    fs->superblock.pNextFreeINode = fs->superblock.nINodes >= FREE_INODE_CACHE_SIZE
            ? FREE_INODE_CACHE_SIZE - 1 : fs->superblock.nINodes - 1;

    fs->superblock.modified = true;

    //initialize superblock free inode cache
    #ifdef DEBUG 
    printf("Initializing superblock free inode cache...\n");
    #endif
    if(fs->superblock.nINodes < FREE_INODE_CACHE_SIZE) {
        //special case: not enough inodes to fill cache
        fprintf(stderr, "Warning: %d inodes do not fill cache of size %d!\n", fs->superblock.nINodes, FREE_INODE_CACHE_SIZE);
        for(UINT i = 0; i < fs->superblock.nINodes; i++) {
            fs->superblock.freeINodeCache[i] = i;
        }
        for(UINT i = fs->superblock.nINodes; i < FREE_INODE_CACHE_SIZE; i++) {
            fs->superblock.freeINodeCache[i] = -1;
        }
    }
    else {
        //typical case: fill cache with first inodes
        for(UINT i = 0; i < FREE_INODE_CACHE_SIZE; i++) {
            fs->superblock.freeINodeCache[i] = i;
        }
    }
    
    //initialize the disk
    #ifdef DEBUG 
    printf("Initializing disk emulator...\n"); 
    #endif
    fs->disk = malloc(sizeof(DiskArray));
    initDisk(fs->disk, fs->nBytes);

    //create inode list on disk
    #ifdef DEBUG 
    printf("Creating disk inode list...\n"); 
    #endif
    UINT nextINodeId = 0;
    UINT nextINodeBlkId = fs->diskINodeBlkOffset;
    BYTE nextINodeBlkBuf[INODES_PER_BLK * INODE_SIZE]; //note that this zeroes out the buffer

    while(nextINodeBlkId < fs->diskDBlkOffset) {
        //fill inode blocks one at a time
        for(UINT i = 0; i < INODES_PER_BLK; i++) {
            INode* inode = (INode*) &nextINodeBlkBuf[i * INODE_SIZE];
            initializeINode(inode, nextINodeId++);
            inode->_in_type = FREE;
        }

        writeBlk(fs->disk, nextINodeBlkId, nextINodeBlkBuf);

        nextINodeBlkId++;
    }

    //create free block list on disk
    #ifdef DEBUG 
    printf("Creating disk free block list...\n"); 
    #endif
    UINT nextListBlk = 0;
    UINT freeDBlkList[FREE_DBLK_CACHE_SIZE];
    
    while(nextListBlk < fs->superblock.nDBlks) {
        if(fs->superblock.nDBlks < nextListBlk + FREE_DBLK_CACHE_SIZE) {
            //special case: not enough data blocks to fill another cache block
            fprintf(stderr, "Warning: %d data blocks do not divide evenly into caches of size %d!\n", fs->superblock.nDBlks, FREE_DBLK_CACHE_SIZE);
            UINT remaining = fs->superblock.nDBlks - nextListBlk;
            UINT offset = FREE_DBLK_CACHE_SIZE - remaining;
            for(int i = 0; i < offset; i++) {
                freeDBlkList[i] = -1;
            }
            for(int i = offset; i < FREE_DBLK_CACHE_SIZE; i++) {
                freeDBlkList[i] = nextListBlk + i - offset;
            }
        }
        else if(fs->superblock.nDBlks == nextListBlk + FREE_DBLK_CACHE_SIZE) {
            //special case: exactly enough data blocks to fill last cache block
            for(UINT i = 0; i < FREE_DBLK_CACHE_SIZE; i++) {
                freeDBlkList[i] = nextListBlk + i;
            }
        }
        else {
            //typical case: fill next cache block
            //next head pointer goes in first entry
            freeDBlkList[0] = nextListBlk + FREE_DBLK_CACHE_SIZE;

            //rest of free blocks are enumerated in order
            for(UINT i = 1; i < FREE_DBLK_CACHE_SIZE; i++) {
                freeDBlkList[i] = nextListBlk + i;
            }
        }

        //write completed block and advance to next head
        writeDBlk(fs, nextListBlk, (BYTE*) freeDBlkList);
        nextListBlk += FREE_DBLK_CACHE_SIZE;
    }
    
    //load free block cache into superblock
    readDBlk(fs, fs->superblock.pFreeDBlksHead, (BYTE*) (fs->superblock.freeDBlkCache));

    //write superblock to disk
    #ifdef DEBUG 
    printf("Writing superblock to disk...\n"); 
    #endif
    BYTE superblockBuf[BLK_SIZE];
    blockify(&fs->superblock, superblockBuf);
    writeBlk(fs->disk, SUPERBLOCK_OFFSET, superblockBuf);
    fs->superblock.modified = false;

    return 0;
}

INT closefs(FileSystem* fs) {
    closeDisk(fs->disk);
    free(fs->disk);
    return 0;
}

//input: none
//output: INode id
//function: allocate a free inode
INT allocINode(FileSystem* fs, INode* inode) {

    if(fs->superblock.nFreeINodes == 0) {
        fprintf(stderr, "Error: no more free inodes available!\n");
        return -1;
    }

    INT nextFreeINodeID;

    // the inode cache is empty
    if(fs->superblock.pNextFreeINode < 0) {
    
        #ifdef DEBUG
        printf("INode cache empty, scanning disk for more free inodes\n");
        #endif
        
        // scan inode list and fill the inode cache list to capacity
        UINT nextINodeBlk = fs->diskINodeBlkOffset;
        BYTE nextINodeBlkBuf[BLK_SIZE];
        
        UINT k = 0; // index in inode cache 
        BOOL FULL = false;
        while(nextINodeBlk < fs->diskDBlkOffset && !FULL)  {
            
            // read the whole block out into a byte array
            readBlk(fs->disk, nextINodeBlk, nextINodeBlkBuf);

            // check inodes one at a time
            for(UINT i = 0; i < INODES_PER_BLK && !FULL; i++) {
                
                // cast the byte array to INode structures
                INode* inode_d = (INode*) (nextINodeBlkBuf + i*INODE_SIZE);

                // found a free inode
                if(inode_d->_in_type == FREE) {
                    fs->superblock.freeINodeCache[k] = (nextINodeBlk - fs->diskINodeBlkOffset) * INODES_PER_BLK + i;
                    k ++;
                        
                    fs->superblock.pNextFreeINode ++;

                    // inode cache full
                    if(k == FREE_INODE_CACHE_SIZE) {
                        FULL = true;
                    }
                }
            }
            
            nextINodeBlk ++;
        }
    }
   
    // the inode cache not emply, allocate one inode from the cache
    nextFreeINodeID = fs->superblock.freeINodeCache[fs->superblock.pNextFreeINode]; 
    
    // initialize the inode
    initializeINode(inode, nextFreeINodeID);
    
    // write the inode back to disk
    if(writeINode(fs, nextFreeINodeID, inode) == -1){
        fprintf(stderr, "error: write inode %d to disk\n", nextFreeINodeID);
        return -1;
    }
    
    // update the inode cache list and stack pointer
    fs->superblock.freeINodeCache[fs->superblock.pNextFreeINode] = -1;
    fs->superblock.pNextFreeINode --;

    // decrement the free inodes count
    fs->superblock.nFreeINodes --;

    return nextFreeINodeID;

}

// input: inode number
// output: none
// function: free a inode, which updates the inode cache and/or inode table
INT freeINode(FileSystem* fs, UINT id) {
    assert(id < fs->superblock.nINodes);

    // if inode cache not full, store the inode number in the list
    if (fs->superblock.pNextFreeINode < FREE_INODE_CACHE_SIZE - 1){
        fs->superblock.freeINodeCache[fs->superblock.pNextFreeINode + 1] = id;
        fs->superblock.pNextFreeINode ++;
    }

    // update the inode table to mark the inode free
    INode inode;

    if(readINode(fs, id, &inode) == -1){
        fprintf(stderr, "error: read inode %d from disk\n", id);
        return -1;
    }
   
    // free all data blocks associated with this inode 
    for (UINT i = 0; i < INODE_NUM_DIRECT_BLKS; i ++) {
        if (inode._in_directBlocks[i] != -1) {
            freeDBlk(fs,inode._in_directBlocks[i]);
        }
    }
    for (UINT i = 0; i < INODE_NUM_S_INDIRECT_BLKS; i ++) {
        if (inode._in_sIndirectBlocks[i] != -1) {
            INT buf[BLK_SIZE/sizeof(INT)];
            readDBlk(fs, inode._in_sIndirectBlocks[i], (BYTE *)buf);
            for (UINT j = 0; j < (BLK_SIZE / sizeof(INT)); j ++) {
                // free the actual data blocks
                if(buf[j] != -1)
                    freeDBlk(fs, buf[j]);
            }
            // free the indirect data block
            freeDBlk(fs, inode._in_sIndirectBlocks[i]);
        }
    }
    for (UINT i = 0; i < INODE_NUM_D_INDIRECT_BLKS; i ++) {
        if(inode._in_dIndirectBlocks[i] != -1) {
            INT buf_d[BLK_SIZE/sizeof(INT)];
            readDBlk(fs, inode._in_dIndirectBlocks[i], (BYTE*)buf_d);
            for (UINT j = 0; j < (BLK_SIZE / sizeof(INT)); j ++) {
                if(buf_d[j] != -1) {
                    INT buf_s[BLK_SIZE/sizeof(INT)];
                    readDBlk(fs, buf_d[j], (BYTE*) buf_s);
                    for (UINT k = 0; k < (BLK_SIZE / sizeof(INT)); k ++) {
                        // free the actual data blocks
                        if(buf_s[k] != -1)
                            freeDBlk(fs, buf_s[k]);
                    }
                    // free the single indirect blocks
                    freeDBlk(fs, buf_d[j]);
                }
            }
            // free the double indirect blocks
            freeDBlk(fs, inode._in_dIndirectBlocks[i]);
        }
    }
    for (UINT i = 0; i < INODE_NUM_T_INDIRECT_BLKS; i ++) {
        if(inode._in_tIndirectBlocks[i] != -1) {
            INT buf_t[BLK_SIZE/sizeof(INT)];
            readDBlk(fs, inode._in_tIndirectBlocks[i], (BYTE*)buf_t);
            for (UINT j = 0; j < (BLK_SIZE / sizeof(INT)); j ++) {
                if(buf_t[j] != -1) {
                    INT buf_d[BLK_SIZE/sizeof(INT)];
                    readDBlk(fs, buf_t[j], (BYTE*) buf_d);
                    for (UINT k = 0; k < (BLK_SIZE / sizeof(INT)); k ++) {
                        // free the actual data blocks
                        if(buf_d[k] != -1) {
                            INT buf_s[BLK_SIZE/sizeof(INT)];
                            readDBlk(fs, buf_d[k], (BYTE*) buf_s);
                            for (UINT m = 0; m < (BLK_SIZE / sizeof(INT)); m ++) {
                                // free the actual data blocks
                                if(buf_s[m] != -1)
                                    freeDBlk(fs, buf_s[m]);
                            }
                            // free the single indirect block
                            freeDBlk(fs, buf_d[k]);
                        }
                    }
                    // free the double indirect blocks
                    freeDBlk(fs, buf_t[j]);
                }
            }
            // free the triple indirect blocks
            freeDBlk(fs, inode._in_tIndirectBlocks[i]);
        }
    }

    initializeINode(&inode, id);
    inode._in_type = FREE;

    
    if(writeINode(fs, id, &inode) == -1){
        fprintf(stderr, "error: write inode %d to disk\n", id);
        return -1;
    }
    
    // increase file system free inode count
    fs->superblock.nFreeINodes ++;

    return 0;

}

// input: inode number
// output: the pointer to the inode
// function: read a disk inode
INT readINode(FileSystem* fs, UINT id, INode* inode) {
    assert(id < fs->superblock.nINodes);
    UINT blk_num = fs->diskINodeBlkOffset + id / INODES_PER_BLK;
    UINT blk_offset = id % INODES_PER_BLK;

    BYTE INodeBlkBuf[BLK_SIZE];
    if(readBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: read blk %d from disk\n", blk_num);
        return -1;
    }

    // find the inode to read
    INode* inode_d = (INode*) (INodeBlkBuf + blk_offset * INODE_SIZE);
    
    // asssign struct field from the buffer to the inode
    inode->_in_type = inode_d->_in_type;
    strcpy(inode->_in_owner, inode_d->_in_owner);
    inode->_in_uid = inode_d->_in_uid;
    inode->_in_gid = inode_d->_in_gid;
    inode->_in_permissions = inode_d->_in_permissions;
    inode->_in_modtime =  inode_d->_in_modtime;
    inode->_in_accesstime = inode_d->_in_accesstime;
    inode->_in_filesize = inode_d->_in_filesize;
    inode->_in_linkcount = inode_d->_in_linkcount;
    
    for (UINT i = 0; i < INODE_NUM_DIRECT_BLKS; i ++) {
        inode->_in_directBlocks[i] = inode_d->_in_directBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_S_INDIRECT_BLKS; i ++) {
        inode->_in_sIndirectBlocks[i] = inode_d->_in_sIndirectBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_D_INDIRECT_BLKS; i ++) {
        inode->_in_dIndirectBlocks[i] = inode_d->_in_dIndirectBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_T_INDIRECT_BLKS; i ++) {
        inode->_in_tIndirectBlocks[i] = inode_d->_in_tIndirectBlocks[i];
    }

    return 0;
}

INT readINodeData(FileSystem* fs, INode* inode, BYTE* buf, UINT offset, UINT len) {
    #ifdef DEBUG
    printf("readINodeData on inode %d for len %d at offset %d\n", inode->_in_filesize, len, offset);
    #endif
    
    #ifdef DEBUG
    assert(offset <= inode->_in_filesize);
    #else
    if(offset >= inode->_in_filesize) {
        fprintf(stderr, "Error: readINodeData offset %d with length %d exceeds file size %d!\n", offset, len, inode->_in_filesize);
        return -1;
    }
    #endif
    
    //truncate len if the read goes beyond the filesize
    if(offset + len > inode->_in_filesize) {
        len = inode->_in_filesize - offset;
    }
    
    //compute the number of file blocks allocated based on the file size
    UINT nFileBlks = (inode->_in_filesize + BLK_SIZE - 1) / BLK_SIZE;
    #ifdef DEBUG
    //printf("Total number of file blocks allocated in inode: %d\n", nFileBlks);
    printf("Current file size of inode: %d\n", inode->_in_filesize);
    printf("Total number of file blocks allocated in inode: %d\n", nFileBlks);
    #endif
    
    //convert byte offset to logical id + block offset
    UINT fileBlkId = offset / BLK_SIZE;
    offset = offset % BLK_SIZE;
    #ifdef DEBUG
    printf("Starting at block %d with block offset %d with truncated len %d\n", fileBlkId, offset, len);
    #endif
    
    //return bytes read upon completion
    UINT bytesRead = 0;
    
    //compute start block id
    INT dataBlkId = bmap(fs, inode, fileBlkId);
    if (dataBlkId < 0) {
	printf("reading from 0 length file!\n");
	return 0;
    }
    
    //special case to handle offset in the middle of first block
    if(offset > 0) {
        if(offset + len <= BLK_SIZE) {
            //entire read falls within first block
            readDBlkOffset(fs, dataBlkId, buf, offset, len);
            bytesRead = len;
            len = 0;
        }
        else {
            //read is larger than first block
            readDBlkOffset(fs, dataBlkId, buf, offset, BLK_SIZE - offset);
            bytesRead = BLK_SIZE - offset;
            len -= bytesRead;
            fileBlkId++;
        }
    }
    
    //continue while more bytes to read AND end of inode not reached
    while(len > 0 && fileBlkId < nFileBlks) {
        //compute next data block id using bmap
        dataBlkId = bmap(fs, inode, fileBlkId);
        #ifdef DEBUG
        assert(dataBlkId >= 0);
        #endif
            
        //read next block into buf
        if(len <= BLK_SIZE) {
            //end of read falls within block
            #ifdef DEBUG
            printf("Reached last dblk, calling readDBlkOffset on id %d with offset %d for len %d\n", dataBlkId, 0, len);
            #endif
            readDBlkOffset(fs, dataBlkId, buf + bytesRead, 0, len);
            
            //update len (end of read)
            bytesRead += len;
            len = 0;
        }
        else {
            //read is larger than current block
            #ifdef DEBUG
            printf("Not last dblk, calling readDBlk on id %d\n", dataBlkId);
            #endif
            readDBlk(fs, dataBlkId, buf + bytesRead);
           
            //update len and file block for remaining read
            bytesRead += BLK_SIZE;
            len -= BLK_SIZE;
            fileBlkId++;
        }
    }
    
    #ifdef DEBUG
    printf("readINodeData successfully read %d bytes\n", bytesRead);
    printf("Returning buf: %s\n", buf);
    #endif
    return bytesRead;
}

// input: inode number id, an inode
// output: none
// function: write the disk inode #id in the inode table
INT writeINode(FileSystem* fs, UINT id, INode* inode) {
    assert(id < fs->superblock.nINodes);
    UINT blk_num = fs->diskINodeBlkOffset + id / INODES_PER_BLK;
    UINT blk_offset = id % INODES_PER_BLK;
    
    BYTE INodeBlkBuf[BLK_SIZE];
    if(readBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: read blk %d from disk\n", blk_num);
        return -1;
    }
    
    //INode* inode_s = (INode*) INodeBlkBuf; 
    // find the inode to write
    INode *inode_d = (INode*) (INodeBlkBuf + blk_offset * INODE_SIZE);

    // replace the inode
    inode_d->_in_type = inode->_in_type;
    strcpy(inode_d->_in_owner, inode->_in_owner);
    inode_d->_in_permissions = inode->_in_permissions;
    inode_d->_in_modtime =  inode->_in_modtime;
    inode_d->_in_accesstime = inode->_in_accesstime;
    inode_d->_in_filesize = inode->_in_filesize;
    inode_d->_in_uid = inode->_in_uid;
    inode_d->_in_gid = inode->_in_gid;
    inode_d->_in_linkcount = inode->_in_linkcount;

    for (UINT i = 0; i < INODE_NUM_DIRECT_BLKS; i ++) {
        inode_d->_in_directBlocks[i] = inode->_in_directBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_S_INDIRECT_BLKS; i ++) {
        inode_d->_in_sIndirectBlocks[i] = inode->_in_sIndirectBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_D_INDIRECT_BLKS; i ++) {
        inode_d->_in_dIndirectBlocks[i] = inode->_in_dIndirectBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_T_INDIRECT_BLKS; i ++) {
        inode_d->_in_tIndirectBlocks[i] = inode->_in_tIndirectBlocks[i];
    }

    // write the entire inode block back to disk
    if(writeBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: write blk %d from disk\n", blk_num);
        return -1;
    }
    
    return 0;
}

INT writeINodeData(FileSystem* fs, INode* inode, BYTE* buf, UINT offset, UINT len) {
    #ifdef DEBUG
    printf("writeINodeData on inode of size %d with offset %d for len %d\n", inode->_in_filesize, offset, len);
    #endif
    assert(offset <= MAX_FILE_SIZE);
    assert(offset + len <= MAX_FILE_SIZE);
    
    //convert byte offset to logical id + block offset
    UINT fileBlkId = offset / BLK_SIZE;
    offset = offset % BLK_SIZE;
    #ifdef DEBUG
    printf("writeINodeData starting at file block %d with offset %d\n", fileBlkId, offset);
    #endif
    
    //return bytes written upon completion
    UINT bytesWritten = 0;    
    
    //compute start block id
    //TODO merge this into one line by making balloc return the ID
    INT dataBlkId = balloc(fs, inode, fileBlkId);
    #ifdef DEBUG
    printf("Starting data block: %d\n", dataBlkId);
    #endif
    if(dataBlkId < 0) {
        printf("Warning: could not allocate more data blocks for write!\n");
        return bytesWritten;
    }
    
    //special case to handle offset in the middle of first block
    if(offset > 0) {
        if(offset + len <= BLK_SIZE) {
            //entire write falls within first block
            writeDBlkOffset(fs, dataBlkId, buf, offset, len);
            bytesWritten = len;
            len = 0;
        }
        else {
            //write is larger than first block
            writeDBlkOffset(fs, dataBlkId, buf, offset, BLK_SIZE - offset);
            bytesWritten = BLK_SIZE - offset;
            len -= bytesWritten;
            fileBlkId++;
        }
    }
    
    //continue while more bytes to write AND max filesize not reached
    while(len > 0 && fileBlkId < MAX_FILE_BLKS) {
        printf("allocate data block for fileblkid %d\n", fileBlkId);
        //compute next data block id using balloc
        dataBlkId = balloc(fs, inode, fileBlkId);
        #ifdef DEBUG
        printf("Next data block: %d\n", dataBlkId);
        #endif

        if(dataBlkId < 0) {
            printf("Warning: could not allocate more data blocks for write!\n");
            return bytesWritten;
        }
        
        //write next block from buf
        if(len <= BLK_SIZE) {
            //end of write falls within block
            writeDBlkOffset(fs, dataBlkId, buf + bytesWritten, 0, len);
            
            //update len (end of write)
            bytesWritten += len;
            len = 0;
        }
        else {
            //write is larger than current block
            writeDBlk(fs, dataBlkId, buf + bytesWritten);
           
            //update len and file block for remaining write
            bytesWritten += BLK_SIZE;
            len -= BLK_SIZE;
            fileBlkId++;
        }
    }
    
    return bytesWritten;
}

//Try to alloc a free data block from disk:
//1. check if there are free DBlk at all
//2. check the pNextFreeDBlk
//  if DBlk cache has free entry 
//      alloc that entry
//  else
//      alloc current free list blk
//      move pFreeDBlksHead to next list blk
//3. # free blocks --
//4. return the logical id of allocaed DBlk
INT allocDBlk(FileSystem* fs) {
    //1. check full
    if (fs->superblock.nFreeDBlks == 0) {
        _err_last = _fs_DBlkOutOfNumber;
        THROW(__FILE__, __LINE__, __func__);
        return -1;
    }
    
    INT returnID = -1;

    //2. check pNextFreeDBlk
    if (fs->superblock.pNextFreeDBlk != 0) {
        //alloc next block in cache
        returnID = (fs->superblock.freeDBlkCache)[fs->superblock.pNextFreeDBlk];
        // mark it as allocated
        (fs->superblock.freeDBlkCache)[fs->superblock.pNextFreeDBlk] = -1;
        fs->superblock.pNextFreeDBlk--;
    }
    else {
        //alloc this very block
        returnID = fs->superblock.pFreeDBlksHead;
        //retrieve next head and mark current block as allocated
        INT nextHead = (fs->superblock.freeDBlkCache)[0];
        //zero out before allocation
        for (UINT i=0; i<FREE_DBLK_CACHE_SIZE; i++) 	
          (fs->superblock.freeDBlkCache)[i] = 0;
        //wipe cache and write to disk
        writeDBlk(fs, fs->superblock.pFreeDBlksHead, (BYTE*) (fs->superblock.freeDBlkCache));

        //if we know more dblks are available on disk, load them into cache
        if(fs->superblock.nFreeDBlks > 1) {
            #ifdef DEBUG
            printf("Returning last free block, loading next cache from disk at id: %d\n", nextHead);
            #endif
            //move head in superblock
            fs->superblock.pFreeDBlksHead = nextHead;
            //load cache from next head
            readDBlk(fs, fs->superblock.pFreeDBlksHead, (BYTE*) (fs->superblock.freeDBlkCache));
            //reset stack pointer
            fs->superblock.pNextFreeDBlk = FREE_DBLK_CACHE_SIZE - 1;
        }
    }

    fs->superblock.nFreeDBlks --;
    INT initBlk[FREE_DBLK_CACHE_SIZE];
    memset(initBlk, -1, sizeof(initBlk));
    writeDBlk(fs, returnID, (BYTE *)initBlk);
    return returnID;
}

// Try to insert a free DBlk back to free list
// 1. check pNextFreeDBlk
// 2.   if current cache full
//      wrtie cache back
//      use newly free block NFB as head free block list
//  else
//      insert to current cache
// 3. # Free DBlks ++
INT freeDBlk(FileSystem* fs, UINT id) {
    assert(id < fs->superblock.nDBlks);

    // if no other blocks are free
    if (fs->superblock.nFreeDBlks == 0) {
        fs->superblock.pFreeDBlksHead = id;
        fs->superblock.freeDBlkCache[0] = id;
        fs->superblock.pNextFreeDBlk = 0;
    }
    // if current cache is full
    else if (fs->superblock.pNextFreeDBlk == FREE_DBLK_CACHE_SIZE - 1) {
        #ifdef DEBUG
        printf("DBlk cache full, dumping cache to disk at id: %d\n", fs->superblock.pFreeDBlksHead);
        #endif
        //write cache back
        writeDBlk(fs, fs->superblock.pFreeDBlksHead, (BYTE*) (fs->superblock.freeDBlkCache));
        //init cache
        (fs->superblock.freeDBlkCache)[0] = fs->superblock.pFreeDBlksHead;
        for (UINT i=1; i<FREE_DBLK_CACHE_SIZE; i++)
            (fs->superblock.freeDBlkCache)[i] = -1;
        //move pNextFreeDBlk
        fs->superblock.pNextFreeDBlk = 0;
        //move head
        fs->superblock.pFreeDBlksHead = id;
    }
    else {
        fs->superblock.pNextFreeDBlk ++;
        (fs->superblock.freeDBlkCache)[fs->superblock.pNextFreeDBlk] = id;
    }
    fs->superblock.nFreeDBlks ++;
    return 0;
}

// Try to read out a block from disk
// 1. convert logical id of DBlk to logical id of disk block
// 2. read data
INT readDBlk(FileSystem* fs, UINT id, BYTE* buf) {
    printf("readDBlk id: %u\n", id);
    assert(id < fs->superblock.nDBlks);
    UINT bid = id + fs->diskDBlkOffset;
    return readBlk(fs->disk, bid, buf);

}

// writes a data block to the disk
// dBlkId: the data block logical id (not raw logical id!)
// buf: the buffer to write (must be exactly block-sized)
INT writeDBlk(FileSystem* fs, UINT id, BYTE* buf) {
    assert(id < fs->superblock.nDBlks);
    UINT bid = id + fs->diskDBlkOffset;
    return writeBlk(fs->disk, bid, buf);
}

// input: the data block logical id, the offset into that block, length of
//        bytes to read in that block
// output: a buffer that contains len bytes
// function: read a data block with byte offset
INT readDBlkOffset(FileSystem* fs, UINT id, BYTE* buf, UINT off, UINT len) {
    assert(id < fs->superblock.nDBlks);
    assert(off < BLK_SIZE);
    assert(off + len <= BLK_SIZE);

    BYTE readBuf[BLK_SIZE];

    if (readDBlk(fs, id, readBuf) == -1) {
        fprintf(stderr, "In readDBlkOffset, fail to readDblk %d!\n", id);
        return -1;
    }
    
    memcpy(buf, readBuf + off, len);

    return 0;
}

// input: the data block logical id, the offset into that block, length of
//        bytes to write in that block
// output: the data block being updated
// function: read a data block with byte offset
INT writeDBlkOffset(FileSystem* fs, UINT id, BYTE* buf, UINT off, UINT len) {
    assert(id < fs->superblock.nDBlks);
    assert(off < BLK_SIZE);
    assert(off + len <= BLK_SIZE);

    BYTE writeBuf[BLK_SIZE];

    if (readDBlk(fs, id, writeBuf) == -1) {
        fprintf(stderr, "In writeDBlkOffset, fail to readDblk %d!\n", id);
        return -1;
    }

    memcpy(writeBuf + off, buf, len);

    if (writeDBlk(fs, id, writeBuf) == -1) {
        fprintf(stderr, "In writeDBlkOffset, fail to writeDblk %d!\n", id);
        return -1;
    }

    return 0;
}

// Functionality:
// 	map internal index of an inode to its DBlk id
// Errors:
// 	1. internal index out of range
// 	2. target block not allocated
// Steps:
// 	1. check direct range
// 	2. check single indirect range
// 	3. check double indirect range
// 	4. if reach here, out of range
INT bmap(FileSystem* fs, INode* inode, UINT fileBlkId) 
{
    INT DBlkID = -1;
    if (fileBlkId < INODE_NUM_DIRECT_BLKS) {
        DBlkID = inode->_in_directBlocks[fileBlkId];
        if (DBlkID == -1) {
	    _err_last = _in_NonAllocDBlk;
	    THROW(__FILE__, __LINE__, __func__);
	    return -1;
        }
	return DBlkID;
    }
    UINT entryNum = BLK_SIZE / sizeof (UINT);
    fileBlkId -= (INODE_NUM_DIRECT_BLKS);
    if (fileBlkId < INODE_NUM_S_INDIRECT_BLKS * entryNum) {
	UINT S_index = fileBlkId / entryNum;
	UINT S_offset = fileBlkId % entryNum;
        INT S_BlkID = inode -> _in_sIndirectBlocks[S_index];
	if (S_BlkID == -1) {
	    _err_last = _in_NonAllocIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
	    return -1;
	}
        INT blkBuf[entryNum];
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf);
	if (blkBuf[S_offset] == -1) {
	    _err_last = _in_NonAllocDBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
        }
        return *(blkBuf + S_offset);
    }
    UINT entryNumS = entryNum * entryNum;
    fileBlkId -= (INODE_NUM_S_INDIRECT_BLKS * entryNum);
    if (fileBlkId < INODE_NUM_D_INDIRECT_BLKS * entryNumS) {
        UINT D_index = fileBlkId / entryNumS;
	fileBlkId -= D_index * entryNumS;
	UINT S_Index = fileBlkId / entryNum;
	UINT S_offset = fileBlkId % entryNum;
        INT D_BlkID = inode -> _in_dIndirectBlocks[D_index];
        if (D_BlkID == -1) {
            _err_last = _in_NonAllocDIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
        }
	INT blkBuf[entryNum];
	readDBlk(fs, D_BlkID, (BYTE *)blkBuf);
	if (blkBuf[S_Index] == -1) {
	    _err_last = _in_NonAllocIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
	}
        INT S_BlkID = *(blkBuf + S_Index);
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf);
	if (blkBuf[S_offset] == -1) {
	     _err_last = _in_NonAllocDBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
	}
	return *(blkBuf + S_offset);
    }
    UINT entryNumD = entryNumS * entryNum;
    fileBlkId -= (INODE_NUM_D_INDIRECT_BLKS * entryNumS);
    if (fileBlkId < INODE_NUM_T_INDIRECT_BLKS * entryNumD) {
        UINT T_index = fileBlkId / entryNumD;
        fileBlkId -= T_index * entryNumD;
        UINT D_index = fileBlkId / entryNumS;
	fileBlkId -= D_index * entryNumS;
	UINT S_index = fileBlkId / entryNum;
	UINT S_offset = fileBlkId % entryNum;
        INT T_BlkID = inode -> _in_tIndirectBlocks[T_index];
        if (T_BlkID == -1) {
            _err_last = _in_NonAllocTIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
        }
	INT blkBuf[entryNum];
	readDBlk(fs, T_BlkID, (BYTE *)blkBuf);
	if (blkBuf[D_index] == -1) {
	    _err_last = _in_NonAllocDIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
	}
        INT D_BlkID = *(blkBuf + D_index);
	readDBlk(fs, D_BlkID, (BYTE *)blkBuf);
	if (blkBuf[S_index] == -1) {
	    _err_last = _in_NonAllocIndirectBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
	}
        INT S_BlkID = *(blkBuf + S_index);
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf);
	if (blkBuf[S_offset] == -1) {
	     _err_last = _in_NonAllocDBlk;
            THROW(__FILE__, __LINE__, __func__);
            return -1;
	}
	return *(blkBuf + S_offset);
    }

     _err_last = _in_IndexOutOfRange;
     THROW(__FILE__, __LINE__, __func__);
     return -1;
}

// Functionality:
//     expand inode directories till fileBlkId, leave "holes" if necessary, return DBlkID
// Errors:
//     1. disk full (catched by allocDBlk)
// Steps:
INT balloc(FileSystem *fs, INode* inode, UINT fileBlkId)
{
    printf("balloc called with %u\n", fileBlkId);
    UINT count = 0;
    // shortcut: check if already allocated
    INT DBlkID = bmap(fs, inode, fileBlkId);
    if (DBlkID !=  -1 )
        return DBlkID;
    printf("bmap done with %d\n", DBlkID);
    //UINT cur_internal_index = 0;
    //for (cur_internal_index=0; bmap(fs, inode, cur_internal_index) != -1 && cur_internal_index < fileBlkId; cur_internal_index ++);
    UINT cur_internal_index = fileBlkId;    

    INT newDBlkID = -1;
    BYTE blkBuf[BLK_SIZE];
    UINT entryNum = BLK_SIZE / sizeof(UINT);
    UINT entryNumS = entryNum * entryNum;
    UINT entryNumD = entryNumS * entryNum;

    // now, cur_internal_index holds the first unallocated entry
    while (cur_internal_index <= fileBlkId) {
        if (cur_internal_index < INODE_NUM_DIRECT_BLKS) {
	    // alloc the DBlk
	    if (cur_internal_index == fileBlkId) { //if this is the target writing block
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
	    }
	    else   //otherwise, creating holes
		newDBlkID = -1;
            inode->_in_directBlocks[cur_internal_index] = newDBlkID;
	    count ++;
	    cur_internal_index ++;
	}
	else if ((cur_internal_index - INODE_NUM_DIRECT_BLKS) < (INODE_NUM_S_INDIRECT_BLKS * entryNum)) {
	    UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS) / entryNum;
	    UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
	    // if this is the first alloc in this entry block, fisrt alloc the entry block
	    if (inode->_in_sIndirectBlocks[S_index] == -1) {
		newDBlkID = allocDBlk(fs);
	        if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
        	}
		inode->_in_sIndirectBlocks[S_index] = newDBlkID;
	    }
	    // now, alloc the DBlk
	    if (cur_internal_index ==fileBlkId) {
                newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
	    }
	    else
		newDBlkID = -1;
	    UINT S_DBlkID = inode->_in_sIndirectBlocks[S_index];
	    readDBlk(fs, S_DBlkID, blkBuf);
	    *((UINT *)blkBuf + S_offset) = newDBlkID;
	    writeDBlk(fs, S_DBlkID, blkBuf);
	    count ++;
	    cur_internal_index ++;
	    
	}
	else if ((cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) < (INODE_NUM_D_INDIRECT_BLKS * entryNumS)) {
	    UINT D_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) / entryNumS;
	    UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum - D_index * entryNumS) / entryNum;
	    //weilong: mathimatically, since mod, it does not matter if we substract N*entryNum
	    UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
            printf("D_Index: %u, S_Index: %u, S_offset: %u, cur_internal_index: %u\n", D_index, S_index, S_offset, cur_internal_index);
	    if (inode->_in_dIndirectBlocks[D_index] == -1) {
		newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		inode->_in_dIndirectBlocks[D_index] = newDBlkID;
	    }
	    UINT D_BlkID = inode->_in_dIndirectBlocks[D_index];
	    readDBlk(fs, D_BlkID, blkBuf);
	    if (*((UINT *)blkBuf + S_index) == -1) {
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		*((UINT *)blkBuf + S_index) = newDBlkID;
		writeDBlk(fs, D_BlkID, blkBuf);
	    }
            //now, alloc the DBlk
	    if (cur_internal_index == fileBlkId) {
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
	    }
	    else
		newDBlkID = -1;
	    UINT S_DBlkID = *((UINT *)blkBuf + S_index);
	    readDBlk(fs, S_DBlkID, blkBuf);
	    *((UINT *)blkBuf + S_offset) = newDBlkID;
	    writeDBlk(fs, S_DBlkID, blkBuf);
	    count ++;
	    cur_internal_index ++;
	}
	else if ((cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum - INODE_NUM_D_INDIRECT_BLKS * entryNumS) 
                < (INODE_NUM_T_INDIRECT_BLKS * entryNumD)) {
            UINT T_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                    - INODE_NUM_D_INDIRECT_BLKS * entryNumS) / entryNumD;
	    UINT D_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                    - INODE_NUM_D_INDIRECT_BLKS * entryNumS - T_index * entryNumD) / entryNumS;
            assert(D_index < entryNum);
	    UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                    - INODE_NUM_D_INDIRECT_BLKS * entryNumS - T_index * entryNumD - D_index * entryNumS) / entryNum;
            assert(S_index < entryNum);
	    UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
	    
            if (inode->_in_tIndirectBlocks[T_index] == -1) {
		newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		inode->_in_tIndirectBlocks[T_index] = newDBlkID;
	    }
	    UINT T_BlkID = inode->_in_tIndirectBlocks[T_index];
	    readDBlk(fs, T_BlkID, blkBuf);
	    if (*((UINT *)blkBuf + D_index) == -1) {
		newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		*((UINT *)blkBuf + D_index) = newDBlkID;
		writeDBlk(fs, T_BlkID, blkBuf);
	    }
	    UINT D_BlkID = *((UINT *)blkBuf + D_index);
	    readDBlk(fs, D_BlkID, blkBuf);
	    if (*((UINT *)blkBuf + S_index) == -1) {
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		*((UINT *)blkBuf + S_index) = newDBlkID;
		writeDBlk(fs, D_BlkID, blkBuf);
	    }
            //now, alloc the DBlk
	    if (cur_internal_index == fileBlkId) {
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
	    }
	    else
		newDBlkID = -1;
	    UINT S_DBlkID = *((UINT *)blkBuf + S_index);
	    readDBlk(fs, S_DBlkID, blkBuf);
	    *((UINT *)blkBuf + S_offset) = newDBlkID;
	    writeDBlk(fs, S_DBlkID, blkBuf);
	    count ++;
	    cur_internal_index ++;
	}
	else {
	    _err_last = _in_IndexOutOfRange;
	    THROW(__FILE__, __LINE__, __func__);
	    return -1;
	}
    }

#ifdef TRUNCATE_DEBUG
    printf("balloc allocated datablk %d\n", newDBlkID);
#endif 
    return newDBlkID;

}

// Functionality:
//     expand inode directories till fileBlkId, return DBlkID
// Errors:
//     1. disk full (catched by allocDBlk)
// Steps:
INT old_balloc(FileSystem *fs, INode* inode, UINT fileBlkId)
{
    UINT count = 0;
    // check if already allocated
    UINT DBlkID = bmap(fs, inode, fileBlkId);
    if (DBlkID !=  -1 )
        return DBlkID;
    
    UINT cur_internal_index = 0;
    for (cur_internal_index=0; bmap(fs, inode, cur_internal_index) != -1 && cur_internal_index < fileBlkId; cur_internal_index ++);
    
    UINT newDBlkID = -1;
    BYTE blkBuf[BLK_SIZE];
    UINT entryNum = BLK_SIZE / sizeof(UINT);
    UINT entryNumS = entryNum * entryNum;

    // now, cur_internal_index holds the first unallocated entry
    while (cur_internal_index <= fileBlkId) {
        if (cur_internal_index < INODE_NUM_DIRECT_BLKS) {
	    // alloc the DBlk
	    newDBlkID = allocDBlk(fs);
            if (newDBlkID == -1) {
                _err_last = _fs_DBlkOutOfNumber;
                THROW(__FILE__, __LINE__, __func__);
                return newDBlkID;
            }
	    inode->_in_directBlocks[cur_internal_index] = newDBlkID;
	    count ++;
	    cur_internal_index ++;
	}
	else if ((cur_internal_index - INODE_NUM_DIRECT_BLKS) < (INODE_NUM_S_INDIRECT_BLKS * entryNum)) {
	    UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS) / entryNum;
	    UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
	    // if this is the first alloc in this entry block, fisrt alloc the entry block
	    if (inode->_in_sIndirectBlocks[S_index] == -1) {
		newDBlkID = allocDBlk(fs);
	        if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
        	}
		inode->_in_sIndirectBlocks[S_index] = newDBlkID;
	    }
	    // now, alloc the DBlk
            newDBlkID = allocDBlk(fs);
            if (newDBlkID == -1) {
                _err_last = _fs_DBlkOutOfNumber;
                THROW(__FILE__, __LINE__, __func__);
                return newDBlkID;
            }
	    UINT S_DBlkID = inode->_in_sIndirectBlocks[S_index];
	    readDBlk(fs, S_DBlkID, blkBuf);
	    *((UINT *)blkBuf + S_offset) = newDBlkID;
	    writeDBlk(fs, S_DBlkID, blkBuf);
	    count ++;
	    cur_internal_index ++;
	    
	}
	else if ((cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) < (INODE_NUM_D_INDIRECT_BLKS * entryNumS)) {
	    UINT D_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) / entryNumS;
	    UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) / entryNum;
	    UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) % entryNum;
	    if (inode->_in_dIndirectBlocks[D_index] == -1) {
		newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		inode->_in_dIndirectBlocks[D_index] = newDBlkID;
	    }
	    UINT D_BlkID = inode->_in_dIndirectBlocks[D_index];
	    readDBlk(fs, D_BlkID, blkBuf);
	    if (*((UINT *)blkBuf + S_index) == -1) {
	        newDBlkID = allocDBlk(fs);
                if (newDBlkID == -1) {
                    _err_last = _fs_DBlkOutOfNumber;
                    THROW(__FILE__, __LINE__, __func__);
                    return newDBlkID;
                }
		*((UINT *)blkBuf + S_index) = newDBlkID;
		writeDBlk(fs, D_BlkID, blkBuf);
	    }
	    newDBlkID = allocDBlk(fs);
            if (newDBlkID == -1) {
                _err_last = _fs_DBlkOutOfNumber;
                THROW(__FILE__, __LINE__, __func__);
                return newDBlkID;
            }
	    UINT S_DBlkID = *((UINT *)blkBuf + S_index);
	    readDBlk(fs, S_DBlkID, blkBuf);
	    *((UINT *)blkBuf + S_offset) = newDBlkID;
	    writeDBlk(fs, S_DBlkID, blkBuf);
	    count ++;
	    cur_internal_index ++;
	}
	else {
	    _err_last = _in_IndexOutOfRange;
	    THROW(__FILE__, __LINE__, __func__);
	    return -1;
	}
    }
    return newDBlkID;
}

INT bfree(FileSystem *fs, INode* inode, UINT fileBlkId) {
    printf("bfree called with %u\n", fileBlkId);
    INT DBlkID = bmap(fs, inode, fileBlkId);
    if (DBlkID ==  -1 ) {
        fprintf(stderr, "Cannot free an unallocated fileBlkId %d\n", fileBlkId);
        return -1;
    }
    printf("bmap done with %d\n", DBlkID);

    UINT cur_internal_index = fileBlkId;    
    
    UINT entryNum = BLK_SIZE / sizeof(UINT);
    UINT entryNumS = entryNum * entryNum;
    UINT entryNumD = entryNumS * entryNum;
    UINT blkBuf_t[entryNum];
    UINT blkBuf_d[entryNum];
    UINT blkBuf_s[entryNum];

    if(cur_internal_index >= INODE_NUM_DIRECT_BLKS + INODE_NUM_S_INDIRECT_BLKS * entryNum + INODE_NUM_D_INDIRECT_BLKS * entryNumS) {
    
        UINT T_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                - INODE_NUM_D_INDIRECT_BLKS * entryNumS) / entryNumD;
        assert(T_index < INODE_NUM_T_INDIRECT_BLKS);
        UINT D_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                - INODE_NUM_D_INDIRECT_BLKS * entryNumS - T_index * entryNumD) / entryNumS;
        assert(D_index < entryNum);
        UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum
                - INODE_NUM_D_INDIRECT_BLKS * entryNumS - T_index * entryNumD - D_index * entryNumS) / entryNum;
        assert(S_index < entryNum);
        UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
	    
        UINT T_BlkID = inode->_in_tIndirectBlocks[T_index];
	readDBlk(fs, T_BlkID, (BYTE*) blkBuf_t);
        if (blkBuf_t[D_index] == -1) {
            fprintf(stderr, "ERROR: unallocated triple indirect block\n");
            return -1;
        }
        UINT D_BlkID = blkBuf_t[D_index];
	readDBlk(fs, D_BlkID, (BYTE *)blkBuf_d);
        if (blkBuf_d[S_index] == -1) {
            fprintf(stderr, "ERROR: unallocated double indirect block\n");
            return -1;
        }
        UINT S_BlkID = blkBuf_d[S_index];
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        // free the data block
        freeDBlk(fs, blkBuf_s[S_offset]);
        // mark the corresponding entry in the S_indirecct blk as -1
        blkBuf_s[S_offset] = -1;
        writeDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        if(S_offset == 0) {
            freeDBlk(fs, S_BlkID);
            blkBuf_d[S_index] = -1;
            writeDBlk(fs, D_BlkID, (BYTE *)blkBuf_d);
            if (S_index == 0) {
                freeDBlk(fs, D_BlkID);
                blkBuf_t[D_index] = -1;
                writeDBlk(fs, T_BlkID, (BYTE *)blkBuf_t);
                if(D_index == 0) {
                    freeDBlk(fs, T_BlkID);
                    inode->_in_tIndirectBlocks[T_index] = -1;
                }

            }

        }
    }
    else if (cur_internal_index >= INODE_NUM_DIRECT_BLKS + INODE_NUM_S_INDIRECT_BLKS * entryNum) {
        UINT D_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum) / entryNumS;
        UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS - INODE_NUM_S_INDIRECT_BLKS * entryNum - D_index * entryNumS) / entryNum;
        UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;
        
        UINT D_BlkID = inode->_in_dIndirectBlocks[D_index];
	readDBlk(fs, D_BlkID, (BYTE *)blkBuf_d);
        if (blkBuf_d[S_index] == -1) {
            fprintf(stderr, "ERROR: unallocated double indirect block\n");
            return -1;
        }
        UINT S_BlkID = blkBuf_d[S_index];
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        // free the data block
        freeDBlk(fs, blkBuf_s[S_offset]);
        // mark the corresponding entry in the S_indirecct blk as -1
        blkBuf_s[S_offset] = -1;
        writeDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        if(S_offset == 0) {
            freeDBlk(fs, S_BlkID);
            blkBuf_d[S_index] = -1;
            writeDBlk(fs, D_BlkID, (BYTE *)blkBuf_d);
            if (S_index == 0) {
                freeDBlk(fs, D_BlkID);
                inode->_in_dIndirectBlocks[D_index] = -1;
            }

        }
    }
    else if (cur_internal_index >= INODE_NUM_DIRECT_BLKS) {
        UINT S_index = (cur_internal_index - INODE_NUM_DIRECT_BLKS) / entryNum;
        UINT S_offset = (cur_internal_index - INODE_NUM_DIRECT_BLKS) % entryNum;

#ifdef TRUNCATE_DEBUG
        printf("In the range of Single indirect block, S_index = %u, S_offset = %u\n", S_index, S_offset);
#endif
        UINT S_BlkID = inode->_in_sIndirectBlocks[S_index];
	readDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        // free the data block
        freeDBlk(fs, blkBuf_s[S_offset]);
#ifdef TRUNCATE_DEBUG
        printf("free data block %u\n", blkBuf_s[S_offset]);
#endif
        // mark the corresponding entry in the S_indirecct blk as -1
        blkBuf_s[S_offset] = -1;
        writeDBlk(fs, S_BlkID, (BYTE *)blkBuf_s);

        if(S_offset == 0) {
#ifdef TRUNCATE_DEBUG
            printf("free data block %u\n", S_BlkID);
#endif
            freeDBlk(fs, S_BlkID);
            inode->_in_sIndirectBlocks[S_index] = -1;
        }
    }
    else {
        freeDBlk(fs, inode->_in_directBlocks[cur_internal_index]);
#ifdef TRUNCATE_DEBUG
        printf("free datablock %d\n",inode->_in_directBlocks[cur_internal_index]);
#endif
        inode->_in_directBlocks[cur_internal_index] = -1;
    }

    return 0;
}
#ifdef DEBUG
void printINodes(FileSystem* fs) {
    for(UINT i = 0; i < fs->superblock.nINodes; i++) {
        INode inode;
        readINode(fs, i, &inode);
        printf("%d\t| ", i);
        printINode(&inode);
    }
}

void printDBlkInts(BYTE *buf)
{
    for(UINT k = 0; k < BLK_SIZE; k+=sizeof(UINT)) {
        UINT* val = (UINT*) (buf + k);
        printf("%d ", *val);
    }
    printf("\n");
}

void printDBlkBytes(BYTE *buf)
{
    for(UINT k = 0; k < BLK_SIZE; k++) {
        printf("%x ", buf[k]);
    }
    printf("\n");
}

void printDBlkChars(BYTE *buf)
{
    for(UINT k = 0; k < BLK_SIZE; k++) {
        printf("%c ", (char) buf[k]);
    }
    printf("\n");
}

void printDBlks(FileSystem* fs) {
    for(UINT i = 0; i < fs->superblock.nDBlks; i++) {
        BYTE buf[BLK_SIZE];
        readDBlk(fs, i, buf);
        printf("%d\t| ", i);
        printDBlkInts(buf);
    }
}
#endif
