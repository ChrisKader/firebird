#include "memory/nand_fs.h"
#include <cstring>
#include <algorithm>
#include <map>

// Max filesystem nodes to prevent runaway parsing on corrupt data
static const size_t MAX_FS_NODES = 10000;
// Max file size we'll attempt to read (64 MB) - prevents multi-GB allocations on corrupt inodes
static const size_t MAX_FILE_SIZE = 64 * 1024 * 1024;
// Max data block pointers per node
static const size_t MAX_DATA_BLOCKS = 16384;

// -------------------- Helpers --------------------

// Read a 32-bit little-endian value from a buffer
static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

// Convert offset within logical filesystem space to physical NAND byte offset.
// Logical space is sequential blocks without spare; physical has spare per page.
static size_t logical_to_physical_offset(const NandFilesystem &fs, size_t partition_offset,
                                          uint32_t logical_block, uint32_t byte_within_block)
{
    if (logical_block >= fs.logical_to_physical.size())
        return SIZE_MAX;

    uint32_t phys_block = fs.logical_to_physical[logical_block];
    if (phys_block == UINT32_MAX)
        return SIZE_MAX;

    // Physical block start in nand_data
    size_t phys_block_start = partition_offset + (size_t)phys_block * fs.page_size * fs.pages_per_block;

    // Compute which page within the block, and offset within the page
    uint32_t page_in_block = byte_within_block / fs.data_per_page;
    uint32_t off_in_page = byte_within_block % fs.data_per_page;

    if (page_in_block >= fs.pages_per_block)
        return SIZE_MAX;

    return phys_block_start + (size_t)page_in_block * fs.page_size + off_in_page;
}

// Read bytes from a logical block in the filesystem
static bool read_logical(const NandFilesystem &fs, const uint8_t *nand_data, size_t nand_size,
                          uint32_t logical_block, uint32_t offset, uint8_t *dest, size_t len)
{
    if (fs.data_per_page == 0 || fs.pages_per_block == 0)
        return false;

    while (len > 0)
    {
        uint32_t page_off = offset % fs.data_per_page;
        uint32_t chunk = fs.data_per_page - page_off;
        if (chunk > len)
            chunk = (uint32_t)len;

        size_t phys = logical_to_physical_offset(fs, fs.partition_offset, logical_block, offset);
        if (phys == SIZE_MAX || phys + chunk > nand_size)
            return false;

        memcpy(dest, nand_data + phys, chunk);
        dest += chunk;
        offset += chunk;
        len -= chunk;

        // If we cross a block boundary within the same logical block, something is wrong
        if (len > 0 && offset >= fs.data_per_page * fs.pages_per_block)
            return false;
    }
    return true;
}

// Write bytes to a logical block
static bool write_logical(const NandFilesystem &fs, uint8_t *nand_data, size_t nand_size,
                            uint32_t logical_block, uint32_t offset, const uint8_t *src, size_t len)
{
    if (fs.data_per_page == 0 || fs.pages_per_block == 0)
        return false;

    while (len > 0)
    {
        uint32_t page_off = offset % fs.data_per_page;
        uint32_t chunk = fs.data_per_page - page_off;
        if (chunk > len)
            chunk = (uint32_t)len;

        size_t phys = logical_to_physical_offset(fs, fs.partition_offset, logical_block, offset);
        if (phys == SIZE_MAX || phys + chunk > nand_size)
            return false;

        memcpy(nand_data + phys, src, chunk);
        src += chunk;
        offset += chunk;
        len -= chunk;

        if (len > 0 && offset >= fs.data_per_page * fs.pages_per_block)
            return false;
    }
    return true;
}

// Read data from a Reliance filesystem block.
// Reliance block numbers use fs.block_size, which may differ from NAND block data size.
// fs.reliance_nand_base is the NAND data-byte offset where Reliance byte 0 lives.
// This translates Reliance block+offset -> absolute NAND data-byte -> NAND logical block reads.
static bool read_fs_block(const NandFilesystem &fs, const uint8_t *nand_data, size_t nand_size,
                           uint32_t fs_block, uint32_t offset_in_fs_block,
                           uint8_t *dest, size_t len)
{
    if (fs.block_size == 0) return false;
    uint32_t nand_block_data = fs.data_per_page * fs.pages_per_block;
    if (nand_block_data == 0) return false;

    // Compute absolute byte offset in the NAND data stream (from partition start)
    size_t byte_offset = fs.reliance_nand_base
                       + (size_t)fs_block * (size_t)fs.block_size
                       + offset_in_fs_block;

    while (len > 0)
    {
        uint32_t nand_block = (uint32_t)(byte_offset / nand_block_data);
        uint32_t nand_offset = (uint32_t)(byte_offset % nand_block_data);
        uint32_t chunk = nand_block_data - nand_offset;
        if (chunk > len) chunk = (uint32_t)len;

        if (!read_logical(fs, nand_data, nand_size, nand_block, nand_offset, dest, chunk))
            return false;

        dest += chunk;
        byte_offset += chunk;
        len -= chunk;
    }
    return true;
}

