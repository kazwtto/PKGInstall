// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstring>
#include <optional>
#include <zlib.h>

#include "io_file.h"
#include "path_utils.h"
#include "pkg.h"
#include "pkg_type.h"
#include <zarchive/zarchivewriter.h>

namespace {

constexpr u32 kPkgMagic = 0x7F434E54;
constexpr u32 kPfscMagic = 0x43534650;
constexpr u64 kPfsBlockSize = 0x10000;
constexpr u64 kXtsSectorSize = 0x1000;
constexpr size_t kDirentHeaderSize = 16;

bool DecompressPFSC(std::span<const char> compressed_data, std::span<char> decompressed_data) {
    z_stream decompressStream{};
    decompressStream.avail_in = static_cast<uInt>(compressed_data.size());
    decompressStream.next_in =
        reinterpret_cast<Bytef*>(const_cast<char*>(compressed_data.data()));
    decompressStream.avail_out = static_cast<uInt>(decompressed_data.size());
    decompressStream.next_out = reinterpret_cast<Bytef*>(decompressed_data.data());

    if (inflateInit(&decompressStream) != Z_OK) {
        return false;
    }

    const int inflateRet = inflate(&decompressStream, Z_FINISH);
    const int endRet = inflateEnd(&decompressStream);
    return (inflateRet == Z_STREAM_END || inflateRet == Z_OK) && endRet == Z_OK;
}

bool DecompressSector(std::span<const char> sectorData, std::span<char> outBlock) {
    if (outBlock.size() < kPfsBlockSize || sectorData.size() > kPfsBlockSize) {
        return false;
    }
    if (sectorData.size() == kPfsBlockSize) {
        std::memcpy(outBlock.data(), sectorData.data(), kPfsBlockSize);
        return true;
    }
    if (sectorData.empty()) {
        return false;
    }
    return DecompressPFSC(sectorData, outBlock);
}

std::optional<u32> GetPFSCOffset(std::span<const u8> pfs_image) {
    u32 value = 0;
    for (size_t i = 0x20000; i + sizeof(u32) <= pfs_image.size(); i += 0x10000) {
        std::memcpy(&value, &pfs_image[i], sizeof(u32));
        if (value == kPfscMagic) {
            return static_cast<u32>(i);
        }
    }
    return std::nullopt;
}

bool IsSafeSingleComponent(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos ||
        name.find('\0') != std::string_view::npos) {
        return false;
    }
#ifdef _WIN32
    if (name.find(':') != std::string_view::npos) {
        return false;
    }
#endif
    return true;
}

bool ReadPKGEntryAt(Common::FS::IOFile& file, u64 offset, PKGEntry& entry) {
    if (!file.Seek(static_cast<s64>(offset))) {
        return false;
    }
    return file.Read(entry.id) == 1 && file.Read(entry.filename_offset) == 1 &&
           file.Read(entry.flags1) == 1 && file.Read(entry.flags2) == 1 &&
           file.Read(entry.offset) == 1 && file.Read(entry.size) == 1 &&
           file.Read(entry.padding) == 1;
}

struct ZarWriteState {
    FILE* file = nullptr;
    bool writeError = false;
};

} // namespace

PKG::PKG() = default;

PKG::~PKG() = default;

void PKG::ResetOpenState() {
    pkgSize = 0;
    std::memset(pkgTitleID, 0, sizeof(pkgTitleID));
    pkgheader = {};
    pkgFlags.clear();
    sfo.clear();
    icon0.clear();
}

void PKG::ResetExtractState() {
    extractPaths.clear();
    fsTable.clear();
    iNodeBuf.clear();
    sectorMap.clear();
    decNp.clear();
    pfsc_offset = 0;
}

bool PKG::ValidateAndReadHeader(Common::FS::IOFile& file, std::string& failreason) {
    if (!file.IsOpen()) {
        failreason = "Failed to open PKG file";
        return false;
    }

    pkgSize = file.GetSize();
    if (pkgSize < sizeof(PKGHeader)) {
        failreason = "PKG file is too small";
        return false;
    }

    if (!file.Seek(0) || file.ReadRaw<u8>(&pkgheader, sizeof(PKGHeader)) != sizeof(PKGHeader)) {
        failreason = "Failed to read PKG header";
        return false;
    }

    if (pkgheader.magic != kPkgMagic) {
        failreason = "Invalid PKG magic";
        return false;
    }

    if (pkgheader.pkg_size > pkgSize) {
        failreason = "PKG file size is different";
        return false;
    }

    if (pkgheader.pkg_content_offset > pkgheader.pkg_size ||
        pkgheader.pkg_content_size > pkgheader.pkg_size - pkgheader.pkg_content_offset) {
        failreason = "Content size is bigger than pkg size";
        return false;
    }

    return true;
}

