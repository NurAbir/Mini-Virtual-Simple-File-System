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
    uint32_t magic;               
    uint32_t version;            
    uint32_t block_size;          
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;        
    uint64_t mtime_epoch;         
    uint32_t flags;
    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum;            // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    // CREATE YOUR INODE HERE
    // IF CREATED CORRECTLY, THE STATIC_ASSERT ERROR SHOULD BE GONE
    uint16_t mode;                
    uint16_t links;              
    uint32_t uid;                 
    uint32_t gid;                 
    uint64_t size_bytes;          
    uint64_t atime;              
    uint64_t mtime;               
    uint64_t ctime;               
    uint32_t direct[12];  
    uint32_t reserved_0;          
    uint32_t reserved_1;          
    uint32_t reserved_2;          
    uint32_t proj_id;             // group ID
    uint32_t uid16_gid16;        
    uint64_t xattr_ptr; 
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
    uint32_t inode_no;           
    uint8_t type;                 
    char name[58];
    uint8_t  checksum; // XOR of bytes 0..62
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

int parse_args(int argc, char* argv[], char** image_path, uint64_t* size_kib, uint64_t* inode_count) {
    *image_path = NULL;
    *size_kib = 0;
    *inode_count = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--image") == 0 && i + 1 < argc) {
            *image_path = argv[++i];
        } else if (strcmp(argv[i], "--size-kib") == 0 && i + 1 < argc) {
            *size_kib = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--inodes") == 0 && i + 1 < argc) {
            *inode_count = strtoull(argv[++i], NULL, 10);
        }
    }
    
    
    if (!*image_path || *size_kib == 0 || *inode_count == 0) {
        return -1; 
    }
    
    return 0; 
}
void init_superblock(superblock_t* sb, uint64_t total_blocks, uint64_t inode_count, time_t build_time) {
    memset(sb, 0, sizeof(superblock_t));
    
    sb->magic = 0x4D565346;
    sb->version = 1;
    sb->block_size = BS;
    sb->total_blocks = total_blocks;
    sb->inode_count = inode_count;
    
    
    sb->inode_bitmap_start = 1;
    sb->inode_bitmap_blocks = 1;
    sb->data_bitmap_start = 2;
    sb->data_bitmap_blocks = 1;
    sb->inode_table_start = 3;
    sb->inode_table_blocks = (inode_count * INODE_SIZE + BS - 1) / BS; //ceiling division
    sb->data_region_start = 3 + sb->inode_table_blocks;
    sb->data_region_blocks = total_blocks - sb->data_region_start;
    
    sb->root_inode = ROOT_INO;
    sb->mtime_epoch = (uint64_t)build_time;
    sb->flags = 0;
}
void init_root_inode(inode_t* root_inode, time_t build_time, uint32_t proj_id) {
    memset(root_inode, 0, sizeof(inode_t));
    
    root_inode->mode = 0040000;  // directory mode (octal)
    root_inode->links = 2;       // . and .. entries
    root_inode->uid = 0;
    root_inode->gid = 0;
    root_inode->size_bytes = BS; // one block for directory entries
    root_inode->atime = (uint64_t)build_time;
    root_inode->mtime = (uint64_t)build_time;
    root_inode->ctime = (uint64_t)build_time;
    root_inode->direct[0] = 1;   // first data block 
    
    // Initialize remaining direct pointers to 0
    for (int i = 1; i < 12; i++) {
        root_inode->direct[i] = 0;
    }
    
    root_inode->reserved_0 = 0;
    root_inode->reserved_1 = 0;
    root_inode->reserved_2 = 0;
    root_inode->proj_id = proj_id;
    root_inode->uid16_gid16 = 0;
    root_inode->xattr_ptr = 0;
}
void init_root_directory_entries(dirent64_t* entries) {
    // Entry for "."
    memset(&entries[0], 0, sizeof(dirent64_t));
    entries[0].inode_no = ROOT_INO;
    entries[0].type = 2; // directory
    strcpy(entries[0].name, ".");
    
    // Entry for ".."
    memset(&entries[1], 0, sizeof(dirent64_t));
    entries[1].inode_no = ROOT_INO;
    entries[1].type = 2; // directory
    strcpy(entries[1].name, "..");
    
   
    for (int i = 2; i < BS / sizeof(dirent64_t); i++) {
        memset(&entries[i], 0, sizeof(dirent64_t));
        entries[i].inode_no = 0; // free entry
    }
    
    // Finalize checksums
    dirent_checksum_finalize(&entries[0]);
    dirent_checksum_finalize(&entries[1]);
}




