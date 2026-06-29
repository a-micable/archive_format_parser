#include "index.h"

#include <limits>

namespace vector {
namespace {

constexpr std::uint64_t kMaxEntries = 4096;
constexpr std::uint64_t kMaxNameLength = 4096;

}  // namespace

bool parseIndex(Reader* reader, IndexTable* index, std::string* error) {
    std::uint64_t count = 0;
    if (!reader->readVarint(&count)) {
        if (error) *error = "truncated index entry count";
        return false;
    }
    if (count > kMaxEntries) {
        if (error) *error = "index has too many entries";
        return false;
    }

    index->entries.clear();
    index->name_storage.clear();
    index->entries.reserve(static_cast<std::size_t>(count));

    struct PendingEntry {
        std::uint64_t offset;
        std::uint64_t packed_size;
        std::uint64_t original_size;
        std::uint8_t flags;
        std::size_t name_offset;
        std::size_t name_length;
    };
    std::vector<PendingEntry> pending;
    pending.reserve(static_cast<std::size_t>(count));

    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t offset = 0;
        std::uint64_t packed_size = 0;
        std::uint64_t original_size = 0;
        std::uint64_t name_length = 0;
        std::uint8_t flags = 0;
        const std::uint8_t* name = nullptr;

        if (!reader->readVarint(&offset) ||
            !reader->readVarint(&packed_size) ||
            !reader->readVarint(&original_size) ||
            !reader->readByte(&flags) ||
            !reader->readVarint(&name_length)) {
            if (error) *error = "truncated index entry";
            return false;
        }
        if (!decodeFlagByte(flags, &flags)) {
            if (error) *error = "invalid index flags";
            return false;
        }
        if (name_length == 0 || name_length > kMaxNameLength) {
            if (error) *error = "invalid entry name length";
            return false;
        }
        if (!reader->readBytes(static_cast<std::size_t>(name_length), &name)) {
            if (error) *error = "truncated entry name";
            return false;
        }

        std::size_t start = index->name_storage.size();
        index->name_storage.insert(index->name_storage.end(),
                                   reinterpret_cast<const char*>(name),
                                   reinterpret_cast<const char*>(name + name_length));
        pending.push_back({offset, packed_size, original_size, flags, start,
                           static_cast<std::size_t>(name_length)});
    }

    for (const PendingEntry& item : pending) {
        IndexEntry entry;
        entry.offset = item.offset;
        entry.packed_size = item.packed_size;
        entry.original_size = item.original_size;
        entry.flags = item.flags;
        entry.name = std::string_view(index->name_storage.data() + item.name_offset,
                                      item.name_length);
        index->entries.push_back(entry);
    }

    return true;
}

void writeIndex(std::vector<std::uint8_t>* out,
                const std::vector<IndexEntry>& entries) {
    writeVarint(out, entries.size());
    for (const IndexEntry& entry : entries) {
        writeVarint(out, entry.offset);
        writeVarint(out, entry.packed_size);
        writeVarint(out, entry.original_size);
        out->push_back(encodeFlagByte(entry.flags));
        writeVarint(out, entry.name.size());
        out->insert(out->end(), entry.name.begin(), entry.name.end());
    }
}

}  // namespace vector