// Write data to a Reliance filesystem block (inverse of read_fs_block).
static bool write_fs_block(const NandFilesystem &fs, uint8_t *nand_data, size_t nand_size,
                            uint32_t fs_block, uint32_t offset_in_fs_block,
                            const uint8_t *src, size_t len)
{
    if (fs.block_size == 0) return false;
    uint32_t nand_block_data = fs.data_per_page * fs.pages_per_block;
    if (nand_block_data == 0) return false;

    size_t byte_offset = fs.reliance_nand_base
                       + (size_t)fs_block * (size_t)fs.block_size
                       + offset_in_fs_block;

    while (len > 0)
    {
        uint32_t nand_block = (uint32_t)(byte_offset / nand_block_data);
        uint32_t nand_offset = (uint32_t)(byte_offset % nand_block_data);
        uint32_t chunk = nand_block_data - nand_offset;
        if (chunk > len) chunk = (uint32_t)len;

        if (!write_logical(fs, nand_data, nand_size, nand_block, nand_offset, src, chunk))
            return false;

        src += chunk;
        byte_offset += chunk;
        len -= chunk;
    }
    return true;
}

// Convert UTF-16LE bytes to UTF-8 string
static std::string utf16le_to_utf8(const uint8_t *data, size_t byte_len)
{
    std::string result;
    for (size_t i = 0; i + 1 < byte_len; i += 2)
    {
        uint16_t ch = rd16(data + i);
        if (ch == 0)
            break;
        if (ch < 0x80)
            result += (char)ch;
        else if (ch < 0x800)
        {
            result += (char)(0xC0 | (ch >> 6));
            result += (char)(0x80 | (ch & 0x3F));
        }
        else
        {
            result += (char)(0xE0 | (ch >> 12));
            result += (char)(0x80 | ((ch >> 6) & 0x3F));
            result += (char)(0x80 | (ch & 0x3F));
        }
    }
    return result;
}

// -------------------- FlashFX Pro --------------------

// Spare area layout for CX (64-byte spare):
// Bytes 0-1 of spare: allocation status[15:12] + logical address[11:0]
// Byte 2+ of spare: sequence number, etc.
//
// For classic (16-byte spare), similar but smaller.
//
// Unit header signature: 4 copies of same 4 bytes at page offset 0x00-0x0F
// Logical address 0x8E2 marks unit headers

struct FlashFxUnit {
    uint32_t physical_block;
    uint32_t logical_addr;  // 12-bit logical address
    uint32_t sequence;
    uint8_t status;         // Upper 4 bits of spare[0-1]
};

static void flashfx_build_map(const uint8_t *nand_data, size_t nand_size,
                                size_t partition_offset, size_t partition_size,
                                const nand_metrics &metrics,
                                std::vector<uint32_t> &logical_to_physical)
{
    uint32_t page_size = metrics.page_size;
    uint32_t pages_per_block = 1u << metrics.log2_pages_per_block;
    uint32_t block_size_bytes = page_size * pages_per_block;
    uint32_t data_per_page = page_size & ~0x7Fu; // matches flash.cpp convention
    uint32_t spare_offset = data_per_page;       // spare area starts after data
    if (block_size_bytes == 0)
        return;
    uint32_t num_blocks = (uint32_t)(partition_size / block_size_bytes);

    // Track highest sequence per logical address
    struct MapEntry {
        uint32_t phys_block;
        uint32_t sequence;
    };
    std::vector<MapEntry> best_map;

    for (uint32_t blk = 0; blk < num_blocks; blk++)
    {
        size_t block_start = partition_offset + (size_t)blk * block_size_bytes;
        if (block_start + page_size > nand_size)
            break;

        const uint8_t *first_page = nand_data + block_start;

        // Check if this block is erased (first 16 bytes all 0xFF)
        bool erased = true;
        for (int i = 0; i < 16; i++)
        {
            if (first_page[i] != 0xFF)
            {
                erased = false;
                break;
            }
        }
        if (erased)
            continue;

        // Read spare area of first page for allocation info
        if (block_start + spare_offset + 2 > nand_size)
            continue;

        const uint8_t *spare = first_page + spare_offset;
        uint16_t alloc_info = rd16(spare);

        uint8_t status = (alloc_info >> 12) & 0xF;
        uint32_t logical_addr = alloc_info & 0xFFF;

        // Skip unit headers (0x8E2) and bad/unused blocks
        if (logical_addr == 0x8E2 || logical_addr == 0xFFF)
            continue;

        // Status bits: typically 0x5 = valid data block
        // We accept various status values as potentially valid
        if (status == 0xF || status == 0x0)
            continue;

        // Try to read sequence number from the unit header area
        // FlashFX stores sequence at +0x1C in the first page data area
        uint32_t sequence = 0;
        if (block_start + 0x20 <= nand_size)
            sequence = rd32(first_page + 0x1C);

        if (logical_addr >= best_map.size())
            best_map.resize(logical_addr + 1, {UINT32_MAX, 0});

        if (best_map[logical_addr].phys_block == UINT32_MAX ||
            sequence > best_map[logical_addr].sequence)
        {
            best_map[logical_addr] = {blk, sequence};
        }
    }

    logical_to_physical.resize(best_map.size(), UINT32_MAX);
    for (size_t i = 0; i < best_map.size(); i++)
        logical_to_physical[i] = best_map[i].phys_block;
}

