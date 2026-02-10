// Build: gcc -O2 -std=c17 -Wall -Wextra mkfs_adder.c -o mkfs_adder
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

#pragma pack(push, 1)
typedef struct
{
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
    uint32_t checksum; // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push, 1)
typedef struct
{
    uint16_t mode;  
    uint16_t links; 

    uint32_t uid; 
    uint32_t gid; 

    uint64_t size_bytes; 
    uint64_t atime;      
    uint64_t mtime;      
    uint64_t ctime;      

    uint32_t direct[DIRECT_MAX];
    uint32_t reserved_0;         
    uint32_t reserved_1;        
    uint32_t reserved_2;        

    uint32_t proj_id;     
    uint32_t uid16_gid16; 
    uint64_t xattr_ptr;   

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc; // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;            // inode number (0 if free)
    uint8_t type;                 // 1=file, 2=dir
    char name[58];                // filename (null-terminated)
    uint8_t checksum;             // XOR of bytes 0..62
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

void print_usage(const char* prog_name) {
    fprintf(stderr, "Usage: %s --input <input.img> --output <output.img> --file <filename>\n", prog_name);
}

int parse_args(int argc, char* argv[], char** input_path, char** output_path, char** file_path) {
    if (argc != 7) {
        return -1;
    }
    
    for (int i = 1; i < argc; i += 2) {
        if (strcmp(argv[i], "--input") == 0) {
            *input_path = argv[i + 1];
        } else if (strcmp(argv[i], "--output") == 0) {
            *output_path = argv[i + 1];
        } else if (strcmp(argv[i], "--file") == 0) {
            *file_path = argv[i + 1];
        } else {
            return -1;
        }
    }
    
    if (!*input_path || !*output_path || !*file_path) {
        return -1;
    }
    
    return 0;
}

// Find the first free inode in the bitmap
uint32_t find_free_inode(uint8_t* inode_bitmap, uint64_t inode_count) {
    for (uint64_t i = 0; i < inode_count; i++) {
        uint64_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        
        if (!(inode_bitmap[byte_idx] & (1 << bit_idx))) {
            return i + 1; // inodes are 1-indexed
        }
    }
    return 0; // no free inode found
}

// Find the first free data block in the bitmap
uint32_t find_free_data_block(uint8_t* data_bitmap, uint64_t data_block_count) {
    for (uint64_t i = 0; i < data_block_count; i++) {
        uint64_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        
        if (!(data_bitmap[byte_idx] & (1 << bit_idx))) {
            return i + 1; // return relative to data region (1-indexed)
        }
    }
    return 0; // no free data block found
}

// Set a bit in the bitmap
void set_bitmap_bit(uint8_t* bitmap, uint32_t bit_num) {
    uint32_t byte_idx = (bit_num - 1) / 8; // adjust for 1-indexing
    uint8_t bit_idx = (bit_num - 1) % 8;
    bitmap[byte_idx] |= (1 << bit_idx);
}

// Find free directory entry in root directory
int find_free_dirent(dirent64_t* entries, int max_entries) {
    for (int i = 2; i < max_entries; i++) { // skip . and .. entries
        if (entries[i].inode_no == 0) {
            return i;
        }
    }
    return -1; // no free entry found
}

int add_file_to_filesystem(const char* input_path, const char* output_path, const char* file_path) {
    struct stat st;
    if (stat(file_path, &st) != 0) {
        perror("Cannot access file to add");
        return -1;
    }

    if (!S_ISREG(st.st_mode)) {
        perror("File to add is not a regular file");
        return -1;
    }

    if ((uint64_t)st.st_size > DIRECT_MAX * BS) {
        perror("File too large to add (exceeds direct block limit)");
        return -1;
    }


    FILE* input_file = fopen(input_path, "rb");
        if (input_file == NULL) {
            perror("Cannot open input image file");
            return -1;
    }
    
    // Read and validate superblock
    superblock_t superblock;
    if (fread(&superblock, sizeof(superblock_t), 1, input_file) != 1) {
        perror("Failed to read superblock");
        fclose(input_file);
        return -1;
    }
    
    if (superblock.magic != 0x4D565346) {
        perror("Invalid filesystem magic number");
        fclose(input_file);
        return -1;
    }

    // Calculate blocks needed for the file
    uint64_t blocks_needed = (st.st_size + BS - 1) / BS; // ceiling division
    if (blocks_needed > DIRECT_MAX) {
        perror("File too large to add (exceeds direct block limit)");
        fclose(input_file);
        return -1;
    }
    
    // Read entire image into memory for easier manipulation
    fseek(input_file, 0, SEEK_END);
    long img_size = ftell(input_file);
    fseek(input_file, 0, SEEK_SET);
    
    uint8_t* img_data = malloc(img_size);
    if (!img_data) {
        perror("Memory allocation failed for image data");
        fclose(input_file);
        return -1;
    }
    
    if (fread(img_data, 1, img_size, input_file) != (size_t)img_size) {
        perror("Failed to read image data");
        free(img_data);
        fclose(input_file);
        return -1;
    }
    fclose(input_file);

    // Get pointers to different sections
    superblock_t* sb = (superblock_t*)img_data;
    uint8_t* inode_bitmap = img_data + (sb->inode_bitmap_start * BS);
    uint8_t* data_bitmap = img_data + (sb->data_bitmap_start * BS);
    inode_t* inode_table = (inode_t*)(img_data + (sb->inode_table_start * BS));
    uint8_t* data_region = img_data + (sb->data_region_start * BS);
    
    // Find free inode
    uint32_t free_inode = find_free_inode(inode_bitmap, sb->inode_count);
    if (free_inode == 0) {
        perror("No free inode available");
        free(img_data);
        return -1;
    }

    // Find free data blocks
    uint32_t free_data_blocks[DIRECT_MAX];
    uint32_t found_blocks = 0;
    
    for (uint64_t i = 0; i < sb->data_region_blocks && found_blocks < blocks_needed; i++) {
        uint64_t byte_idx = i / 8;
        uint8_t bit_idx = i % 8;
        
        if (!(data_bitmap[byte_idx] & (1 << bit_idx))) {
            free_data_blocks[found_blocks++] = i + 1; // 1-indexed relative to data region
        }
    }
    
    if (found_blocks < blocks_needed) {
        perror("Not enough free data blocks available");
        free(img_data);
        return -1;
    }

    // Read the file to be added
    FILE* file_to_add = fopen(file_path, "rb");
    if (!file_to_add) {
        perror("Cannot open file to add");
        free(img_data);
        return -1;
    }
    
    uint8_t* file_data = malloc(st.st_size);
    if (!file_data) {
        perror("Memory allocation failed");
        free(img_data);
        fclose(file_to_add);
        return -1;
    }
    
    if (fread(file_data, 1, st.st_size, file_to_add) != (size_t)st.st_size) {
        perror("Failed to read file data");
        free(img_data);
        free(file_data);
        fclose(file_to_add);
        return -1;
    }
    fclose(file_to_add);
    
    // Create new inode for the file
    inode_t* new_inode = &inode_table[free_inode - 1]; // adjust for 1-indexing
    memset(new_inode, 0, sizeof(inode_t));
    
    time_t current_time = time(NULL);
    new_inode->mode = 0100000;  // regular file mode (octal)
    new_inode->links = 1;
    new_inode->uid = 0;
    new_inode->gid = 0;
    new_inode->size_bytes = st.st_size;
    new_inode->atime = (uint64_t)current_time;
    new_inode->mtime = (uint64_t)current_time;
    new_inode->ctime = (uint64_t)current_time;

    // Set direct block pointers
    for (uint32_t i = 0; i < blocks_needed; i++) {
        new_inode->direct[i] = free_data_blocks[i];
    }
    for (uint32_t i = blocks_needed; i < DIRECT_MAX; i++) {
        new_inode->direct[i] = 0;
    }
    
    new_inode->reserved_0 = 0;
    new_inode->reserved_1 = 0;
    new_inode->reserved_2 = 0;
    new_inode->proj_id = 0;
    new_inode->uid16_gid16 = 0;
    new_inode->xattr_ptr = 0;
    
    inode_crc_finalize(new_inode);
    
    // Update inode bitmap
    set_bitmap_bit(inode_bitmap, free_inode);
    
    // Update data bitmap and write file data
    for (uint32_t i = 0; i < blocks_needed; i++) {
        set_bitmap_bit(data_bitmap, free_data_blocks[i]);
        
        // Write file data to the data block
        uint64_t block_offset = (free_data_blocks[i] - 1) * BS; // adjust for 1-indexing
        uint64_t data_to_write = BS;
        uint64_t file_offset = i * BS;
        
        if (file_offset + BS > (uint64_t)st.st_size) {
            data_to_write = st.st_size - file_offset;
        }
        
        memset(data_region + block_offset, 0, BS); // clear block first
        memcpy(data_region + block_offset, file_data + file_offset, data_to_write);
    }
    
    // Add directory entry to root directory
    dirent64_t* root_entries = (dirent64_t*)data_region; // first data block is root directory
    int max_entries = BS / sizeof(dirent64_t);
    int free_entry_idx = find_free_dirent(root_entries, max_entries);
    
    if (free_entry_idx == -1) {
        perror("No free directory entry available in root directory");
        free(img_data);
        free(file_data);
        return -1;
    }

    // Extract just the filename from the path
    const char* filename = strrchr(file_path, '/');
    if (filename) {
        filename++; // skip the '/'
    } else {
        filename = file_path; // no path separator found
    }
    
    if (strlen(filename) >= 58) {
        perror("Filename too long to add (exceeds 57 characters)");
        free(img_data);
        free(file_data);
        return -1;
    }
    
    // Create directory entry
    dirent64_t* new_entry = &root_entries[free_entry_idx];
    memset(new_entry, 0, sizeof(dirent64_t));
    new_entry->inode_no = free_inode;
    new_entry->type = 1; // file
    strcpy(new_entry->name, filename);
    dirent_checksum_finalize(new_entry);
    
    // Update root inode links count
    inode_t* root_inode = &inode_table[ROOT_INO - 1]; // adjust for 1-indexing
    root_inode->links++;
    root_inode->mtime = (uint64_t)current_time;
    inode_crc_finalize(root_inode);
    
    // Update superblock timestamp and checksum
    sb->mtime_epoch = (uint64_t)current_time;
    superblock_crc_finalize(sb);

    // Write updated image to output file
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        perror("Cannot open output image file");
        free(img_data);
        free(file_data);
        return -1;
    }
    
    if (fwrite(img_data, 1, img_size, output_file) != (size_t)img_size) {
        perror("Failed to write updated image data");
        free(img_data);
        free(file_data);
        fclose(output_file);
        return -1;
    }
    
    fclose(output_file);
    free(img_data);
    free(file_data);
    
    printf("Successfully added file %s to filesystem\n", filename);
    printf("Used inode %u and %lu data blocks\n", free_inode, blocks_needed);
    
    return 0;

}

int main(int argc, char* argv[]) {
    crc32_init();
    
    char* input_path;
    char* output_path;
    char* file_path;
    
    if (parse_args(argc, argv, &input_path, &output_path, &file_path) != 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (add_file_to_filesystem(input_path, output_path, file_path) != 0) {
        return 1;
    }
    
    return 0;
}
