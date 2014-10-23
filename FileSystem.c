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

UINT makefs(UINT nDBlks, UINT nINodes, FileSystem* fs) {
	
    //validate number of blocks and inodes
    //num inodes divisible by inodes per block
    //fs size < max size
    //num blocks >= num inodes
    //num inodes >= inode cache size
    //num blocks >= block cache size 

    //allocate memory for filesystem struct
    fs = malloc(sizeof(FileSystem));
    //initialize in-memory superblock
    fs->superblock.nDBlks = nDBlks;
    fs->superblock.nFreeDBlks = nDBlks;
    fs->superblock.pFreeDBlksHead = 0;
    fs->superblock.pNextFreeDBlk = FREE_DBLK_CACHE_SIZE - 1;
    
    fs->superblock.nINodes = nINodes;
    fs->superblock.nFreeINodes = nINodes;
    fs->superblock.pNextFreeINode = FREE_INODE_CACHE_SIZE - 1;

    fs->superblock.modified = true;

    //initialize superblock free inode cache
    for(UINT i = 0; i < FREE_INODE_CACHE_SIZE; i++) {
        fs->superblock.freeINodeCache[i] = i;
    }

    //compute file system size and offset for inode/data blocks
    fs->nBytes = (BLK_SIZE + nINodes * INODE_SIZE + nDBlks * BLK_SIZE);
    fs->diskINodeBlkOffset = 1;
    fs->diskDBlkOffset = fs->diskINodeBlkOffset + nINodes / INODES_PER_BLK;

    //initialize the in-memory disk
    initDisk(fs->disk, fs->nBytes);

    //create inode list on disk
    UINT nextINodeId = 0;
    UINT nextINodeBlk = fs->diskINodeBlkOffset;
    INode nextINodeBlkBuf[INODES_PER_BLK];

    while(nextINodeBlk < fs->diskDBlkOffset) {
        //fill inode blocks one at a time
        for(UINT i = 0; i < INODES_PER_BLK; i++) {
            nextINodeBlkBuf[i]._in_type = FREE; //TODO CHANGE THIS TO ENUM TYPE "FREE"
        }

        writeBlk(fs->disk, nextINodeBlk, (BYTE*) nextINodeBlkBuf);
        nextINodeBlk++;
    }

    //create free block list on disk
    UINT nextListBlk = 0;
    UINT freeDBlkList[FREE_DBLK_CACHE_SIZE];

    while(nextListBlk < fs->superblock.nDBlks) {
        //special case: next head pointer goes in first entry
        freeDBlkList[0] = nextListBlk + FREE_DBLK_CACHE_SIZE;

        //rest of free blocks are enumerated in order
        for(UINT i = 1; i < FREE_DBLK_CACHE_SIZE; i++) {
            freeDBlkList[i] = nextListBlk + i;
        }

        //write completed block and advance to next head
        writeDBlk(fs, nextListBlk, (BYTE*) freeDBlkList);
        nextListBlk += FREE_DBLK_CACHE_SIZE;
    }

    //write superblock to disk
    BYTE superblockBuf[BLK_SIZE];
    blockify(&fs->superblock, superblockBuf);
    writeBlk(fs->disk, SUPERBLOCK_OFFSET, superblockBuf);
    fs->superblock.modified = false;
    return 0;
}

UINT destroyfs(FileSystem* fs) {

    destroyDisk(fs->disk);
    free(fs);
    return 0;
}

UINT initializeINode(INode *inode){
    inode->_in_type = REGULAR;
    //FIXME: shall we make owner string?
    inode->_in_owner = 0;
    inode->_in_permissions = 777;
    //TODO: get time
    //inode->_in_modtime = get_time();
    //inode->_in_accesstime = get_time();
    inode->_in_filesize = 0;

    //since we are storing logical data blk id, 0 could be a valid blk
    for (UINT i = 0; i < INODE_NUM_DIRECT_BLKS; i ++) {
        inode->_in_directBlocks[i] = -1;
    }
    for (UINT i = 0; i < INODE_NUM_S_INDIRECT_BLKS; i ++) {
        inode->_in_sIndirectBlocks[i] = -1;
    }
    for (UINT i = 0; i < INODE_NUM_D_INDIRECT_BLKS; i ++) {
        inode->_in_dIndirectBlocks[i] = -1;
    }

    return 0;
}