// -------------------- Reliance FS --------------------

// Reliance filesystem signatures
static const uint8_t MAST_SIG[] = {'M', 'A', 'S', 'T'};
static const uint8_t INOD_SIG[] = {'I', 'N', 'O', 'D'};

// Forward declarations
static std::vector<uint8_t> read_file_blocks(const NandFilesystem &fs, const uint8_t *nand_data,
                                              size_t nand_size, const NandFsNode &node);
static void parse_directory_data(NandFilesystem &fs, const uint8_t *nand_data, size_t nand_size,
                                  const std::vector<uint8_t> &dir_data, uint32_t parent_inode,
                                  const std::string &parent_path,
                                  const std::map<uint32_t, uint32_t> &inode_to_block, int depth);

// Read all data blocks for a given storage mode and block pointers
static std::vector<uint8_t> read_file_blocks(const NandFilesystem &fs, const uint8_t *nand_data,
                                              size_t nand_size, const NandFsNode &node)
{
    std::vector<uint8_t> result;
    uint32_t block_data_size = fs.data_per_page * fs.pages_per_block;

    // Reject absurdly large sizes (corrupt inode data)
    if (node.size > MAX_FILE_SIZE)
        return result;

    if (node.storage_mode == 0)
    {
        // Inline data: stored in inode block at offset +0x40
        if (node.inode_block == 0 || node.size == 0)
            return result;

        result.resize(node.size);
        if (!read_fs_block(fs, nand_data, nand_size, node.inode_block, 0x40, result.data(), node.size))
        {
            result.clear();
            return result;
        }
        return result;
    }

    // Mode 1/2/3: read from data blocks (Reliance block numbers)
    uint32_t remaining = node.size;
    uint32_t read_unit = fs.block_size ? fs.block_size : block_data_size;
    for (uint32_t blk_ptr : node.data_blocks)
    {
        if (remaining == 0)
            break;

        uint32_t to_read = std::min(remaining, read_unit);
        size_t old_size = result.size();
        result.resize(old_size + to_read);

        if (!read_fs_block(fs, nand_data, nand_size, blk_ptr, 0, result.data() + old_size, to_read))
        {
            // Failed to read block - fill with zeros
            memset(result.data() + old_size, 0, to_read);
        }
        remaining -= to_read;
    }

    return result;
}

// Read block pointers from an INDI (indirect index) block
static std::vector<uint32_t> read_block_pointers(const NandFilesystem &fs,
                                                   const uint8_t *nand_data, size_t nand_size,
                                                   uint32_t block_ptr, uint32_t offset = 0)
{
    std::vector<uint32_t> ptrs;
    if (fs.block_size == 0 || offset >= fs.block_size) return ptrs;
    uint32_t max_ptrs = (fs.block_size - offset) / 4;

    std::vector<uint8_t> block_data(fs.block_size);
    if (!read_fs_block(fs, nand_data, nand_size, block_ptr, 0, block_data.data(), fs.block_size))
        return ptrs;

    for (uint32_t i = 0; i < max_ptrs && ptrs.size() < MAX_DATA_BLOCKS; i++)
    {
        uint32_t p = rd32(block_data.data() + offset + i * 4);
        if (p == 0 || p == UINT32_MAX)
            break;
        ptrs.push_back(p);
    }
    return ptrs;
}