bool PKG::Open(const std::filesystem::path& filepath, std::string& failreason) {
    ResetOpenState();
    ResetExtractState();

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!ValidateAndReadHeader(file, failreason)) {
        return false;
    }

    for (const auto& flag : flagNames) {
        if (isFlagSet(pkgheader.pkg_content_flags, flag.first)) {
            if (!pkgFlags.empty()) {
                pkgFlags += ", ";
            }
            pkgFlags += flag.second;
        }
    }

    // Find title id it is part of pkg_content_id starting at offset 0x40
    if (!file.Seek(0x47) || file.Read(pkgTitleID) != 1) {
        failreason = "Failed to read PKG title ID";
        return false;
    }

    const u64 offset = pkgheader.pkg_table_entry_offset;
    const u32 n_files = pkgheader.pkg_table_entry_count;

    if (offset > pkgSize || static_cast<u64>(n_files) * sizeof(PKGEntry) > pkgSize - offset) {
        failreason = "PKG entry table exceeds file size";
        return false;
    }

    for (u32 i = 0; i < n_files; i++) {
        PKGEntry entry{};
        if (!ReadPKGEntryAt(file, offset + static_cast<u64>(i) * sizeof(PKGEntry), entry)) {
            break;
        }

        const u64 entryOffset = entry.offset;
        const u64 entrySize = entry.size;
        if (entryOffset > pkgSize || entrySize > pkgSize - entryOffset) {
            continue;
        }

        const auto name = GetEntryNameByType(entry.id);
        if (name == "param.sfo" || entry.id == 0x1000) {
            sfo.clear();
            if (file.Seek(static_cast<s64>(entryOffset))) {
                sfo.resize(entrySize);
                if (entrySize > 0 && file.ReadRaw<u8>(sfo.data(), entrySize) != entrySize) {
                    sfo.clear();
                }
            }
        } else if (name == "icon0.png" || entry.id == 0x1200) {
            icon0.clear();
            if (file.Seek(static_cast<s64>(entryOffset))) {
                icon0.resize(entrySize);
                if (entrySize > 0 && file.ReadRaw<u8>(icon0.data(), entrySize) != entrySize) {
                    icon0.clear();
                }
            }
        }
    }
    file.Close();

    return true;
}