//input: none
//output: an inode
//function: allocate a free inode
UINT allocINode(FileSystem* fs, INode* inode) {

    if(fs->superblock.nFreeINodes == 0) {
        fprintf(stderr, "error: no more free inodes available!\n");
        return 1;
    }
    
    while (true) { // continue if inode cache empty but there are free inodes in the inode table

        // the inode cache is empty
        if(fs->superblock.freeINodeCache[0] < 0) {
            assert(fs->superblock.pNextFreeINode < 0);
            
            // scan inode list and fill the inode cache list to capacity
            UINT nextINodeBlk = fs->diskINodeBlkOffset;
            BYTE nextINodeBlkBuf[BLK_SIZE];
            
            UINT k = 0; // index in inode cache 
            BOOL FULL = false;
            while(nextINodeBlk < fs->diskDBlkOffset && !FULL)  {
                
                // read the whole block out into a byte array
                readBlk(fs->disk, nextINodeBlk, nextINodeBlkBuf);
                
                // covert the byte array to INode structures
                INode* inode_s = (INode*) nextINodeBlkBuf; 
                
                // check inodes one at a time
                for(UINT i = 0; i < INODES_PER_BLK && !FULL; i++) {
                    
                    //move to the destination inode
                    INode* inode_d = inode_s + i;

                    // found a free inode
                    if(inode_d->_in_type == FREE) {
                        fs->superblock.freeINodeCache[k] = (nextINodeBlk - fs->diskINodeBlkOffset) * INODES_PER_BLK + i;
                        fs->superblock.pNextFreeINode = fs->superblock.freeINodeCache[k];
                        k ++;

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
        else {
            // read the next free inode from the inode table
            readINode(fs, fs->superblock.pNextFreeINode, inode);

            if(inode->_in_type != FREE) {
                fprintf(stderr, "inode not free after all!\n");
                return 1;
            }

            // update the inode cache list
            for (UINT i = 0; i < FREE_INODE_CACHE_SIZE; i ++) {
                if(fs->superblock.freeINodeCache[i] == fs->superblock.pNextFreeINode) {
                    fs->superblock.freeINodeCache[i] = -1; 
                    if(i == 0) {
                        // the entire inode cache becomes empty
                        fs->superblock.pNextFreeINode = -1;
                    }
                    else {
                        fs->superblock.pNextFreeINode = fs->superblock.freeINodeCache[i-1];
                    }
                }
            }

            // initialize the inode
            initializeINode(inode);

            // decrement the free inodes count
            fs->superblock.nFreeINodes --;
            return 0;

        }
    }
}

// input: inode number
// output: none
// function: free a inode, which updates the inode cache and/or inode table
UINT freeINode(FileSystem* fs, UINT id) {

    // if inode cache not full, store the inode number in the list
    BOOL FIND = false;
    for (UINT i = 0; i < FREE_INODE_CACHE_SIZE && !FIND; i ++) {
        if(fs->superblock.freeINodeCache[i] == -1) {
            fs->superblock.freeINodeCache[i] = id;
            FIND = true;
        }
    }

    // update the inode table to mark the inode free
    INode * inode;
    if(readINode(fs, id, inode) == -1) {
        fprintf(stderr, "error: read inode %d from disk\n", id);
        return 1;
    }
    inode->_in_type = FREE;
    if(writeINode(fs, id, inode) == -1){
        fprintf(stderr, "error: write inode %d to disk\n", id);
        return 1;
    }
    
    // increase file system free inode count
    fs->superblock.nFreeINodes ++;

    return 0;

}

// input: inode number
// output: the pointer to the inode
// function: read a disk inode
UINT readINode(FileSystem* fs, UINT id, INode* inode) {
    UINT blk_num = fs->diskINodeBlkOffset + id / INODES_PER_BLK;
    UINT blk_offset = id % INODES_PER_BLK;

    BYTE INodeBlkBuf[BLK_SIZE];
    if(readBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: read blk %d from disk\n", blk_num);
        return 1;
    }
                
    INode* inode_s = (INode*) INodeBlkBuf; 
    inode = inode_s + blk_offset;

    return 0;
}

// input: inode number id, an inode
// output: none
// function: write the disk inode #id in the inode table
UINT writeINode(FileSystem* fs, UINT id, INode* inode) {
    UINT blk_num = fs->diskINodeBlkOffset + id / INODES_PER_BLK;
    UINT blk_offset = id % INODES_PER_BLK;
    
    BYTE INodeBlkBuf[BLK_SIZE];
    if(readBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: read blk %d from disk\n", blk_num);
        return 1;
    }
    
    INode* inode_s = (INode*) INodeBlkBuf; 
    // find the inode to write
    INode *inode_d = inode_s + blk_offset;

    // replace the inode
    inode_d->_in_type = inode->_in_type;
    inode_d->_in_owner = inode->_in_owner;
    inode_d->_in_permissions = inode->_in_permissions;
    inode_d->_in_modtime =  inode->_in_modtime;
    inode_d->_in_accesstime = inode->_in_accesstime;
    inode_d->_in_filesize = inode_d->_in_filesize;
    for (UINT i = 0; i < INODE_NUM_DIRECT_BLKS; i ++) {
        inode_d->_in_directBlocks[i] = inode->_in_directBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_S_INDIRECT_BLKS; i ++) {
        inode_d->_in_sIndirectBlocks[i] = inode->_in_sIndirectBlocks[i];
    }
    for (UINT i = 0; i < INODE_NUM_D_INDIRECT_BLKS; i ++) {
        inode_d->_in_dIndirectBlocks[i] = inode->_in_dIndirectBlocks[i];
    }

    // write the entire inode block back to disk
    if(writeBlk(fs->disk, blk_num, INodeBlkBuf) == -1) {
        fprintf(stderr, "error: read blk %d from disk\n", blk_num);
        return 1;
    }
    
    return 0;
}

UINT allocDBlk(FileSystem* fs) {
    return 0;

}

UINT freeDBlk(FileSystem* fs, UINT id) {
    return 0;

}

UINT readDBlk(FileSystem* fs, UINT id, BYTE* buf) {
    return 0;

}

// writes a data block to the disk
// dBlkId: the data block logical id (not raw logical id!)
// buf: the buffer to write (must be exactly block-sized)
UINT writeDBlk(FileSystem* fs, UINT dBlkId, BYTE* buf) {
    writeBlk(fs->disk, fs->diskDBlkOffset + dBlkId, buf);
    return 0;
}

// input: inode, logic file byte offset
// output: logical data block #,
// function: block map of a logic file byte offset to a file system block.
// note that the coverted byte offset in this block, num of bytes in to read in the block
// can be easily calculated: i.e. off= offset % BLK_SIZE, len = BLK_SIZE - off
UINT bmap(FileSystem* fs, INode* inode, UINT offset, UINT* cvt_blk_num){

    UINT space_per_sInBlk; // the address range provided by one single indirect block
    UINT space_per_dInBlk; // the address range provdied by one double indirect block

    UINT direct_space;    // total address range provided by direct blocks
    UINT s_indirect_space; // total address range provided by single indirect blocks
    UINT d_indirect_space; // total address range provided by double indirect blocks

    space_per_sInBlk = (BLK_SIZE / 4) * BLK_SIZE;     
    space_per_dInBlk = (BLK_SIZE / 4) * (BLK_SIZE / 4) * BLK_SIZE; 

    direct_space = INODE_NUM_DIRECT_BLKS * BLK_SIZE; 
    s_indirect_space = INODE_NUM_S_INDIRECT_BLKS * space_per_sInBlk;
    d_indirect_space = INODE_NUM_D_INDIRECT_BLKS * space_per_dInBlk;

    if (offset < direct_space) {
        // look up in a direct block
        UINT dBlk_index = offset / BLK_SIZE;

        // read the direct block to find the data block #
        *cvt_blk_num = inode->_in_directBlocks[dBlk_index];
        printf("Found in a direct block, physical block # = %d\n", *cvt_blk_num);
    }
    else if (offset < s_indirect_space) {
        // look up in single indirect blocks
        BYTE readBuf[BLK_SIZE];

        // locate which indirect block to look up
        UINT sInBlks_index = (offset - direct_space) / space_per_sInBlk;

        // read the indirect block that contains a list of direct blocks 
        readDBlk(fs, inode->_in_sIndirectBlocks[sInBlks_index], readBuf);

        // locate which direct block to look up
        UINT dBlk_index = (offset - direct_space - sInBlks_index * space_per_sInBlk) / BLK_SIZE;

        *cvt_blk_num = readBuf[dBlk_index];
        printf("Found in a single indirect block, physical block # = %d\n", *cvt_blk_num);
    }
    else if (offset < d_indirect_space){
        // look up in double indirect blocks
        BYTE readBuf_d[BLK_SIZE]; // buffer to store the double indirect block
        BYTE readBuf_s[BLK_SIZE]; // buffer to store the single indirect block

        // locate which double indirect block to look up
        UINT dInBlks_index = (offset - direct_space - s_indirect_space) / space_per_dInBlk;
        
        // read the double indirect block which contains a list of single indirect blocks
        readDBlk(fs, inode->_in_dIndirectBlocks[dInBlks_index], readBuf_d);

        // locate which single indirect block to look up
        UINT sInBlks_index = (offset - direct_space - s_indirect_space - dInBlks_index * space_per_dInBlk) / space_per_sInBlk;

        // read the single indirect block which contains a list of direct blocks
        readDBlk(fs, readBuf_d[sInBlks_index], readBuf_s);

        // locate which direct block to look up
        UINT dBlk_index = (offset - direct_space - s_indirect_space - dInBlks_index * space_per_dInBlk - sInBlks_index * space_per_sInBlk) / BLK_SIZE;
        
        *cvt_blk_num = readBuf_d[dBlk_index];
        printf("Found in a double indirect block, physical block # = %d\n", *cvt_blk_num);
    }
    else {
        fprintf(stderr, "bmap fail: out of inode address space!\n");
        return 1;
    }
    return 0;
}