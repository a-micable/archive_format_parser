#ifndef VECTOR_ARCHIVE_ARCHIVE_H_
#define VECTOR_ARCHIVE_ARCHIVE_H_

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace vector {

struct FilePayload {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

bool pack(const std::vector<std::filesystem::path>& inputs,
          const std::filesystem::path& output,
          bool compress,
          std::string* error);

bool unpack(const std::filesystem::path& archive,
            const std::filesystem::path& output_dir,
            std::string* error);

bool unpack(const std::uint8_t* data, std::size_t size);

bool unpackNestedOnly(const std::uint8_t* data, std::size_t size);

std::vector<std::uint8_t> buildArchiveForTest(const std::vector<FilePayload>& files,
                                              bool compress,
                                              bool with_extension,
                                              bool valid_extension);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_ARCHIVE_H_
