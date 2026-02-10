# MiniVSFS - Mini Virtual Simple File System

A lightweight implementation of a custom file system in C, featuring superblock management, inode structures, directory entries, and bitmap-based block allocation.

## Project Overview

**MiniVSFS** is an educational file system implementation that demonstrates core file system concepts, including:

- **Superblock**: Metadata about the file system (magic number, version, block size, inode count, etc.)
- **Inode Table**: File/directory metadata with 12 direct blocks and indirect block pointers
- **Bitmap Management**: Efficient tracking of free/used inode and data blocks
- **Directory Entries**: 64-byte fixed-size directory entries with CRC32 checksums
- **Block-based Storage**: 4096-byte block size (configurable)
- **Integrity Verification**: CRC32 checksums for data integrity

## Project Structure

```
MiniVSFS/
├── README.md                          # This file
├── Main Frame/                        # Original skeleton files and reference
│   ├── mkfs_adder_skeleton.c          # Skeleton for file addition tool
│   ├── mkfs_builder_skeleton.c        # Skeleton for file system builder
│   ├── file_*.txt                     # Test/reference files
│   └── ...
└── Work/                              # Development and implementation files
    ├── mkfs_adder_skeleton_V1.c       # Version 1 of adder implementation
    ├── mkfs_adder_test.c              # Testing code for adder
    ├── mkfs_adder_review.c            # Code review version
    ├── mkfs_adder_final.c             # Final adder implementation
    ├── builder_skeleton_V0.c          # Version 0 of builder
    ├── mkfs_builder_final.c           # Final builder implementation
    └── Final/                         # Production-ready versions
        ├── mkfs_adder_final.c
        └── mkfs_builder_final.c
```

## Components

### 1. **mkfs_builder**
Creates a new MiniVSFS file system image from scratch.

**Key Features:**
- Initializes superblock with file system metadata
- Creates inode and data bitmaps
- Sets up the root directory inode
- Formats the file system image

**Compile & Run:**
```bash
gcc -O2 -std=c17 -Wall -Wextra mkfs_builder_final.c -o mkfs_builder
./mkfs_builder <disk_image> <size_in_blocks> [seed]
```

### 2. **mkfs_adder**
Adds files to an existing MiniVSFS file system image.

**Key Features:**
- Allocates inodes and data blocks
- Adds files to directory entries
- Updates superblock and bitmap information
- Verifies file system integrity with CRC32 checksums

**Compile & Run:**
```bash
gcc -O2 -std=c17 -Wall -Wextra mkfs_adder_final.c -o mkfs_adder
./mkfs_adder <disk_image> <file_path> [repeat_count]
```

## Data Structures

### Superblock
- **Size**: 116 bytes
- **Content**: Magic number, version, block size, partition info, timestamps
- **Checksum**: CRC32 for validation

### Inode
- **Size**: 128 bytes (INODE_SIZE)
- **Content**: File metadata, size, timestamps, block pointers
- **Structure**: 12 direct blocks + 1 indirect block
- **Checksum**: CRC32 for data integrity

### Directory Entry
- **Size**: 64 bytes (dirent64_t)
- **Content**: Filename, inode number, file type, permissions
- **Checksum**: XOR checksum of all bytes

## Building Instructions

### Prerequisites
- GCC compiler with C17 support
- Standard C library (libc)

### Compile All Tools
```bash
cd Work/Final/
gcc -O2 -std=c17 -Wall -Wextra mkfs_builder_final.c -o mkfs_builder
gcc -O2 -std=c17 -Wall -Wextra mkfs_adder_final.c -o mkfs_adder
```

### Quick Start
```bash
# Create a new file system (1024 blocks)
./mkfs_builder myfs.img 1024 12345

# Add a file to the file system
./mkfs_adder myfs.img file.txt

# Add the same file multiple times
./mkfs_adder myfs.img file.txt 5
```

## Development Notes

- **Skeleton Files**: Located in `Main Frame/` for reference implementation
- **Versions**: Multiple iterations in `Work/` showing development progress
- **Test Files**: `mkfs_adder_test.c` for testing functionality
- **Final Code**: Production-ready versions in `Work/Final/`

## Key Specifications

| Item | Value |
|------|-------|
| Block Size | 4096 bytes |
| Inode Size | 128 bytes |
| Direct Blocks per Inode | 12 |
| Directory Entry Size | 64 bytes |
| Root Inode Number | 1 |
| Superblock Size | 116 bytes (fits in 1 block) |

## Error Handling

Both tools include comprehensive error checking:
- File I/O validation
- Block allocation verification
- Corruption detection via checksums
- Detailed error messages

## Notes

- All structures use `#pragma pack(1)` for precise memory layout
- 64-bit file offset support for large disk images
- CRC32 initialization required before use
- Bitmap operations ensure no block duplication

---

For implementation details, refer to the source code comments and test files.
