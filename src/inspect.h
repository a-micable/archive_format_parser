#ifndef VECTOR_ARCHIVE_INSPECT_H_
#define VECTOR_ARCHIVE_INSPECT_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "index.h"
#include "manifest.h"

namespace vector {

struct InspectOptions {
    bool recurse_nested = true;
    bool decode_chunks = true;
    bool continue_after_errors = true;
    int max_depth = 8;
    std::string source_name;
};

struct InspectResult {
    ArchiveManifest manifest;
    bool parsed = false;
};

InspectResult inspectArchiveBytes(const std::uint8_t* data,
                                  std::size_t size,
                                  const InspectOptions& options = {});
InspectResult inspectArchiveBytes(const std::vector<std::uint8_t>& bytes,
                                  const InspectOptions& options = {});
InspectResult inspectArchiveFile(const std::filesystem::path& path,
                                 const InspectOptions& options = {});
bool inspectArchiveFile(const std::filesystem::path& path,
                        ArchiveManifest* manifest,
                        std::string* error,
                        const InspectOptions& options = {});

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path,
                                        std::string* error);
bool writeTextFile(const std::filesystem::path& path,
                   const std::string& text,
                   std::string* error);

bool readChunkLayout(const std::uint8_t* chunk_stream,
                     std::size_t chunk_stream_size,
                     std::uint64_t chunk_stream_absolute_offset,
                     const IndexEntry& entry,
                     ChunkLayout* layout,
                     std::string* error);
ExtensionState inspectExtensionState(const std::uint8_t* data,
                                     std::size_t size,
                                     bool expected,
                                     std::uint64_t absolute_offset,
                                     std::uint64_t* consumed,
                                     Diagnostics* diagnostics);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_INSPECT_H_
