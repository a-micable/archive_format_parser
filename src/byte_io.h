#ifndef VECTOR_ARCHIVE_BYTE_IO_H_
#define VECTOR_ARCHIVE_BYTE_IO_H_

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace vector {

struct ByteRange {
    std::size_t offset = 0;
    std::size_t length = 0;

    bool empty() const;
    std::size_t end() const;
    bool contains(std::size_t position) const;
    bool contains(ByteRange other) const;
    bool overlaps(ByteRange other) const;
    ByteRange intersection(ByteRange other) const;
};

class ByteView {
public:
    ByteView();
    ByteView(const std::uint8_t* data, std::size_t size);
    explicit ByteView(const std::vector<std::uint8_t>& bytes);

    const std::uint8_t* data() const;
    std::size_t size() const;
    bool empty() const;
    ByteRange fullRange() const;
    bool has(std::size_t offset, std::size_t length) const;
    ByteView subview(std::size_t offset, std::size_t length) const;
    std::uint8_t byteAt(std::size_t offset, std::uint8_t fallback = 0) const;
    std::string asciiPreview(std::size_t offset,
                             std::size_t length,
                             std::size_t limit) const;

private:
    const std::uint8_t* data_;
    std::size_t size_;
};

class CheckedReader {
public:
    explicit CheckedReader(ByteView view);

    std::size_t position() const;
    std::size_t remaining() const;
    std::size_t size() const;
    ByteRange consumedRange() const;
    ByteRange remainingRange() const;
    bool good() const;
    const std::string& error() const;

    bool readByte(std::uint8_t* value);
    bool readBytes(std::size_t count, ByteView* out);
    bool readVarint(std::uint64_t* value);
    bool skip(std::size_t count);
    bool seek(std::size_t position);
    bool require(std::size_t count, std::string_view what);
    void setError(std::string message);

private:
    ByteView view_;
    std::size_t pos_;
    std::string error_;
};

struct VarintInfo {
    bool valid = false;
    bool canonical = false;
    bool truncated = false;
    bool overflow = false;
    std::uint64_t value = 0;
    std::size_t encoded_size = 0;
};

VarintInfo inspectVarint(ByteView view, std::size_t offset);
std::size_t varintEncodedSize(std::uint64_t value);
std::vector<std::uint8_t> encodeVarintToBytes(std::uint64_t value);
bool checkedAdd(std::size_t a, std::size_t b, std::size_t* out);
bool checkedMul(std::size_t a, std::size_t b, std::size_t* out);
bool rangeWithin(std::size_t offset,
                 std::size_t length,
                 std::size_t container_size);
ByteRange makeRange(std::size_t offset, std::size_t length);

std::uint16_t readLittle16(const std::uint8_t* data);
std::uint32_t readLittle32(const std::uint8_t* data);
std::uint64_t readLittle64(const std::uint8_t* data);
std::uint16_t readBig16(const std::uint8_t* data);
std::uint32_t readBig32(const std::uint8_t* data);
std::uint64_t readBig64(const std::uint8_t* data);
void appendLittle16(std::vector<std::uint8_t>* out, std::uint16_t value);
void appendLittle32(std::vector<std::uint8_t>* out, std::uint32_t value);
void appendLittle64(std::vector<std::uint8_t>* out, std::uint64_t value);
void appendBig16(std::vector<std::uint8_t>* out, std::uint16_t value);
void appendBig32(std::vector<std::uint8_t>* out, std::uint32_t value);
void appendBig64(std::vector<std::uint8_t>* out, std::uint64_t value);

std::string hexByte(std::uint8_t value);
std::string hexWord(std::uint64_t value, int min_digits = 0);
std::string hexRange(ByteView view,
                     std::size_t offset,
                     std::size_t length,
                     std::size_t group = 1);
std::string asciiBytes(ByteView view,
                       std::size_t offset,
                       std::size_t length,
                       char replacement = '.');
std::string humanSize(std::uint64_t bytes);
std::string decimalWithCommas(std::uint64_t value);
std::string percentString(double value, int precision = 1);
std::string ratioString(std::uint64_t numerator,
                        std::uint64_t denominator,
                        int precision = 2);
std::string pluralize(std::uint64_t count,
                      std::string_view singular,
                      std::string_view plural = {});
std::string repeatString(std::string_view value, std::size_t count);
std::string trimCopy(std::string_view value);
std::string lowerAscii(std::string_view value);
std::string upperAscii(std::string_view value);
bool startsWith(std::string_view value, std::string_view prefix);
bool endsWith(std::string_view value, std::string_view suffix);
bool containsControlByte(std::string_view value);
bool isPrintableAscii(std::uint8_t value);
bool isAsciiWhitespace(char value);
bool isAsciiAlpha(char value);
bool isAsciiDigit(char value);
bool isAsciiHexDigit(char value);
int hexDigitValue(char value);

class LineBuilder {
public:
    explicit LineBuilder(std::size_t indent = 0);

    void setIndent(std::size_t indent);
    void increaseIndent(std::size_t amount = 2);
    void decreaseIndent(std::size_t amount = 2);
    void appendLine(std::string_view line);
    void appendWrapped(std::string_view text, std::size_t width = 78);
    void appendKeyValue(std::string_view key, std::string_view value);
    void blankLine();
    std::string str() const;

private:
    std::size_t indent_;
    std::string output_;
};

}  // namespace vector

#endif  // VECTOR_ARCHIVE_BYTE_IO_H_
