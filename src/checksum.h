#ifndef VECTOR_ARCHIVE_CHECKSUM_H_
#define VECTOR_ARCHIVE_CHECKSUM_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "header.h"

namespace vector {

struct ExtensionBlock {
    std::vector<std::uint8_t> bytes;
    bool parsed = false;
};

std::uint64_t checksumBytes(const std::uint8_t* data, std::size_t size);
bool parseExtension(Reader* reader,
                    bool enabled,
                    ExtensionBlock* extension,
                    std::string* error);
void writeExtension(std::vector<std::uint8_t>* out,
                    const std::vector<std::uint8_t>& bytes,
                    bool valid_checksum);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_CHECKSUM_H_