bool PKG::ReadOuterAndDecryptKeys(Common::FS::IOFile& file,
                                  std::vector<OuterEntry>& outerEntries,
                                  std::string& failreason) {
    const u64 offset = pkgheader.pkg_table_entry_offset;
    const u32 n_files = pkgheader.pkg_table_entry_count;

    if (offset > pkgSize || static_cast<u64>(n_files) * sizeof(PKGEntry) > pkgSize - offset) {
        failreason = "PKG entry table exceeds file size";
        return false;
    }

    std::array<u8, 64> concatenated_ivkey_dk3{};
    std::array<u8, 32> seed_digest{};
    std::array<std::array<u8, 32>, 7> digest1{};
    std::array<std::array<u8, 256>, 7> key1{};
    std::array<u8, 256> imgkeydata{};

    for (u32 i = 0; i < n_files; i++) {
        PKGEntry entry{};
        if (!ReadPKGEntryAt(file, offset + static_cast<u64>(i) * sizeof(PKGEntry), entry)) {
            failreason = "Failed to read PKG entry";
            return false;
        }

        const u64 entryOffset = entry.offset;
        const u64 entrySize = entry.size;
        if (entryOffset > pkgSize || entrySize > pkgSize - entryOffset) {
            failreason = "PKG entry exceeds file size";
            return false;
        }

        auto name = GetEntryNameByType(entry.id);
        std::string entryName = name.empty() ? std::to_string(entry.id) : std::string(name);

        if (entry.id == 0x10) {
            constexpr u64 kEntryKeysSize = 32 + 7 * 32 + 7 * 256;
            if (entryOffset + kEntryKeysSize > pkgSize || !file.Seek(static_cast<s64>(entryOffset))) {
                failreason = "Failed to seek to PKG entry keys";
                return false;
            }
            if (file.Read(seed_digest) != 1) {
                failreason = "Failed to read PKG seed digest";
                return false;
            }
            for (int k = 0; k < 7; k++) {
                if (file.Read(digest1[k]) != 1) {
                    failreason = "Failed to read PKG key digests";
                    return false;
                }
            }
            for (int k = 0; k < 7; k++) {
                if (file.Read(key1[k]) != 1) {
                    failreason = "Failed to read PKG entry keys";
                    return false;
                }
            }
            if (!crypto.RSA2048Decrypt(dk3_, key1[3], true)) {
                failreason = "Failed to decrypt PKG derived key 3";
                return false;
            }
        } else if (entry.id == 0x20) {
            if (entryOffset + imgkeydata.size() > pkgSize ||
                !file.Seek(static_cast<s64>(entryOffset)) || file.Read(imgkeydata) != 1) {
                failreason = "Failed to read PKG image key";
                return false;
            }

            std::memcpy(concatenated_ivkey_dk3.data(), &entry, sizeof(entry));
            std::memcpy(concatenated_ivkey_dk3.data() + sizeof(entry), dk3_.data(), sizeof(dk3_));

            crypto.ivKeyHASH256(concatenated_ivkey_dk3, ivKey);
            crypto.aesCbcCfb128Decrypt(ivKey, imgkeydata, imgKey);
            if (!crypto.RSA2048Decrypt(ekpfsKey, imgKey, false)) {
                failreason = "Failed to decrypt EKPFS key";
                return false;
            }
        }

        const bool isNp =
            (entry.id == 0x400 || entry.id == 0x401 || entry.id == 0x402 || entry.id == 0x403);
        outerEntries.push_back({entry, std::move(entryName), isNp});
    }

    return true;
}

bool PKG::InitPfsContext(Common::FS::IOFile& file, std::vector<u8>& pfsc, int& num_blocks,
                         std::string& failreason) {
    num_blocks = 0;
    pfsc.clear();

    const u64 pfsOffset = pkgheader.pfs_image_offset;
    if (pfsOffset > pkgSize || 0x370 + 16 > pkgSize - pfsOffset) {
        failreason = "PFS image offset out of bounds";
        return false;
    }

    std::array<u8, 16> seed{};
    if (!file.Seek(static_cast<s64>(pfsOffset + 0x370)) || file.Read(seed) != 1) {
        failreason = "Failed to read PFS seed";
        return false;
    }

    crypto.PfsGenCryptoKey(ekpfsKey, seed, dataKey, tweakKey);

    const u64 length = static_cast<u64>(pkgheader.pfs_cache_size) * 0x2ULL;
    if (length == 0) {
        return true;
    }

    if (length > pkgSize || pfsOffset > pkgSize - length || length < 0x20000) {
        failreason = "Invalid PFS cache size";
        return false;
    }

    std::vector<u8> pfs_encrypted(length);
    if (!file.Seek(static_cast<s64>(pfsOffset)) ||
        file.ReadRaw<u8>(pfs_encrypted.data(), length) != length) {
        failreason = "Failed to read encrypted PFS image";
        return false;
    }

    std::vector<u8> pfs_decrypted(length);
    crypto.decryptPFS(dataKey, tweakKey, pfs_encrypted, pfs_decrypted, 0);

    const auto pfscOffsetOpt = GetPFSCOffset(pfs_decrypted);
    if (!pfscOffsetOpt.has_value()) {
        failreason = "PFSC header not found in PFS image";
        return false;
    }
    pfsc_offset = *pfscOffsetOpt;
    if (pfsc_offset + sizeof(PFSCHdr) > length) {
        failreason = "PFSC header exceeds decrypted PFS cache";
        return false;
    }

    const u64 pfscLen = length - pfsc_offset;
    pfsc.resize(pfscLen);
    std::memcpy(pfsc.data(), pfs_decrypted.data() + pfsc_offset, pfscLen);

    PFSCHdr pfsChdr{};
    std::memcpy(&pfsChdr, pfsc.data(), sizeof(pfsChdr));
    if (pfsChdr.block_sz2 <= 0 || pfsChdr.data_length < 0 || pfsChdr.block_offsets < 0) {
        failreason = "Invalid PFSC header fields";
        return false;
    }

    const u64 totalBlocks = static_cast<u64>(pfsChdr.data_length) / static_cast<u64>(pfsChdr.block_sz2);
    const u64 blockOffsets = static_cast<u64>(pfsChdr.block_offsets);
    if (blockOffsets > pfsc.size() || (totalBlocks + 1) > (pfsc.size() - blockOffsets) / sizeof(u64)) {
        failreason = "PFSC sector map exceeds cache size";
        return false;
    }

    num_blocks = static_cast<int>(totalBlocks);
    sectorMap.resize(totalBlocks + 1);
    for (u64 i = 0; i < totalBlocks + 1; i++) {
        std::memcpy(&sectorMap[i], pfsc.data() + blockOffsets + i * sizeof(u64), sizeof(u64));
    }

    return true;
}

