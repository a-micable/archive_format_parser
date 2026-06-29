#ifndef VECTOR_ARCHIVE_HEADER_H_
#define VECTOR_ARCHIVE_HEADER_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vector {

constexpr std::uint8_t kFormatVersion = '1';
constexpr std::uint8_t kFlagCompressed = 1u << 0;
constexpr std::uint8_t kFlagExtension = 1u << 1;
constexpr std::uint8_t kFlagNested = 1u << 2;

struct Header {
    std::uint8_t version = 0;
    std::uint8_t flags = 0;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size);

    bool readByte(std::uint8_t* value);
    bool readBytes(std::size_t count, const std::uint8_t** out);
    bool readVarint(std::uint64_t* value);
    bool skip(std::size_t count);

    const std::uint8_t* current() const;
    std::size_t position() const;
    std::size_t remaining() const;
    std::size_t size() const;

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;
};

bool parseHeader(Reader* reader, Header* header, std::string* error);
void writeHeader(std::vector<std::uint8_t>* out, std::uint8_t flags);
void writeVarint(std::vector<std::uint8_t>* out, std::uint64_t value);
bool decodeFlagByte(std::uint8_t byte, std::uint8_t* value);
std::uint8_t encodeFlagByte(std::uint8_t value);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_HEADER_H_