// Read an INOD block and populate a NandFsNode (without name/path/parent - caller sets those).
static bool read_inode_block(NandFilesystem &fs, const uint8_t *nand_data, size_t nand_size,
                              uint32_t inode_num, uint32_t inode_block_ptr, NandFsNode &node)
{
    if (inode_block_ptr == 0 || inode_block_ptr == UINT32_MAX || fs.block_size == 0)
        return false;

    std::vector<uint8_t> inode_data(fs.block_size);
    if (!read_fs_block(fs, nand_data, nand_size, inode_block_ptr, 0, inode_data.data(), fs.block_size))
        return false;

    if (memcmp(inode_data.data(), INOD_SIG, 4) != 0)
        return false;

    node.inode_num = inode_num;
    node.size = rd32(inode_data.data() + 0x08);
    node.mtime = rd32(inode_data.data() + 0x18);

    if (node.size > MAX_FILE_SIZE)
        node.size = 0;

    uint32_t attributes = rd32(inode_data.data() + 0x28);
    node.storage_mode = attributes & 0x3;
    node.inode_block = inode_block_ptr;

    switch (node.storage_mode)
    {
    case 0: // Inline data at +0x40
        break;
    case 1: // Single indirect: block pointers at +0x40
        node.data_blocks = read_block_pointers(fs, nand_data, nand_size, inode_block_ptr, 0x40);
        break;
    case 2: // Double indirect
    {
        auto indi_ptrs = read_block_pointers(fs, nand_data, nand_size, inode_block_ptr, 0x40);
        for (uint32_t indi : indi_ptrs)
        {
            if (node.data_blocks.size() >= MAX_DATA_BLOCKS) break;
            auto data_ptrs = read_block_pointers(fs, nand_data, nand_size, indi);
            node.data_blocks.insert(node.data_blocks.end(), data_ptrs.begin(), data_ptrs.end());
        }
        break;
    }
    case 3: // Triple indirect
    {
        auto dbli_ptrs = read_block_pointers(fs, nand_data, nand_size, inode_block_ptr, 0x40);
        for (uint32_t dbli : dbli_ptrs)
        {
            if (node.data_blocks.size() >= MAX_DATA_BLOCKS) break;
            auto indi_ptrs = read_block_pointers(fs, nand_data, nand_size, dbli);
            for (uint32_t indi : indi_ptrs)
            {
                if (node.data_blocks.size() >= MAX_DATA_BLOCKS) break;
                auto data_ptrs = read_block_pointers(fs, nand_data, nand_size, indi);
                node.data_blocks.insert(node.data_blocks.end(), data_ptrs.begin(), data_ptrs.end());
            }
        }
        break;
    }
    }

    return true;
}

// Parse directory entries from data buffer.
// CX2 Reliance directory entry format (reverse-engineered from real NAND):
//   +0x00: 0x80 magic byte
//   +0x03: entry_length (padded to 16-byte boundary)
//   +0x07: name_byte_length (UTF-16LE bytes, may include null terminator)
//   +0x09: attributes (bit 0 = in-use, bit 1 = directory)
//   +0x0B: child inode number (uint8; high byte at +0x0A for uint16 BE)
//   +0x12: name (UTF-16LE)
static void parse_directory_data(NandFilesystem &fs, const uint8_t *nand_data, size_t nand_size,
                                  const std::vector<uint8_t> &dir_data, uint32_t parent_inode,
                                  const std::string &parent_path,
                                  const std::map<uint32_t, uint32_t> &inode_to_block, int depth)
{
    if (depth > 32 || fs.nodes.size() >= MAX_FS_NODES)
        return;

    size_t pos = 0;
    while (pos + 0x12 < dir_data.size() && fs.nodes.size() < MAX_FS_NODES)
    {
        if (dir_data[pos] != 0x80)
        {
            pos++;
            continue;
        }

        uint8_t entry_len = dir_data[pos + 3];
        if (entry_len < 0x12 || pos + entry_len > dir_data.size())
            break;

        uint8_t name_byte_len = dir_data[pos + 7];
        uint8_t attribs = dir_data[pos + 9];
        // Child inode number: uint16 big-endian at +0x0A (high byte at +0x0A, low byte at +0x0B)
        uint32_t child_inode = ((uint32_t)dir_data[pos + 0x0A] << 8) | dir_data[pos + 0x0B];

        bool in_use = (attribs & 0x01) != 0;
        bool is_dir = (attribs & 0x02) != 0;

        if (in_use && name_byte_len > 0 && child_inode != 0)
        {
            // Read name using cell-aware reader.
            // Directory entries are divided into 16-byte cells:
            //   Cell 0 (entry+0x00..+0x0F): header
            //   Cell 1 (entry+0x10..+0x1F): 2-byte sub-header + 14 bytes name data
            //   Cell 2 (entry+0x20..+0x2F): 2-byte sub-header + 14 bytes name data
            //   ...
            // Name data is fragmented: 14 bytes per cell, with 2-byte continuation
            // headers between cells that must be skipped.
            std::vector<uint8_t> name_buf;
            size_t nb_remaining = name_byte_len;
            size_t cell_off = pos + 0x12; // First cell's name data starts here
            while (nb_remaining > 0 && cell_off < pos + entry_len)
            {
                size_t avail = 14; // Max name bytes per cell
                if (avail > nb_remaining) avail = nb_remaining;
                if (cell_off + avail > pos + entry_len) avail = pos + entry_len - cell_off;
                for (size_t i = 0; i < avail && cell_off + i < dir_data.size(); i++)
                    name_buf.push_back(dir_data[cell_off + i]);
                nb_remaining -= avail;
                cell_off += 14 + 2; // Skip 14 bytes data + 2 bytes next cell header
            }
            if (!name_buf.empty())
            {
                std::string name = utf16le_to_utf8(name_buf.data(), name_buf.size());

                if (!name.empty() && name != "." && name != "..")
                {
                    auto it = inode_to_block.find(child_inode);
                    if (it != inode_to_block.end())
                    {
                        NandFsNode node;
                        if (read_inode_block(fs, nand_data, nand_size, child_inode, it->second, node))
                        {
                            node.parent_inode = parent_inode;
                            node.name = name;
                            node.full_path = parent_path + "/" + name;
                            node.type = is_dir ? NandFsNode::DIR_NODE : NandFsNode::FILE_NODE;
                            fs.nodes.push_back(node);

                            if (is_dir)
                            {
                                auto dir_contents = read_file_blocks(fs, nand_data, nand_size, node);
                                if (!dir_contents.empty())
                                {
                                    parse_directory_data(fs, nand_data, nand_size, dir_contents,
                                                         child_inode, node.full_path,
                                                         inode_to_block, depth + 1);
                                }
                            }
                        }
                    }
                }
            }
        }

        pos += entry_len;
    }
}