bool PKG::ParsePfsMetadata(
    const std::vector<u8>& pfsc, int num_blocks,
    const std::function<void(int rootInode)>& onUrootComplete,
    const std::function<bool(const pfs_fs_table& entry, std::string& err)>& onEntry,
    std::string& failreason) {
    u32 ndinode = 0;
    int ndinode_counter = 0;
    bool dinode_reached = false;
    bool uroot_reached = false;
    std::vector<char> decompressedData(kPfsBlockSize);

    for (int i = 0; i < num_blocks; i++) {
        const u64 sectorOffset = sectorMap[i];
        const u64 nextOffset = sectorMap[i + 1];
        if (nextOffset < sectorOffset || nextOffset > pfsc.size()) {
            failreason = "Invalid PFS metadata sector offset";
            return false;
        }
        const u64 sectorSize = nextOffset - sectorOffset;
        if (sectorSize > kPfsBlockSize) {
            failreason = "Invalid PFS metadata sector size";
            return false;
        }

        const std::span<const char> compressedView(
            reinterpret_cast<const char*>(pfsc.data() + sectorOffset), sectorSize);
        if (!DecompressSector(compressedView, decompressedData)) {
            failreason = "Failed to decompress PFS metadata sector";
            return false;
        }

        if (i == 0) {
            std::memcpy(&ndinode, decompressedData.data() + 0x30, sizeof(u32));
        }

        const u64 occupied_blocks = (static_cast<u64>(ndinode) * 0xA8ULL + 0xFFFFULL) / kPfsBlockSize;
        if (i >= 1 && static_cast<u64>(i) <= occupied_blocks) {
            for (size_t p = 0; p + sizeof(Inode) <= kPfsBlockSize; p += 0xA8) {
                Inode node{};
                std::memcpy(&node, &decompressedData[p], sizeof(node));
                if (node.Mode == 0) {
                    break;
                }
                iNodeBuf.push_back(node);
            }
        }

        const std::string_view flat_path_table(&decompressedData[0x10], 15);
        if (flat_path_table == "flat_path_table") {
            uroot_reached = true;
        }

        if (uroot_reached) {
            for (size_t k = 0; k + kDirentHeaderSize <= kPfsBlockSize;) {
                Dirent dirent{};
                const size_t copyLen = std::min<size_t>(sizeof(Dirent), kPfsBlockSize - k);
                std::memcpy(&dirent, &decompressedData[k], copyLen);

                if (dirent.ino != 0) {
                    ndinode_counter++;
                } else {
                    if (onUrootComplete) {
                        onUrootComplete(ndinode_counter);
                    }
                    uroot_reached = false;
                    break;
                }

                if (dirent.entsize < static_cast<s32>(kDirentHeaderSize) ||
                    k + static_cast<size_t>(dirent.entsize) > kPfsBlockSize) {
                    break;
                }
                k += static_cast<size_t>(dirent.entsize);
            }
        }

        const char dot = decompressedData[0x10];
        const std::string_view dotdot(&decompressedData[0x28], 2);
        if (dot == '.' && dotdot == "..") {
            dinode_reached = true;
        }

        bool end_reached = false;
        if (dinode_reached) {
            for (size_t j = 0; j + kDirentHeaderSize <= kPfsBlockSize;) {
                Dirent dirent{};
                const size_t copyLen = std::min<size_t>(sizeof(Dirent), kPfsBlockSize - j);
                std::memcpy(&dirent, &decompressedData[j], copyLen);

                if (dirent.ino == 0) {
                    break;
                }
                if (dirent.entsize < static_cast<s32>(kDirentHeaderSize) ||
                    j + static_cast<size_t>(dirent.entsize) > kPfsBlockSize) {
                    failreason = "Corrupt PFS directory entry";
                    return false;
                }

                const size_t maxName =
                    std::min<size_t>(sizeof(dirent.name), static_cast<size_t>(dirent.entsize) - kDirentHeaderSize);
                const size_t nameLen =
                    dirent.namelen > 0 ? std::min<size_t>(static_cast<size_t>(dirent.namelen), maxName) : 0;

                auto& table = fsTable.emplace_back();
                table.name = std::string(dirent.name, nameLen);
                table.inode = static_cast<u32>(dirent.ino);
                table.type = static_cast<u32>(dirent.type);

                if (onEntry && !onEntry(table, failreason)) {
                    return false;
                }

                if (table.type == PFS_FILE || table.type == PFS_DIR) {
                    ndinode_counter++;
                    if (static_cast<u32>(ndinode_counter + 1) == ndinode) {
                        end_reached = true;
                    }
                }
                j += static_cast<size_t>(dirent.entsize);
            }
            if (end_reached) {
                break;
            }
        }
    }

    return true;
}

