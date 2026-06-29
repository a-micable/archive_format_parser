#include "header.h"

#include <array>
#include <limits>

namespace vector {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic = {'V', 'E', 'C', '!'};

}  // namespace

Reader::Reader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size), pos_(0) {}

bool Reader::readByte(std::uint8_t* value) {
    if (remaining() < 1) {
        return false;
    }
    *value = data_[pos_++];
    return true;
}

bool Reader::readBytes(std::size_t count, const std::uint8_t** out) {
    if (remaining() < count) {
        return false;
    }
    *out = data_ + pos_;
    pos_ += count;
    return true;
}

bool Reader::readVarint(std::uint64_t* value) {
    std::uint64_t result = 0;
    unsigned shift = 0;
    for (int i = 0; i < 10; ++i) {
        std::uint8_t byte = 0;
        if (!readByte(&byte)) {
            return false;
        }
        result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

bool Reader::skip(std::size_t count) {
    if (remaining() < count) {
        return false;
    }
    pos_ += count;
    return true;
}

const std::uint8_t* Reader::current() const {
    return data_ + pos_;
}

std::size_t Reader::position() const {
    return pos_;
}

std::size_t Reader::remaining() const {
    return size_ - pos_;
}

std::size_t Reader::size() const {
    return size_;
}

bool parseHeader(Reader* reader, Header* header, std::string* error) {
    const std::uint8_t* magic = nullptr;
    if (!reader->readBytes(kMagic.size(), &magic)) {
        if (error) *error = "archive is too small for header";
        return false;
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (magic[i] != kMagic[i]) {
            if (error) *error = "bad .vec magic";
            return false;
        }
    }
    std::uint8_t encoded_flags = 0;
    if (!reader->readByte(&header->version) || !reader->readByte(&encoded_flags)) {
        if (error) *error = "truncated .vec header";
        return false;
    }
    if (header->version != kFormatVersion) {
        if (error) *error = "unsupported .vec version";
        return false;
    }
    if (!decodeFlagByte(encoded_flags, &header->flags)) {
        if (error) *error = "invalid header flags";
        return false;
    }
    return true;
}

void writeHeader(std::vector<std::uint8_t>* out, std::uint8_t flags) {
    out->insert(out->end(), kMagic.begin(), kMagic.end());
    out->push_back(kFormatVersion);
    out->push_back(encodeFlagByte(flags));
}

void writeVarint(std::vector<std::uint8_t>* out, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out->push_back(byte);
    } while (value != 0);
}

bool decodeFlagByte(std::uint8_t byte, std::uint8_t* value) {
    *value = static_cast<std::uint8_t>(byte ^ 0x5au);
    return true;
}

std::uint8_t encodeFlagByte(std::uint8_t value) {
    return static_cast<std::uint8_t>(value ^ 0x5au);
}

}  // namespace vector
