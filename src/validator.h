#ifndef VECTOR_ARCHIVE_VALIDATOR_H_
#define VECTOR_ARCHIVE_VALIDATOR_H_

#include <cstdint>
#include <filesystem>
#include <string>

#include "inspect.h"
#include "path_utils.h"

namespace vector {

struct ValidationOptions {
    InspectOptions inspect;
    PathPolicy path_policy = defaultArchivePathPolicy();
    bool require_canonical_paths = false;
    bool warn_on_duplicates = true;
    bool warn_on_sparse_layout = true;
    bool warn_on_ratio_anomalies = true;
    bool warn_on_empty_entries = false;
    bool warn_on_unknown_flags = true;
    bool require_extension_checksum = false;
    std::uint64_t max_entry_size = 128ull * 1024ull * 1024ull;
    std::uint64_t max_archive_size = 1024ull * 1024ull * 1024ull;
    double high_compression_ratio = 64.0;
    double expansion_ratio = 16.0;
};

struct ValidationResult {
    ArchiveManifest manifest;
    Diagnostics diagnostics;
    bool ok = false;
};

ValidationResult validateArchiveBytes(const std::uint8_t* data,
                                      std::size_t size,
                                      const ValidationOptions& options = {});
ValidationResult validateArchiveBytes(const std::vector<std::uint8_t>& bytes,
                                      const ValidationOptions& options = {});
ValidationResult validateArchiveFile(const std::filesystem::path& path,
                                     const ValidationOptions& options = {});

void validateManifest(const ArchiveManifest& manifest,
                      const ValidationOptions& options,
                      Diagnostics* diagnostics);
void validateEntryTree(const ManifestEntry& entry,
                       const ValidationOptions& options,
                       Diagnostics* diagnostics);
void validatePathPolicy(const ManifestEntry& entry,
                        const ValidationOptions& options,
                        Diagnostics* diagnostics);
void validateChunkLayout(const ManifestEntry& entry,
                         const ValidationOptions& options,
                         Diagnostics* diagnostics);
void validateCompressionProfile(const ManifestEntry& entry,
                                const ValidationOptions& options,
                                Diagnostics* diagnostics);
void validateFlagProfile(const ManifestEntry& entry,
                         const ValidationOptions& options,
                         Diagnostics* diagnostics);
void validateArchiveTotals(const ArchiveManifest& manifest,
                           const ValidationOptions& options,
                           Diagnostics* diagnostics);

std::string formatValidationText(const ValidationResult& result);
std::string formatValidationJson(const ValidationResult& result);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_VALIDATOR_H_