bool PKG::Extract(const std::filesystem::path& filepath, const std::filesystem::path& extract,
                  std::string& failreason) {
    ResetExtractState();
    extract_path = extract;
    pkgpath = filepath;

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!ValidateAndReadHeader(file, failreason)) {
        return false;
    }

    std::vector<OuterEntry> outerEntries;
    if (!ReadOuterAndDecryptKeys(file, outerEntries, failreason)) {
        return false;
    }

    for (const auto& oe : outerEntries) {
        const auto relName = Common::Path::fromUtf8(oe.name);
        if (!relName.is_relative() || relName.has_root_name() || relName.has_root_directory()) {
            failreason = "Unsafe PKG entry path";
            return false;
        }
        const auto outPath = extract_path / "sce_sys" / relName;
        if (!Common::Path::isSubPath(outPath, extract_path)) {
            failreason = "Unsafe PKG entry path";
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (ec) {
            failreason = "Failed to create sce_sys directory";
            return false;
        }

        if (!file.Seek(static_cast<s64>(oe.entry.offset))) {
            failreason = "Failed to seek to PKG entry offset";
            return false;
        }

        std::vector<u8> data(oe.entry.size);
        if (oe.entry.size > 0 &&
            file.ReadRaw<u8>(data.data(), oe.entry.size) != oe.entry.size) {
            failreason = "Failed to read PKG entry data";
            return false;
        }

        Common::FS::IOFile out(outPath, Common::FS::FileAccessMode::Write);
        if (!out.IsOpen()) {
            failreason = "Failed to create output file in sce_sys";
            return false;
        }

        if (oe.isNp) {
            decNp.resize(oe.entry.size);
            std::span<u8> cipherNp(data.data(), oe.entry.size);
            std::array<u8, 64> concatenated_ivkey_dk3_{};
            std::memcpy(concatenated_ivkey_dk3_.data(), &oe.entry, sizeof(oe.entry));
            std::memcpy(concatenated_ivkey_dk3_.data() + sizeof(oe.entry), dk3_.data(), sizeof(dk3_));
            crypto.ivKeyHASH256(concatenated_ivkey_dk3_, ivKey);
            crypto.aesCbcCfb128DecryptEntry(ivKey, cipherNp, decNp);
            if (!decNp.empty() && out.Write(decNp) != decNp.size()) {
                failreason = "Failed to write decrypted NP entry";
                return false;
            }
        } else {
            if (!data.empty() && out.WriteRaw<u8>(data.data(), data.size()) != data.size()) {
                failreason = "Failed to write PKG entry";
                return false;
            }
        }
        out.Close();
    }

    int num_blocks = 0;
    std::vector<u8> pfsc;
    if (!InitPfsContext(file, pfsc, num_blocks, failreason)) {
        return false;
    }
    file.Close();

    std::filesystem::path extractRoot = extract_path;
    const auto onUrootComplete = [&](int rootInode) {
        const auto parent_path = extract_path.parent_path();
        const auto title_id = GetTitleID();
        const std::string extractNameUtf8 = Common::Path::toUtf8(extract_path);

        if (parent_path.filename() != title_id && !extractNameUtf8.ends_with("-patch")) {
            extractRoot = parent_path / title_id;
        } else {
            extractRoot = extract_path;
        }
        extractPaths[rootInode] = extractRoot;
        current_dir = extractRoot;
    };

    const auto onEntry = [&](const pfs_fs_table& table, std::string& err) -> bool {
        if (table.type == PFS_CURRENT_DIR) {
            if (auto it = extractPaths.find(static_cast<int>(table.inode)); it != extractPaths.end()) {
                current_dir = it->second;
            }
            extractPaths[static_cast<int>(table.inode)] = current_dir / ".";
            return true;
        }
        if (table.type == PFS_PARENT_DIR) {
            return true;
        }

        if (!IsSafeSingleComponent(table.name)) {
            err = "Unsafe filename inside PFS image";
            return false;
        }

        const auto targetPath = current_dir / Common::Path::fromUtf8(table.name);
        if (!Common::Path::isSubPath(targetPath, extractRoot)) {
            err = "Path traversal detected inside PFS image";
            return false;
        }

        extractPaths[static_cast<int>(table.inode)] = targetPath;
        if (table.type == PFS_DIR) {
            std::error_code ec;
            std::filesystem::create_directories(targetPath, ec);
            if (ec) {
                err = "Failed to create output directory";
                return false;
            }
        }
        return true;
    };

    return ParsePfsMetadata(pfsc, num_blocks, onUrootComplete, onEntry, failreason);
}

