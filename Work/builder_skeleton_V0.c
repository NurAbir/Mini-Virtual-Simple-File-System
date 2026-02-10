// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_minivsfs.c -o mkfs_builder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#define BS 4096u               // block size
#define INODE_SIZE 128u
#define ROOT_INO 1u

uint64_t g_random_seed = 0; // This should be replaced by seed value from the CLI.

// below contains some basic structures you need for your project
// you are free to create more structures as you require

#pragma pack(push, 1)
typedef struct {
    // CREATE YOUR SUPERBLOCK HERE
    // ADD ALL FIELDS AS PROVIDED BY THE SPECIFICATION

    //start - Nur
    uint32_t magic;               // 4 bytes, 0x4D565346
    uint32_t version;             // 4 bytes, 1
    uint32_t block_size;          // 4 bytes, 4096

    uint64_t total_blocks;        // 8 bytes
    uint64_t inode_count;         // 8 bytes
    uint64_t inode_bitmap_blocks; // 8 bytes
    uint64_t inode_bitmap_start;  // 8 bytes

    uint64_t data_bitmap_blocks;  // 8 bytes
    uint64_t data_region_start;   // 8 bytes
    uint64_t root_inode;          // 8 bytes, 1
    uint64_t mtime_epoch;         // 8 bytes, Build time (Unix Epoch)

    uint32_t flags;               // 4 bytes, 0
    //end - Nur

    //given
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;            // crc32(superblock[0..4091]) // 4 bytes
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    // CREATE YOUR INODE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE

    //start - Nur
    uint16_t mode;              // 2 bytes
    uint16_t links;             // 2 bytes

    uint32_t uid;               // 4 bytes, 0
    uint32_t gid;               // 4 bytes, 0
    
    uint64_t size_bytes;        // 8 bytes, 
    uint64_t atime;             // 8 bytes,  Build time (Unix Epoch)
    uint64_t mtime;             // 8 bytes,  Build time (Unix Epoch)
    uint64_t ctime;             // 8 bytes,  Build time (Unix Epoch)

    uint32_t direct[12];        // 12*4 = 48 bytes [4 each]
    uint32_t reversed_0;        // 4 bytes, 0
    uint32_t reversed_1;        // 4 bytes, 0
    uint32_t reversed_2;        // 4 bytes, 0

    uint32_t proj_id;           // 4 bytes, Our Group ID (2)
    uint32_t uid16_gid16;       // 4 bytes, 0
    uint64_t xattr_ptr;         // 8 bytes, 0
    uint64_t inode_crc;         // 8 bytes, crc32 of bytes [0..119]
    //end - Nur

    //given
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc;   // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    // CREATE YOUR DIRECTORY ENTRY STRUCTURE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE

    //start - Nur
    uint32_t inode_no;          // 4 bytes, 0 - if free
    uint8_t type;              // 1 byte: file type (1=file, 2=dir)
    char name[58];              // 58 bytes
    //end - Nur

    uint8_t  checksum;          // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");


// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *) sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++) x ^= p[i];   // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

int main() {
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    // PARSE YOUR CLI PARAMETERS
    // THEN CREATE YOUR FILE SYSTEM WITH A ROOT DIRECTORY
    // THEN SAVE THE DATA INSIDE THE OUTPUT IMAGE

    //start - Nur
    superblock_t sb;
    memset(&sb, 0, sizeof(superblock_t));

    sb.magic = 0x4D565346;                      // "MVFS"
    sb.version = 1;
    sb.block_size = BS;  

    sb.total_blocks = TOTAL_BlOCKS;             //from input
    sb.inode_count = INODES_COUNT;
    
    sb.inode_bitmap_start = 1;                  //block 1
    sb.inode_bitmap_blocks = 1;                 //1 block for inode bitmap

    sb.data_region_start = 2;                   //block 2
    sb.data_bitmap_blocks = 1;                  //1 block for data bitmap

    sb.inode_table_start = 3;                   //block 3
    sb.inode_table_blocks = (sb.inode_count * INODE_SIZE + BS - 1) / BS;

    sb.data_region_start   = sb.inode_table_start + sb.inode_table_blocks;      //block 7
    sb.data_region_blocks  = TOTAL_BLOCKS - sb.data_region_start;               //rest of the blocks for data region


    sb.root_inode = ROOT_INO;                    //inode 1
    sb.mtime_epoch = (uint64_t)time(NULL);      //build time
    sb.flags = 0;

    superblock_crc_finalize(&sb);

    // Write superblock to file

    FILE *fp = fopen("minivsfs.img", "wb");
    if (fp == NULL) {
        perror("Failed to create image file");
        return EXIT_FAILURE;
    }

    // Write superblock

    if (fwrite(&sb, 1, BS, fp) != BS) {
        perror("Failed to write superblock");
        fclose(fp);
        return EXIT_FAILURE;
    }

    // Initialize Inode Map

    uint8_t inode_bitmap[BS];
    memset(inode_bitmap, 0, BS);
    inode_bitmap[0] |= 1 << 0; // Mark ROOT inode as used
    fseek(fp, sb.inode_bitmap_start * BS, SEEK_SET);
    fwrite(inode_bitmap, 1, BS, fp);

    // Initialize Data Bitmap

    uint8_t data_bitmap[BS];
    memset(data_bitmap, 0, BS);
    data_bitmap[0] |= 1 << 0; // Mark first data block as
    fseek(fp, sb.data_bitmap_start * BS, SEEK_SET);
    fwrite(data_bitmap, 1, BS, fp);

    // Initialize ROOT Inode

    inode_t root_inode;
    memset(&root_inode, 0, sizeof(root_inode));

    root_inode.mode = 0x4000 | 0755; // Directory + 0755 permissions
    root_inode.links = 2;

    root_inode.uid = 0;
    root_inode.gid = 0;

    root_inode.size_bytes = BS; // Size of one block
    
    root_inode->reserved_0 = 0;
    root_inode->reserved_1 = 0;
    root_inode->reserved_2 = 0;

    root_inode.proj_id = 2; // Our Group ID

    root_inode.uid16_gid16  = 0;  
    root_inode.xattr_ptr    = 0;  

    uint32_t current_time = (uint32_t)time(NULL);
    root_inode.atime = current_time;
    root_inode.mtime = current_time;
    root_inode.ctime = current_time;

    inode_crc_finalize(&root_inode);

    // Write ROOT Inode

    fseek(fp, sb.inode_table_start * BS, SEEK_SET);
    fwrite(&root_inode, 1, sizeof(root_inode), fp);

    // Initialize ROOT Directory

    dirent64_t de;
    memset(&de, 0, sizeof(de));

    de.inode_no = ROOT_INO;
    de.type = 2; // Directory
    strncpy(de.name, ".", sizeof(de.name) - 1);
    dirent_checksum_finalize(&de);

    fseek(fp, sb.data_region_start * BS, SEEK_SET);
    fwrite(&de, 1, sizeof(de), fp);

    memset(&de, 0, sizeof(de));
    de.inode_no = ROOT_INO;
    de.type = 2; // Directory
    strncpy(de.name, "..", sizeof(de.name) - 1);
    dirent_checksum_finalize(&de);

    fseek(fp, sb.data_region_start * BS + sizeof(de), SEEK_SET);
    fwrite(&de, 1, sizeof(de), fp);

    fclose(fp);
    //end - Nur

    return 0;
}