NandFilesystem nand_fs_parse(const uint8_t *nand_data, size_t nand_size,
                              size_t partition_offset, size_t partition_size,
                              const nand_metrics &metrics)
{
    auto hex8 = [](uint32_t v) {
        char buf[12]; snprintf(buf, sizeof(buf), "%08X", v); return std::string(buf);
    };

    NandFilesystem fs;
    fs.partition_offset = partition_offset;
    fs.page_size = metrics.page_size;
    fs.data_per_page = metrics.page_size & ~0x7Fu; // matches flash.cpp convention
    fs.pages_per_block = 1u << metrics.log2_pages_per_block;

    if (!nand_data || partition_size == 0 || fs.page_size == 0 ||
        fs.data_per_page == 0 || fs.pages_per_block == 0)
    {
        fs.error = "Invalid NAND metrics (page_size=" + std::to_string(fs.page_size)
                 + " data_per_page=" + std::to_string(fs.data_per_page)
                 + " pages_per_block=" + std::to_string(fs.pages_per_block) + ")";
        return fs;
    }

    uint32_t block_size_phys = fs.page_size * fs.pages_per_block;
    uint32_t num_phys_blocks = block_size_phys ? (uint32_t)(partition_size / block_size_phys) : 0;

    // Step 1: Build FlashFX physical -> logical mapping
    flashfx_build_map(nand_data, nand_size, partition_offset, partition_size,
                      metrics, fs.logical_to_physical);

    bool used_identity_map = false;
    if (fs.logical_to_physical.empty())
    {
        // FlashFX mapping failed -- fall back to 1:1 identity mapping.
        // CX2 SPI NAND may not use FlashFX spare area metadata.
        fs.logical_to_physical.resize(num_phys_blocks);
        for (uint32_t i = 0; i < num_phys_blocks; i++)
            fs.logical_to_physical[i] = i;
        used_identity_map = true;
    }

    uint32_t block_data_size = fs.data_per_page * fs.pages_per_block;
    if (block_data_size == 0)
    {
        fs.error = "block_data_size is 0";
        return fs;
    }

    // Step 2: Find and validate MAST header.
    // MAST contains: sig(4) + ?(4) + block_size(4) + total_blocks(4) + meta_ptr1(4) + meta_ptr2(4)
    // MAST is at Reliance byte offset 0x40. We use the physical location of MAST to compute
    // reliance_nand_base = (NAND data-byte offset of MAST) - 0x40, so that
    // read_fs_block can translate Reliance block numbers to correct NAND locations.
    uint32_t mast_block = UINT32_MAX;
    uint32_t mast_offset_in_block = 0;
    std::vector<uint8_t> mast_data(block_data_size);

    // Validate a MAST candidate given its NAND data-byte offset from partition start.
    // Temporarily sets fs.block_size and fs.reliance_nand_base to test META readability.
    auto validate_mast = [&](size_t mast_nand_data_byte) -> bool {
        uint32_t bs = rd32(mast_data.data() + 0x08);
        uint32_t tb = rd32(mast_data.data() + 0x0C);
        if (bs == 0 || bs > 0x100000 || tb == 0 || tb == UINT32_MAX)
            return false;
        // MAST is at Reliance offset 0x40. Compute Reliance base in NAND data-byte space.
        size_t candidate_base = (mast_nand_data_byte >= 0x40) ? mast_nand_data_byte - 0x40 : 0;
        // Temporarily set fs fields so read_fs_block can work
        uint32_t saved_bs = fs.block_size;
        size_t saved_base = fs.reliance_nand_base;
        fs.block_size = bs;
        fs.reliance_nand_base = candidate_base;
        // Check META pointers at +0x10 and +0x14
        for (uint32_t moff : {0x10u, 0x14u})
        {
            uint32_t mptr = rd32(mast_data.data() + moff);
            if (mptr == 0 || mptr == UINT32_MAX) continue;
            uint8_t probe[16];
            if (!read_fs_block(fs, nand_data, nand_size, mptr, 0, probe, 16))
                continue;
            bool all_ff = true;
            for (int i = 0; i < 16; i++)
                if (probe[i] != 0xFF) { all_ff = false; break; }
            if (!all_ff)
            {
                fs.block_size = saved_bs;
                fs.reliance_nand_base = saved_base;
                return true;
            }
        }
        fs.block_size = saved_bs;
        fs.reliance_nand_base = saved_base;
        return false;
    };

    auto try_mast_at = [&](uint32_t blk, uint32_t off) -> bool {
        if (blk >= fs.logical_to_physical.size()) return false;
        if (fs.logical_to_physical[blk] == UINT32_MAX) return false;
        uint8_t sig[4];
        if (!read_logical(fs, nand_data, nand_size, blk, off, sig, 4)) return false;
        if (memcmp(sig, MAST_SIG, 4) != 0) return false;
        if (!read_logical(fs, nand_data, nand_size, blk, off, mast_data.data(),
                          std::min(block_data_size - off, block_data_size)))
            return false;
        // MAST's NAND data-byte offset from partition start
        size_t mast_byte = (size_t)blk * block_data_size + off;
        if (!validate_mast(mast_byte)) return false;
        mast_block = blk;
        mast_offset_in_block = off;
        // Commit the reliance base
        fs.reliance_nand_base = (mast_byte >= 0x40) ? mast_byte - 0x40 : 0;
        return true;
    };

    // Try documented locations first, then broader scan
    if (!try_mast_at(0, 0x40))
    {
        for (uint32_t b = 0; b < 4 && mast_block == UINT32_MAX; b++)
            try_mast_at(b, 0);

        uint32_t scan_limit = std::min((uint32_t)fs.logical_to_physical.size(), 1024u);
        for (uint32_t i = 0; i < scan_limit && mast_block == UINT32_MAX; i++)
        {
            if (!try_mast_at(i, 0))
                try_mast_at(i, 0x40);
        }
    }

    // Raw page scan as last resort
    if (mast_block == UINT32_MAX)
    {
        uint32_t pages_in_part = (uint32_t)(partition_size / fs.page_size);
        uint32_t max_scan_pages = std::min(pages_in_part, 65536u);
        for (uint32_t pg = 0; pg < max_scan_pages; pg++)
        {
            size_t page_phys = partition_offset + (size_t)pg * fs.page_size;
            for (uint32_t off : {0u, 0x40u})
            {
                if (page_phys + off + 4 > nand_size) continue;
                if (memcmp(nand_data + page_phys + off, MAST_SIG, 4) != 0) continue;

                size_t mast_phys = page_phys + off;
                size_t to_read = std::min((size_t)block_data_size, nand_size - mast_phys);
                memcpy(mast_data.data(), nand_data + mast_phys, to_read);
                if (to_read < (size_t)block_data_size)
                    memset(mast_data.data() + to_read, 0, block_data_size - to_read);

                // Compute the NAND data-byte offset of this MAST within the partition
                uint32_t blk_in_part = pg / fs.pages_per_block;
                uint32_t page_in_blk = pg % fs.pages_per_block;
                size_t mast_byte = (size_t)blk_in_part * block_data_size
                                 + (size_t)page_in_blk * fs.data_per_page + off;

                if (validate_mast(mast_byte))
                {
                    mast_block = blk_in_part;
                    mast_offset_in_block = page_in_blk * fs.data_per_page + off;
                    fs.reliance_nand_base = (mast_byte >= 0x40) ? mast_byte - 0x40 : 0;
                    break;
                }
            }
            if (mast_block != UINT32_MAX) break;
        }
    }

    if (mast_block == UINT32_MAX)
    {
        // Scan for any MAST signature to show in diagnostics (even invalid ones)
        std::string diag;
        uint32_t found_count = 0;
        uint32_t pages_in_part = (uint32_t)(partition_size / fs.page_size);
        for (uint32_t pg = 0; pg < std::min(pages_in_part, 65536u) && found_count < 5; pg++)
        {
            size_t page_phys = partition_offset + (size_t)pg * fs.page_size;
            for (uint32_t off : {0u, 0x40u})
            {
                if (page_phys + off + 32 > nand_size) continue;
                if (memcmp(nand_data + page_phys + off, MAST_SIG, 4) != 0) continue;
                diag += "\n  MAST sig at page " + std::to_string(pg) + "+0x" + hex8(off) + ":";
                for (int i = 0; i < 32; i += 4)
                    diag += " " + hex8(rd32(nand_data + page_phys + off + i));
                found_count++;
            }
        }
        fs.error = "No valid MAST found (scanned " + std::to_string(fs.logical_to_physical.size())
                 + " logical blocks + raw pages"
                 + ", identity_map=" + (used_identity_map ? "yes" : "no")
                 + ", part_off=0x" + hex8((uint32_t)partition_offset)
                 + ", part_size=0x" + hex8((uint32_t)partition_size) + ")"
                 + (diag.empty() ? "\n  No MAST signatures found at all" : diag);
        return fs;
    }

    // Dump validated MAST header for diagnostics
    std::string mast_dump;
    for (uint32_t i = 0; i < 64 && i < block_data_size; i += 4)
        mast_dump += " " + hex8(rd32(mast_data.data() + i));

    // Parse MAST: The Reliance MAST format (from hackspire wiki):
    //  +0x00: "MAST" signature (4 bytes)
    //  +0x04: version? / counter?
    //  +0x08: block_size (Reliance logical block size)
    //  +0x0C: total_blocks
    //  +0x10: meta_ptr_1 (Reliance block number of META copy 1)
    //  +0x14: meta_ptr_2 (Reliance block number of META copy 2)
    fs.block_size = rd32(mast_data.data() + 0x08);
    fs.total_blocks = rd32(mast_data.data() + 0x0C);
    uint32_t meta_ptr_1 = rd32(mast_data.data() + 0x10);
    uint32_t meta_ptr_2 = rd32(mast_data.data() + 0x14);

    if (fs.block_size == 0 || fs.total_blocks == 0)
    {
        fs.error = "MAST at block " + std::to_string(mast_block)
                 + "+0x" + hex8(mast_offset_in_block)
                 + " has invalid block_size=" + std::to_string(fs.block_size)
                 + " total_blocks=" + std::to_string(fs.total_blocks)
                 + "\nMAST dump:" + mast_dump;
        return fs;
    }

    // Step 3: Read META (pick newer of two copies based on counter at +0x04)
    uint32_t meta_block = meta_ptr_1;
    std::vector<uint8_t> meta1(fs.block_size), meta2(fs.block_size);

    bool have_meta1 = read_fs_block(fs, nand_data, nand_size, meta_ptr_1, 0, meta1.data(), fs.block_size);
    bool have_meta2 = read_fs_block(fs, nand_data, nand_size, meta_ptr_2, 0, meta2.data(), fs.block_size);

    std::vector<uint8_t> *meta = nullptr;
    if (have_meta1 && have_meta2)
    {
        uint32_t counter1 = rd32(meta1.data() + 0x04);
        uint32_t counter2 = rd32(meta2.data() + 0x04);
        meta = (counter2 > counter1) ? &meta2 : &meta1;
        meta_block = (counter2 > counter1) ? meta_ptr_2 : meta_ptr_1;
    }
    else if (have_meta1)
        meta = &meta1;
    else if (have_meta2)
    {
        meta = &meta2;
        meta_block = meta_ptr_2;
    }
    else
    {
        fs.error = "META blocks not readable"
                 " (ptr1=" + std::to_string(meta_ptr_1)
                 + " ptr2=" + std::to_string(meta_ptr_2)
                 + " map_size=" + std::to_string(fs.logical_to_physical.size()) + ")"
                 + "\nMAST at block " + std::to_string(mast_block)
                 + "+0x" + hex8(mast_offset_in_block)
                 + "\nMAST dump:" + mast_dump;
        return fs;
    }

    (void)meta_block;

    // Check if META is all FF (erased) -- common problem when MAST field offsets are wrong
    bool meta_all_ff = true;
    for (uint32_t i = 0; i < fs.block_size && meta_all_ff; i++)
        if ((*meta)[i] != 0xFF) meta_all_ff = false;

    if (meta_all_ff)
    {
        // META data is all erased. Likely wrong MAST field offsets or wrong block mapping.
        // Show full MAST dump to help debug.
        fs.error = std::string("META data is all 0xFF (erased)")
                 + "\nmeta_ptr_1=" + std::to_string(meta_ptr_1)
                 + " meta_ptr_2=" + std::to_string(meta_ptr_2)
                 + " block_size=" + std::to_string(fs.block_size)
                 + " total_blocks=" + std::to_string(fs.total_blocks)
                 + "\nreliance_nand_base=0x" + hex8((uint32_t)fs.reliance_nand_base)
                 + "\nMAST at NAND block " + std::to_string(mast_block)
                 + " offset 0x" + hex8(mast_offset_in_block)
                 + " (identity_map=" + (used_identity_map ? "yes" : "no") + ")";
        return fs;
    }

    // Step 4: Scan all Reliance blocks for INOD signatures to build inode table.
    // Each INOD block has: "INOD" (4 bytes) + inode_number (uint32 LE at +0x04).
    // Multiple blocks may share the same inode number (copy-on-write).
    // The newest version is at the highest block number.
    std::map<uint32_t, uint32_t> inode_to_block; // inode_num -> newest Reliance block
    for (uint32_t b = 0; b < fs.total_blocks; b++)
    {
        uint8_t hdr[8];
        if (!read_fs_block(fs, nand_data, nand_size, b, 0, hdr, 8))
            continue;
        if (memcmp(hdr, INOD_SIG, 4) != 0)
            continue;
        uint32_t inum = rd32(hdr + 4);
        inode_to_block[inum] = b; // highest block number wins (newest version)
    }

    if (inode_to_block.empty())
    {
        fs.error = "No INOD blocks found (scanned " + std::to_string(fs.total_blocks) + " blocks)"
                 + "\nblock_size=" + std::to_string(fs.block_size)
                 + " reliance_base=0x" + hex8((uint32_t)fs.reliance_nand_base);
        return fs;
    }

    // Step 5: Parse root directory (inode 2)
    auto root_it = inode_to_block.find(fs.root_inode);
    if (root_it == inode_to_block.end())
    {
        std::string found_inodes;
        int count = 0;
        for (auto &[inum, blkn] : inode_to_block)
        {
            if (count++ < 20)
                found_inodes += " " + std::to_string(inum);
        }
        fs.error = "Root inode " + std::to_string(fs.root_inode) + " not found"
                 + " (" + std::to_string(inode_to_block.size()) + " inodes found:" + found_inodes + ")";
        return fs;
    }

    NandFsNode root_node;
    if (!read_inode_block(fs, nand_data, nand_size, fs.root_inode, root_it->second, root_node))
    {
        fs.error = "Failed to read root inode block " + std::to_string(root_it->second);
        return fs;
    }

    root_node.parent_inode = 0;
    root_node.name = "/";
    root_node.full_path = "/";
    root_node.type = NandFsNode::DIR_NODE;
    fs.nodes.push_back(root_node);

    // Parse root directory entries recursively
    auto root_data = read_file_blocks(fs, nand_data, nand_size, root_node);
    if (!root_data.empty())
    {
        parse_directory_data(fs, nand_data, nand_size, root_data, fs.root_inode, "",
                             inode_to_block, 0);
    }

    fs.valid = !fs.nodes.empty();
    return fs;
}