bool PKG::ExtractInodeBlocks(Common::FS::IOFile& pkgFile, u32 inode_number,
                             std::vector<u8>& pfsc_buf, std::vector<u8>& pfs_dec,
                             std::vector<char>& decompressedData,
                             const std::function<bool(std::span<const char>)>& writeChunk,
                             std::string& failreason) {
    if (inode_number >= iNodeBuf.size()) {
        failreason = "Invalid PFS inode index";
        return false;
    }

    const u32 sector_loc = iNodeBuf[inode_number].loc;
    const u32 nblocks = iNodeBuf[inode_number].Blocks;
    const u64 bsize = static_cast<u64>(std::max<s64>(0, iNodeBuf[inode_number].Size));

    if (static_cast<u64>(sector_loc) + static_cast<u64>(nblocks) >= sectorMap.size()) {
        failreason = "Invalid PFS sector map range";
        return false;
    }

    constexpr u64 pfsc_buf_size = 0x11000;
    if (pfsc_buf.size() < pfsc_buf_size) {
        pfsc_buf.resize(pfsc_buf_size);
    }
    if (pfs_dec.size() < pfsc_buf_size) {
        pfs_dec.resize(pfsc_buf_size);
    }
    if (decompressedData.size() < kPfsBlockSize) {
        decompressedData.resize(kPfsBlockSize);
    }

    for (u32 j = 0; j < nblocks; j++) {
        const u64 sectorOffset = sectorMap[sector_loc + j];
        const u64 nextOffset = sectorMap[sector_loc + j + 1];
        if (nextOffset < sectorOffset) {
            failreason = "Invalid PFS sector offset";
            return false;
        }
        const u64 sectorSize = nextOffset - sectorOffset;
        if (sectorSize > kPfsBlockSize) {
            failreason = "Invalid PFS sector size";
            return false;
        }

        const u64 pfsRelOffset = pfsc_offset + sectorOffset;
        const u64 fileOffset = pkgheader.pfs_image_offset + pfsRelOffset;
        const u64 currentSector1 = pfsRelOffset / kXtsSectorSize;
        const u64 sectorOffsetMask = pfsRelOffset & ~0xFFFULL;
        const u64 previousData = pfsRelOffset - sectorOffsetMask;
        if (previousData + sectorSize > pfsc_buf_size || fileOffset < previousData) {
            failreason = "PFS block offset out of bounds";
            return false;
        }

        if (!pkgFile.Seek(static_cast<s64>(fileOffset - previousData))) {
            failreason = "Failed to seek to PFS block";
            return false;
        }
        const size_t bytesRead = pkgFile.ReadSpan<u8>(std::span<u8>(pfsc_buf.data(), pfsc_buf_size));
        const size_t alignedBytes = (bytesRead / kXtsSectorSize) * kXtsSectorSize;
        if (alignedBytes < previousData + sectorSize) {
            failreason = "Unexpected EOF while reading PFS block";
            return false;
        }

        crypto.decryptPFS(dataKey, tweakKey,
                          std::span<const u8>(pfsc_buf.data(), alignedBytes),
                          std::span<u8>(pfs_dec.data(), alignedBytes), currentSector1);

        const std::span<const char> compressedView(
            reinterpret_cast<const char*>(pfs_dec.data() + previousData), sectorSize);
        if (!DecompressSector(compressedView, decompressedData)) {
            failreason = "Failed to decompress PFS sector";
            return false;
        }

        u64 write_size = kPfsBlockSize;
        if (j + 1 == nblocks) {
            const u64 prevWritten = static_cast<u64>(j) * kPfsBlockSize;
            write_size =
                (bsize > prevWritten) ? std::min<u64>(kPfsBlockSize, bsize - prevWritten) : 0ULL;
        }

        if (write_size > 0 && !writeChunk(std::span<const char>(decompressedData.data(), write_size))) {
            return false;
        }
    }

    return true;
}