int create_filesystem(const char* image_path, uint64_t size_kib, uint64_t inode_count) {
    uint64_t total_blocks = (size_kib * 1024) / BS;
    time_t build_time = time(NULL);
    

    uint64_t min_blocks = 4 + ((inode_count * INODE_SIZE + BS - 1) / BS);

    
     
    if (total_blocks < min_blocks) {
        perror("Image size too small");
        return -1;
    }
    
    FILE* img_file = fopen(image_path, "wb");
    if (img_file == NULL) {
        perror("Failed to create image file");
        return -1;
    }
    
    
    superblock_t superblock;
    init_superblock(&superblock, total_blocks, inode_count, build_time);
    
   
    uint8_t* block_buffer = calloc(1, BS);

    
    
    memcpy(block_buffer, &superblock, sizeof(superblock_t));
    superblock_crc_finalize((superblock_t*)block_buffer);

    fwrite(block_buffer, BS, 1, img_file);
       
       
  
    
    // Write inode bitmap (block 1)
    memset(block_buffer, 0, BS);
    // bit 0 = inode 1
    block_buffer[0] = 0x01;

    fwrite(block_buffer, BS, 1, img_file);
    
    // Write data bitmap (block 2)
    memset(block_buffer, 0, BS);
    // for root directory
    block_buffer[0] = 0x01;

    fwrite(block_buffer, BS, 1, img_file);
    
    // Write inode table
    uint64_t inode_blocks = superblock.inode_table_blocks;

    for (uint64_t i = 0; i < inode_blocks; i++) {
        memset(block_buffer, 0, BS);
        
        // inode block-->root inode
        if (i == 0) {
            inode_t root_inode;
            init_root_inode(&root_inode, build_time, 2); //proj_id = 2 
            inode_crc_finalize(&root_inode);
            memcpy(block_buffer, &root_inode, sizeof(inode_t));
        }
        
        fwrite(block_buffer, BS, 1, img_file); 

    }
    
    // Write data region
    uint64_t data_blocks = superblock.data_region_blocks;
    for (uint64_t i = 0; i < data_blocks; i++) {
        memset(block_buffer, 0, BS);
        
        // first data block-->root directory
        if (i == 0) {
            init_root_directory_entries((dirent64_t *)block_buffer);
        }

        fwrite(block_buffer, BS, 1, img_file);
    }
    
    free(block_buffer);
    fclose(img_file);

    printf("Successfully created MiniVSFS image: %s\n", image_path); //sanity check
    
    
    return 0;
}
int main(int argc, char* argv[]) {
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    char* image_path;
    uint64_t size_kib, inode_count;
    // PARSE YOUR CLI PARAMETERS
    if (parse_args(argc, argv, &image_path, &size_kib, &inode_count) != 0) {
        fprintf(stderr, "Usage: %s --image <output.img> --size-kib <180..4096> --inodes <128..512>\n", argv[0]);;
        return 1;
    }
    // THEN CREATE YOUR FILE SYSTEM WITH A ROOT DIRECTORY
    create_filesystem(image_path,size_kib,inode_count);
    // THEN SAVE THE DATA INSIDE THE OUTPUT IMAGE
    return 0;
}