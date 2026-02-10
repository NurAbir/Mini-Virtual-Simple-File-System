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
#include <getopt.h>
#include <sys/stat.h>
#include <unistd.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12

#pragma pack(push, 1)
typedef struct
{
    // start - Mehedi
    uint32_t magic;      // 0x4D565346
    uint32_t version;    // 1
    uint32_t block_size; // 4096

    uint64_t total_blocks;       // size_kib * 1024 / 4096
    uint64_t inode_count;        // number of inodes
    uint64_t inode_bitmap_start; // block number where inode bitmap starts

    uint64_t inode_bitmap_blocks; // number of blocks for inode bitmap (1)
    uint64_t data_bitmap_start;   // block number where data bitmap starts
    uint64_t data_bitmap_blocks;  // number of blocks for data bitmap (1)
    uint64_t inode_table_start;   // block number where inode table starts

    uint64_t inode_table_blocks; // number of blocks for inode table
    uint64_t data_region_start;  // block number where data region starts
    uint64_t data_region_blocks; // number of blocks in data region
    uint64_t root_inode;         // 1
    uint64_t mtime_epoch;        // build time

    uint32_t flags; // 0
    // end - Mehedi

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint32_t checksum; // crc32(superblock[0..4091])
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push, 1)
typedef struct
{
    // start - Mehedi
    uint16_t mode;  // file type and permissions
    uint16_t links; // number of hard links

    uint32_t uid; // user id (0)
    uint32_t gid; // group id (0)

    uint64_t size_bytes; // size in bytes
    uint64_t atime;      // access time
    uint64_t mtime;      // modification time
    uint64_t ctime;      // creation time

    uint32_t direct[DIRECT_MAX]; // direct block pointers
    uint32_t reserved_0;         // 0
    uint32_t reserved_1;         // 0
    uint32_t reserved_2;         // 0

    uint32_t proj_id;     // group id
    uint32_t uid16_gid16; // 0
    uint64_t xattr_ptr;   // 0
    // end - Mehedi

    // THIS FIELD SHOULD STAY AT THE END
    // ALL OTHER FIELDS SHOULD BE ABOVE THIS
    uint64_t inode_crc; // low 4 bytes store crc32 of bytes [0..119]; high 4 bytes 0

} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size mismatch");

#pragma pack(push, 1)
typedef struct
{
    uint32_t inode_no; // inode number (0 if free)
    uint8_t type;      // 1=file, 2=dir
    char name[58];     // filename (null-terminated)
    uint8_t checksum;  // XOR of bytes 0..62
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t) == 64, "dirent size mismatch");

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================
uint32_t CRC32_TAB[256];
void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        CRC32_TAB[i] = c;
    }
}
uint32_t crc32(const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = CRC32_TAB[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb)
{
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void inode_crc_finalize(inode_t *ino)
{
    uint8_t tmp[INODE_SIZE];
    memcpy(tmp, ino, INODE_SIZE);
    // zero crc area before computing
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c; // low 4 bytes carry the crc
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
void dirent_checksum_finalize(dirent64_t *de)
{
    const uint8_t *p = (const uint8_t *)de;
    uint8_t x = 0;
    for (int i = 0; i < 63; i++)
        x ^= p[i]; // covers ino(4) + type(1) + name(58)
    de->checksum = x;
}

// start - Mehedi
// Additional function for printing usage information (Text files given to our group)
void print_usage(const char *program_name)
{
    fprintf(stderr, "Usage: %s --input <input.img> --output <output.img> --file <filename>\n", program_name);
}
// end - Mehedi

int main(int argc, char *argv[])
{
    crc32_init();
    // WRITE YOUR DRIVER CODE HERE
    // PARSE YOUR CLI PARAMETERS
    // THEN CREATE YOUR FILE SYSTEM WITH A ROOT DIRECTORY
    // THEN SAVE THE DATA INSIDE THE OUTPUT IMAGE

    // start - Mehedi
    superblock_t sb;
    memset(&sb, 0, sizeof(superblock_t));

    sb.magic = 0x4D565346; // "MVFS"
    sb.version = 1;
    sb.block_size = BS;

    sb.total_blocks = TOTAL_BlOCKS; // from input
    sb.inode_count = INODES_COUNT;

    sb.inode_bitmap_start = 1;  // block 1
    sb.inode_bitmap_blocks = 1; // 1 block for inode bitmap

    sb.data_region_start = 2;  // block 2
    sb.data_bitmap_blocks = 1; // 1 block for data bitmap

    sb.inode_table_start = 3; // block 3
    sb.inode_table_blocks = (sb.inode_count * INODE_SIZE + BS - 1) / BS;

    sb.data_region_start = sb.inode_table_start + sb.inode_table_blocks; // block 7
    sb.data_region_blocks = TOTAL_BLOCKS - sb.data_region_start;         // rest of the blocks for data region

    sb.root_inode = ROOT_INO;              // inode 1
    sb.mtime_epoch = (uint64_t)time(NULL); // build time
    sb.flags = 0;

    superblock_crc_finalize(&sb);

    char *input_img = NULL;
    char *output_img = NULL;
    char *file_to_add = NULL;

    // Parse command line arguments using getopt_long
    static struct option long_options[] = {
        {"input", required_argument, 0, 'i'},
        {"output", required_argument, 0, 'o'},
        {"file", required_argument, 0, 'f'},
        {0, 0, 0, 0}};

    int option_index = 0;
    int c;

    while ((c = getopt_long(argc, argv, "i:o:f:", long_options, &option_index)) != -1)
    {
        switch (c)
        {
        case 'i':
            input_img = optarg;
            break;
        case 'o':
            output_img = optarg;
            break;
        case 'f':
            file_to_add = optarg;
            break;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    // Validate required arguments
    if (!input_img || !output_img || !file_to_add)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Open input image
    FILE *fin = fopen(input_img, "rb");
    if (!fin)
    {
        fprintf(stderr, "Error: Cannot open input image %s: %s\n", input_img, strerror(errno));
        return 1;
    }

    // Load superblock
    superblock_t sb;
    if (fread(&sb, sizeof(sb), 1, fin) != 1)
    {
        fprintf(stderr, "Error: Failed to read superblock\n");
        fclose(fin);
        return 1;
    }

    // Verify magic number
    if (sb.magic != 0x4D565346)
    {
        fprintf(stderr, "Error: Invalid filesystem magic number\n");
        fclose(fin);
        return 1;
    }

    fseek(fin, 0, SEEK_SET);

    // Check if file exists and get its size
    struct stat st;
    if (stat(file_to_add, &st) < 0)
    {
        fprintf(stderr, "Error: Cannot access file %s: %s\n", file_to_add, strerror(errno));
        free(img);
        return 1;
    }

    size_t fsize = st.st_size;

    // Get pointers to bitmaps
    uint8_t *inode_bmap = img + sb.inode_bitmap_start * BS;
    uint8_t *data_bmap = img + sb.data_bitmap_start * BS;

    // Find free inode
    int free_ino = -1;
    for (uint64_t i = 0; i < sb.inode_count; i++)
    {
        int byte = i / 8;
        int bit = i % 8;
        if (!(inode_bmap[byte] & (1 << bit)))
        {
            free_ino = i + 1; // inodes are 1-indexed
            inode_bmap[byte] |= (1 << bit);
            break;
        }
    }

    if (free_ino == -1)
    {
        fprintf(stderr, "Error: No free inodes available\n");
        free(img);
        free(fbuf);
        return 1;
    }
    // Work in Progress
    // end - Mehedi
    return 0;
}