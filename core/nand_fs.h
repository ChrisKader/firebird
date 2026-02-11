#ifndef NAND_FS_H
#define NAND_FS_H

#include <cstdint>
#include <string>
#include <vector>
#include "flash.h"

struct NandFsNode {
    uint32_t inode_num;
    uint32_t parent_inode;
    enum NodeType { FILE_NODE, DIR_NODE } type;
    std::string name;
    std::string full_path;
    uint32_t size;
    uint32_t mtime;
    uint8_t storage_mode; // 0=inline, 1=single indirect, 2=double indirect, 3=triple indirect
    std::vector<uint32_t> data_blocks; // Reliance FS block numbers for non-inline data
    uint32_t inode_block;              // Reliance FS block number of the INOD (for inline reads at +0x40)
};

struct NandFilesystem {
    bool valid = false;
    uint32_t block_size = 0;
    uint32_t total_blocks = 0;
    size_t partition_offset = 0;
    uint32_t page_size = 0;       // Full page size (data + spare)
    uint32_t data_per_page = 0;   // Data bytes per page (page_size & ~0x7F)
    uint32_t pages_per_block = 0;
    size_t reliance_nand_base = 0; // NAND data-byte offset where Reliance byte 0 lives
    std::vector<uint32_t> logical_to_physical; // FlashFX mapping: logical block -> physical block
    std::vector<NandFsNode> nodes;
    uint32_t root_inode = 2; // Usually inode 2
    std::string error;       // Diagnostic: why parsing failed

    const NandFsNode *find(const std::string &path) const;
    std::vector<const NandFsNode *> children(uint32_t parent_inode) const;
};

// Parse the filesystem from NAND data. partition_offset/size in bytes within nand_data.
NandFilesystem nand_fs_parse(const uint8_t *nand_data, size_t nand_size,
                              size_t partition_offset, size_t partition_size,
                              const nand_metrics &metrics);

// Read file contents from NAND. Returns the file data.
std::vector<uint8_t> nand_fs_read_file(const NandFilesystem &fs,
                                        const NandFsNode &node,
                                        const uint8_t *nand_data,
                                        size_t nand_size);

// Write file contents back to NAND (in-place, must not exceed original allocated blocks).
// Returns false if file would exceed allocated space.
bool nand_fs_write_file(const NandFilesystem &fs,
                         const NandFsNode &node,
                         const uint8_t *file_data, size_t file_size,
                         uint8_t *nand_data, size_t nand_size);

#endif // NAND_FS_H
