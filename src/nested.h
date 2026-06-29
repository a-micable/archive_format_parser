#ifndef VECTOR_ARCHIVE_NESTED_H_
#define VECTOR_ARCHIVE_NESTED_H_

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "index.h"
#include "pool.h"

namespace vector {

struct UnpackOptions {
    bool extract = false;
    std::filesystem::path output_dir;
};

bool parseArchiveBytes(const std::uint8_t* data,
                       std::size_t size,
                       BufferPool* pool,
                       const UnpackOptions& options,
                       int depth,
                       std::string* error);

bool parseNestedArchive(const std::uint8_t* data,
                        std::size_t size,
                        BufferPool* pool,
                        const UnpackOptions& options,
                        int depth,
                        std::string* error);

bool looksLikeVec(const std::uint8_t* data, std::size_t size);
std::filesystem::path safeOutputPath(const std::filesystem::path& output_dir,
                                     std::string_view name);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_NESTED_H_