std::vector<uint8_t> nand_fs_read_file(const NandFilesystem &fs,
                                        const NandFsNode &node,
                                        const uint8_t *nand_data,
                                        size_t nand_size)
{
    return read_file_blocks(fs, nand_data, nand_size, node);
}

bool nand_fs_write_file(const NandFilesystem &fs,
                         const NandFsNode &node,
                         const uint8_t *file_data, size_t file_size,
                         uint8_t *nand_data, size_t nand_size)
{
    if (fs.block_size == 0)
        return false;

    if (node.storage_mode == 0)
    {
        // Inline: overwrite data at inode block +0x40
        uint32_t max_inline = fs.block_size > 0x40 ? fs.block_size - 0x40 : 0;
        if (file_size > max_inline || node.inode_block == 0)
            return false;

        if (!write_fs_block(fs, nand_data, nand_size, node.inode_block, 0x40,
                            file_data, file_size))
            return false;

        // Zero out remaining space after new data (within original size)
        if (file_size < node.size)
        {
            std::vector<uint8_t> zeros(node.size - file_size, 0);
            write_fs_block(fs, nand_data, nand_size, node.inode_block,
                           0x40 + (uint32_t)file_size, zeros.data(), zeros.size());
        }

        return true;
    }

    // Mode 1/2/3: write to data blocks (Reliance block numbers)
    uint32_t write_unit = fs.block_size;
    size_t max_capacity = (size_t)node.data_blocks.size() * write_unit;
    if (file_size > max_capacity)
        return false;

    size_t remaining = file_size;
    size_t src_off = 0;
    for (size_t i = 0; i < node.data_blocks.size() && remaining > 0; i++)
    {
        uint32_t to_write = std::min((uint32_t)remaining, write_unit);
        if (!write_fs_block(fs, nand_data, nand_size, node.data_blocks[i], 0,
                            file_data + src_off, to_write))
            return false;
        src_off += to_write;
        remaining -= to_write;
    }

    return true;
}

const NandFsNode *NandFilesystem::find(const std::string &path) const
{
    for (const auto &node : nodes)
    {
        if (node.full_path == path)
            return &node;
    }
    // Try with leading slash
    if (!path.empty() && path[0] != '/')
    {
        std::string with_slash = "/" + path;
        for (const auto &node : nodes)
        {
            if (node.full_path == with_slash)
                return &node;
        }
    }
    return nullptr;
}

std::vector<const NandFsNode *> NandFilesystem::children(uint32_t parent_inode) const
{
    std::vector<const NandFsNode *> result;
    for (const auto &node : nodes)
    {
        if (node.parent_inode == parent_inode && node.inode_num != parent_inode)
            result.push_back(&node);
    }
    return result;
}