bool PKG::ExtractFiles(const int index) {
    if (index < 0 || static_cast<size_t>(index) >= fsTable.size()) {
        return false;
    }

    const u32 inode_number = fsTable[index].inode;
    const u32 inode_type = fsTable[index].type;

    if (inode_type != PFS_FILE) {
        return true;
    }

    const auto pathIt = extractPaths.find(static_cast<int>(inode_number));
    if (pathIt == extractPaths.end()) {
        return false;
    }

    Common::FS::IOFile inflated;
    inflated.Open(pathIt->second, Common::FS::FileAccessMode::Write);
    if (!inflated.IsOpen()) {
        return false;
    }

    Common::FS::IOFile pkgFile;
    pkgFile.Open(pkgpath, Common::FS::FileAccessMode::Read);
    if (!pkgFile.IsOpen()) {
        return false;
    }

    std::vector<u8> pfsc_buf;
    std::vector<u8> pfs_dec;
    std::vector<char> decompressedData;
    std::string failreason;
    const bool ok = ExtractInodeBlocks(
        pkgFile, inode_number, pfsc_buf, pfs_dec, decompressedData,
        [&inflated](std::span<const char> chunk) -> bool {
            return inflated.WriteRaw<u8>(chunk.data(), chunk.size()) == chunk.size();
        },
        failreason);

    pkgFile.Close();
    inflated.Close();
    return ok;
}

