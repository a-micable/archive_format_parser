#ifndef VECTOR_ARCHIVE_MANIFEST_H_
#define VECTOR_ARCHIVE_MANIFEST_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostics.h"

namespace vector {

enum class ExtensionState {
    kAbsent,
    kPresentUnchecked,
    kChecksumValid,
    kChecksumInvalid,
    kTruncated,
};

struct ChunkLayout {
    std::uint64_t relative_offset = 0;
    std::uint64_t absolute_offset = 0;
    std::uint64_t header_size = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t encoded_end = 0;
    std::uint8_t chunk_flags = 0;
    bool readable = false;
};

struct ManifestEntry {
    std::string name;
    std::string normalized_name;
    std::uint64_t packed_size = 0;
    std::uint64_t original_size = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t flags = 0;
    ChunkLayout chunk;
    bool compressed = false;
    bool nested = false;
    bool name_changed = false;
    int depth = 0;
    std::vector<ManifestEntry> children;
};

struct ArchiveStats {
    std::uint64_t archive_size = 0;
    std::uint64_t entry_count = 0;
    std::uint64_t nested_archive_count = 0;
    std::uint64_t compressed_entry_count = 0;
    std::uint64_t total_packed_size = 0;
    std::uint64_t total_original_size = 0;
    std::uint64_t largest_entry_size = 0;
    std::string largest_entry_name;
    int max_depth = 0;
};

struct ArchiveManifest {
    std::string source_name;
    std::uint8_t version = 0;
    std::uint8_t flags = 0;
    std::uint64_t archive_size = 0;
    std::uint64_t index_offset = 0;
    std::uint64_t chunk_stream_offset = 0;
    std::uint64_t chunk_stream_size = 0;
    ExtensionState extension_state = ExtensionState::kAbsent;
    std::uint64_t extension_offset = 0;
    std::uint64_t extension_size = 0;
    std::vector<ManifestEntry> entries;
    Diagnostics diagnostics;
};

struct ManifestFormatOptions {
    bool show_offsets = true;
    bool show_flags = true;
    bool show_sizes = true;
    bool show_normalized_names = true;
    bool show_nested_entries = true;
    bool show_diagnostics = true;
    bool json_pretty = true;
};

std::string extensionStateName(ExtensionState state);
std::string entryFlagsText(const ManifestEntry& entry);
std::string archiveFlagsText(std::uint8_t flags);
std::string formatManifestText(const ArchiveManifest& manifest,
                               const ManifestFormatOptions& options = {});
std::string formatManifestJson(const ArchiveManifest& manifest,
                               const ManifestFormatOptions& options = {});
ArchiveStats computeArchiveStats(const ArchiveManifest& manifest);
std::vector<const ManifestEntry*> flattenEntries(const ArchiveManifest& manifest);
std::vector<const ManifestEntry*> flattenEntries(const ManifestEntry& entry);
const ManifestEntry* findEntryByName(const ArchiveManifest& manifest,
                                     const std::string& name);
std::string describeEntry(const ManifestEntry& entry,
                          const ManifestFormatOptions& options = {});
std::string describeArchiveStats(const ArchiveStats& stats);
std::string manifestSummaryLine(const ArchiveManifest& manifest);
bool manifestHasEntry(const ArchiveManifest& manifest, const std::string& name);
bool manifestHasNestedArchives(const ArchiveManifest& manifest);
bool manifestHasCompressedEntries(const ArchiveManifest& manifest);
std::uint64_t countEntriesRecursive(const ArchiveManifest& manifest);
std::uint64_t countEntriesRecursive(const ManifestEntry& entry);
std::uint64_t sumPackedRecursive(const ArchiveManifest& manifest);
std::uint64_t sumOriginalRecursive(const ArchiveManifest& manifest);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_MANIFEST_H_
