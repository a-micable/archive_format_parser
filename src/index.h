#ifndef VECTOR_ARCHIVE_INDEX_H_
#define VECTOR_ARCHIVE_INDEX_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "header.h"

namespace vector {

constexpr std::uint8_t kEntryCompressed = 1u << 0;
constexpr std::uint8_t kEntryNested = 1u << 1;

struct IndexEntry {
    std::uint64_t offset = 0;
    std::uint64_t packed_size = 0;
    std::uint64_t original_size = 0;
    std::uint8_t flags = 0;
    std::string_view name;
};

struct IndexTable {
    std::vector<IndexEntry> entries;
    std::vector<char> name_storage;
};

bool parseIndex(Reader* reader, IndexTable* index, std::string* error);
void writeIndex(std::vector<std::uint8_t>* out,
                const std::vector<IndexEntry>& entries);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_INDEX_H_