bool PKG::ExtractToZar(const std::filesystem::path& filepath,
                       const std::filesystem::path& zarOutputFile,
                       std::string& failreason,
                       std::function<void(int current, int total)> progressCb,
                       std::function<bool()> cancelCheck) {
    ResetExtractState();
    pkgpath = filepath;

    ZarWriteState writeState{};
#ifdef _WIN32
    writeState.file = _wfopen(zarOutputFile.c_str(), L"wb");
#else
    writeState.file = std::fopen(zarOutputFile.c_str(), "wb");
#endif
    if (!writeState.file) {
        failreason = "Failed to create ZAR output file";
        return false;
    }

    auto cleanupOnFailure = [&](const std::string& reason) -> bool {
        if (writeState.file) {
            std::fclose(writeState.file);
            writeState.file = nullptr;
        }
        std::error_code ec;
        std::filesystem::remove(zarOutputFile, ec);
        failreason = reason;
        return false;
    };

    ZArchiveWriter writer(
        [](const int32_t /*partIndex*/, void* /*ctx*/) {},
        [](const void* data, size_t length, void* ctx) {
            auto* state = static_cast<ZarWriteState*>(ctx);
            if (!state->file || state->writeError) {
                return;
            }
            if (length > 0 && std::fwrite(data, 1, length, state->file) != length) {
                state->writeError = true;
            }
        },
        &writeState);

    Common::FS::IOFile file(filepath, Common::FS::FileAccessMode::Read);
    if (!ValidateAndReadHeader(file, failreason)) {
        return cleanupOnFailure(failreason);
    }

    std::vector<OuterEntry> outerEntries;
    if (!ReadOuterAndDecryptKeys(file, outerEntries, failreason)) {
        return cleanupOnFailure(failreason);
    }

    writer.MakeDir("sce_sys", true);

    for (const auto& oe : outerEntries) {
        if (oe.entry.id == 0x1 || oe.entry.id == 0x10 || oe.entry.id == 0x20 || oe.entry.id == 0x80) {
            continue;
        }
        if (!file.Seek(static_cast<s64>(oe.entry.offset))) {
            return cleanupOnFailure("Failed to seek to outer entry data");
        }

        std::vector<u8> data(oe.entry.size);
        if (oe.entry.size > 0 &&
            file.ReadRaw<u8>(data.data(), oe.entry.size) != oe.entry.size) {
            return cleanupOnFailure("Failed to read outer entry data");
        }

        const std::string zarRelPath = "sce_sys/" + oe.name;
        writer.StartNewFile(zarRelPath.c_str());

        if (oe.isNp) {
            decNp.resize(oe.entry.size);
            std::span<u8> cipherNp(data.data(), oe.entry.size);
            std::array<u8, 64> concatenated_ivkey_dk3_{};
            std::memcpy(concatenated_ivkey_dk3_.data(), &oe.entry, sizeof(oe.entry));
            std::memcpy(concatenated_ivkey_dk3_.data() + sizeof(oe.entry), dk3_.data(), sizeof(dk3_));
            crypto.ivKeyHASH256(concatenated_ivkey_dk3_, ivKey);
            crypto.aesCbcCfb128DecryptEntry(ivKey, cipherNp, decNp);
            writer.AppendData(decNp.data(), decNp.size());
        } else {
            writer.AppendData(data.data(), data.size());
        }

        if (writeState.writeError) {
            return cleanupOnFailure("Failed to write ZAR output file");
        }
    }

    int num_blocks = 0;
    std::vector<u8> pfsc;
    if (!InitPfsContext(file, pfsc, num_blocks, failreason)) {
        return cleanupOnFailure(failreason);
    }

    if (!ParsePfsMetadata(pfsc, num_blocks, nullptr, nullptr, failreason)) {
        return cleanupOnFailure(failreason);
    }

    std::unordered_map<int, std::string> relPaths;
    std::string current_rel_dir;

    for (const auto& table : fsTable) {
        if (table.type == PFS_CURRENT_DIR) {
            if (auto it = relPaths.find(static_cast<int>(table.inode)); it != relPaths.end()) {
                current_rel_dir = it->second;
            }
            continue;
        }
        if (table.type == PFS_PARENT_DIR) {
            continue;
        }
        if (!IsSafeSingleComponent(table.name)) {
            return cleanupOnFailure("Unsafe filename inside PFS image");
        }

        const std::string relPath =
            current_rel_dir.empty() ? table.name : (current_rel_dir + "/" + table.name);
        relPaths[static_cast<int>(table.inode)] = relPath;

        if (table.type == PFS_DIR) {
            writer.MakeDir(relPath.c_str(), true);
        }
    }

    int totalFiles = 0;
    for (const auto& table : fsTable) {
        if (table.type == PFS_FILE) {
            totalFiles++;
        }
    }

    int processedFiles = 0;
    constexpr u64 pfsc_buf_size = 0x11000;
    std::vector<u8> pfsc_buf(pfsc_buf_size);
    std::vector<u8> pfs_dec(pfsc_buf_size);
    std::vector<char> decompressedData(kPfsBlockSize);

    for (const auto& table : fsTable) {
        if (cancelCheck && cancelCheck()) {
            file.Close();
            return cleanupOnFailure("Operation canceled");
        }

        if (table.type != PFS_FILE) {
            continue;
        }

        const u32 inode_number = table.inode;
        const auto pathIt = relPaths.find(static_cast<int>(inode_number));
        if (pathIt == relPaths.end()) {
            file.Close();
            return cleanupOnFailure("Missing relative path for PFS file");
        }

        writer.StartNewFile(pathIt->second.c_str());

        std::string blockError;
        if (!ExtractInodeBlocks(
                file, inode_number, pfsc_buf, pfs_dec, decompressedData,
                [&writer, &writeState, &blockError](std::span<const char> chunk) -> bool {
                    writer.AppendData(chunk.data(), chunk.size());
                    if (writeState.writeError) {
                        blockError = "Failed to write ZAR output file";
                        return false;
                    }
                    return true;
                },
                blockError)) {
            file.Close();
            return cleanupOnFailure(blockError);
        }

        processedFiles++;
        if (progressCb) {
            progressCb(processedFiles, totalFiles);
        }
    }

    writer.Finalize();
    file.Close();
    const bool flushOk = (std::fflush(writeState.file) == 0);
    std::fclose(writeState.file);
    writeState.file = nullptr;

    if (writeState.writeError || !flushOk) {
        std::error_code ec;
        std::filesystem::remove(zarOutputFile, ec);
        failreason = "Failed to finalize ZAR output file";
        return false;
    }

    return true;
}
