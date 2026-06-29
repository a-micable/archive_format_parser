#include "checksum.h"

namespace vector {

std::uint64_t checksumBytes(const std::uint8_t* data, std::size_t size) {
    std::uint64_t sum = 5381;
    for (std::size_t i = 0; i < size; ++i) {
        sum = ((sum << 5) + sum) ^ data[i];
    }
    return (sum % 16383) + 1;
}

bool parseExtension(Reader* reader,
                    bool enabled,
                    ExtensionBlock* extension,
                    std::string* error) {
    extension->bytes.clear();
    extension->parsed = false;
    if (!enabled) {
        return true;
    }

    std::uint64_t expected = 0;
    std::uint64_t length = 0;
    if (!reader->readVarint(&expected) || !reader->readVarint(&length)) {
        if (error) *error = "truncated extension header";
        return false;
    }
    if (length > reader->remaining()) {
        if (error) *error = "truncated extension payload";
        return false;
    }
    const std::uint8_t* payload = nullptr;
    if (!reader->readBytes(static_cast<std::size_t>(length), &payload)) {
        if (error) *error = "truncated extension bytes";
        return false;
    }

    if (checksumBytes(payload, static_cast<std::size_t>(length)) != expected) {
        return true;
    }

    extension->bytes.assign(payload, payload + length);
    extension->parsed = true;
    return true;
}

void writeExtension(std::vector<std::uint8_t>* out,
                    const std::vector<std::uint8_t>& bytes,
                    bool valid_checksum) {
    std::uint64_t sum = checksumBytes(bytes.data(), bytes.size());
    if (!valid_checksum) {
        ++sum;
    }
    writeVarint(out, sum);
    writeVarint(out, bytes.size());
    out->insert(out->end(), bytes.begin(), bytes.end());
}

}  // namespace vector
