#include "byte_io.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>

namespace vector {
namespace {

char hexNibble(unsigned value) {
    return static_cast<char>(value < 10 ? '0' + value : 'a' + value - 10);
}

std::string fixedDouble(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

}  // namespace

bool ByteRange::empty() const {
    return length == 0;
}

std::size_t ByteRange::end() const {
    std::size_t result = 0;
    if (!checkedAdd(offset, length, &result)) {
        return std::numeric_limits<std::size_t>::max();
    }
    return result;
}

bool ByteRange::contains(std::size_t position) const {
    return position >= offset && position < end();
}

bool ByteRange::contains(ByteRange other) const {
    return other.offset >= offset && other.end() <= end();
}

bool ByteRange::overlaps(ByteRange other) const {
    if (empty() || other.empty()) {
        return false;
    }
    return offset < other.end() && other.offset < end();
}

ByteRange ByteRange::intersection(ByteRange other) const {
    const std::size_t begin = std::max(offset, other.offset);
    const std::size_t finish = std::min(end(), other.end());
    if (finish <= begin) {
        return {begin, 0};
    }
    return {begin, finish - begin};
}

ByteView::ByteView() : data_(nullptr), size_(0) {}

ByteView::ByteView(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {}

ByteView::ByteView(const std::vector<std::uint8_t>& bytes)
    : data_(bytes.data()), size_(bytes.size()) {}

const std::uint8_t* ByteView::data() const {
    return data_;
}

std::size_t ByteView::size() const {
    return size_;
}

bool ByteView::empty() const {
    return size_ == 0;
}

ByteRange ByteView::fullRange() const {
    return {0, size_};
}

bool ByteView::has(std::size_t offset, std::size_t length) const {
    return rangeWithin(offset, length, size_);
}

ByteView ByteView::subview(std::size_t offset, std::size_t length) const {
    if (!has(offset, length)) {
        return ByteView();
    }
    return ByteView(data_ + offset, length);
}

std::uint8_t ByteView::byteAt(std::size_t offset, std::uint8_t fallback) const {
    if (offset >= size_) {
        return fallback;
    }
    return data_[offset];
}

std::string ByteView::asciiPreview(std::size_t offset,
                                   std::size_t length,
                                   std::size_t limit) const {
    const std::size_t safe_length = has(offset, length) ? length : 0;
    const std::size_t count = std::min(safe_length, limit);
    std::string out = asciiBytes(*this, offset, count);
    if (safe_length > count) {
        out += "...";
    }
    return out;
}

CheckedReader::CheckedReader(ByteView view) : view_(view), pos_(0) {}

std::size_t CheckedReader::position() const {
    return pos_;
}

std::size_t CheckedReader::remaining() const {
    if (pos_ > view_.size()) {
        return 0;
    }
    return view_.size() - pos_;
}

std::size_t CheckedReader::size() const {
    return view_.size();
}

ByteRange CheckedReader::consumedRange() const {
    return {0, pos_};
}

ByteRange CheckedReader::remainingRange() const {
    return {pos_, remaining()};
}

bool CheckedReader::good() const {
    return error_.empty();
}

const std::string& CheckedReader::error() const {
    return error_;
}

bool CheckedReader::readByte(std::uint8_t* value) {
    if (!require(1, "byte")) {
        return false;
    }
    *value = view_.data()[pos_++];
    return true;
}

bool CheckedReader::readBytes(std::size_t count, ByteView* out) {
    if (!require(count, "byte span")) {
        return false;
    }
    *out = view_.subview(pos_, count);
    pos_ += count;
    return true;
}

bool CheckedReader::readVarint(std::uint64_t* value) {
    VarintInfo info = inspectVarint(view_, pos_);
    if (!info.valid) {
        if (info.truncated) {
            setError("truncated varint at offset " + std::to_string(pos_));
        } else if (info.overflow) {
            setError("overflowing varint at offset " + std::to_string(pos_));
        } else {
            setError("invalid varint at offset " + std::to_string(pos_));
        }
        return false;
    }
    *value = info.value;
    pos_ += info.encoded_size;
    return true;
}

bool CheckedReader::skip(std::size_t count) {
    if (!require(count, "skip")) {
        return false;
    }
    pos_ += count;
    return true;
}

bool CheckedReader::seek(std::size_t position) {
    if (position > view_.size()) {
        setError("seek outside input");
        return false;
    }
    pos_ = position;
    return true;
}

bool CheckedReader::require(std::size_t count, std::string_view what) {
    if (!error_.empty()) {
        return false;
    }
    if (remaining() < count) {
        std::ostringstream out;
        out << "truncated " << what << " at offset " << pos_
            << " need " << count << " have " << remaining();
        setError(out.str());
        return false;
    }
    return true;
}

void CheckedReader::setError(std::string message) {
    if (error_.empty()) {
        error_ = std::move(message);
    }
}

VarintInfo inspectVarint(ByteView view, std::size_t offset) {
    VarintInfo info;
    if (offset > view.size()) {
        info.truncated = true;
        return info;
    }
    std::uint64_t result = 0;
    unsigned shift = 0;
    for (std::size_t i = 0; i < 10; ++i) {
        if (offset + i >= view.size()) {
            info.truncated = true;
            info.encoded_size = i;
            return info;
        }
        const std::uint8_t byte = view.byteAt(offset + i);
        if (shift == 63 && (byte & 0x7e) != 0) {
            info.overflow = true;
            info.encoded_size = i + 1;
            return info;
        }
        result |= static_cast<std::uint64_t>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            info.valid = true;
            info.value = result;
            info.encoded_size = i + 1;
            info.canonical = varintEncodedSize(result) == info.encoded_size;
            return info;
        }
        shift += 7;
    }
    info.overflow = true;
    info.encoded_size = 10;
    return info;
}

std::size_t varintEncodedSize(std::uint64_t value) {
    std::size_t size = 1;
    while ((value >>= 7) != 0) {
        ++size;
    }
    return size;
}

std::vector<std::uint8_t> encodeVarintToBytes(std::uint64_t value) {
    std::vector<std::uint8_t> out;
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0);
    return out;
}

bool checkedAdd(std::size_t a, std::size_t b, std::size_t* out) {
    if (a > std::numeric_limits<std::size_t>::max() - b) {
        return false;
    }
    *out = a + b;
    return true;
}

bool checkedMul(std::size_t a, std::size_t b, std::size_t* out) {
    if (a != 0 && b > std::numeric_limits<std::size_t>::max() / a) {
        return false;
    }
    *out = a * b;
    return true;
}

bool rangeWithin(std::size_t offset,
                 std::size_t length,
                 std::size_t container_size) {
    std::size_t end = 0;
    return checkedAdd(offset, length, &end) && end <= container_size;
}

ByteRange makeRange(std::size_t offset, std::size_t length) {
    return {offset, length};
}

std::uint16_t readLittle16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[0]) |
           (static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t readLittle32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) |
           (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint64_t readLittle64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value <<= 8;
        value |= data[i];
    }
    return value;
}

std::uint16_t readBig16(const std::uint8_t* data) {
    return static_cast<std::uint16_t>(data[1]) |
           (static_cast<std::uint16_t>(data[0]) << 8);
}

std::uint32_t readBig32(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[3]) |
           (static_cast<std::uint32_t>(data[2]) << 8) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[0]) << 24);
}

std::uint64_t readBig64(const std::uint8_t* data) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value <<= 8;
        value |= data[i];
    }
    return value;
}

void appendLittle16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>(value & 0xff));
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void appendLittle32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out->push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

void appendLittle64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out->push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

void appendBig16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    out->push_back(static_cast<std::uint8_t>(value & 0xff));
}

void appendBig32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    for (int i = 3; i >= 0; --i) {
        out->push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

void appendBig64(std::vector<std::uint8_t>* out, std::uint64_t value) {
    for (int i = 7; i >= 0; --i) {
        out->push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xff));
    }
}

std::string hexByte(std::uint8_t value) {
    std::string out;
    out.push_back(hexNibble((value >> 4) & 0x0f));
    out.push_back(hexNibble(value & 0x0f));
    return out;
}

std::string hexWord(std::uint64_t value, int min_digits) {
    std::string out;
    do {
        out.push_back(hexNibble(static_cast<unsigned>(value & 0x0f)));
        value >>= 4;
    } while (value != 0);
    while (static_cast<int>(out.size()) < min_digits) {
        out.push_back('0');
    }
    std::reverse(out.begin(), out.end());
    return "0x" + out;
}

std::string hexRange(ByteView view,
                     std::size_t offset,
                     std::size_t length,
                     std::size_t group) {
    if (!view.has(offset, length)) {
        return "";
    }
    std::string out;
    for (std::size_t i = 0; i < length; ++i) {
        if (i != 0) {
            out.push_back(group != 0 && i % group == 0 ? ' ' : ':');
        }
        out += hexByte(view.byteAt(offset + i));
    }
    return out;
}

std::string asciiBytes(ByteView view,
                       std::size_t offset,
                       std::size_t length,
                       char replacement) {
    if (!view.has(offset, length)) {
        return "";
    }
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const std::uint8_t byte = view.byteAt(offset + i);
        out.push_back(isPrintableAscii(byte) ? static_cast<char>(byte) : replacement);
    }
    return out;
}

std::string humanSize(std::uint64_t bytes) {
    static const char* kUnits[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0])) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return std::to_string(bytes) + " B";
    }
    return fixedDouble(value, value >= 10.0 ? 1 : 2) + " " + kUnits[unit];
}

std::string decimalWithCommas(std::uint64_t value) {
    std::string raw = std::to_string(value);
    std::string out;
    int group = 0;
    for (auto it = raw.rbegin(); it != raw.rend(); ++it) {
        if (group == 3) {
            out.push_back(',');
            group = 0;
        }
        out.push_back(*it);
        ++group;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

std::string percentString(double value, int precision) {
    return fixedDouble(value * 100.0, precision) + "%";
}

std::string ratioString(std::uint64_t numerator,
                        std::uint64_t denominator,
                        int precision) {
    if (denominator == 0) {
        return "n/a";
    }
    const double ratio = static_cast<double>(numerator) /
                         static_cast<double>(denominator);
    return fixedDouble(ratio, precision) + "x";
}

std::string pluralize(std::uint64_t count,
                      std::string_view singular,
                      std::string_view plural) {
    if (count == 1) {
        return std::to_string(count) + " " + std::string(singular);
    }
    if (plural.empty()) {
        return std::to_string(count) + " " + std::string(singular) + "s";
    }
    return std::to_string(count) + " " + std::string(plural);
}

std::string repeatString(std::string_view value, std::size_t count) {
    std::string out;
    out.reserve(value.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        out.append(value);
    }
    return out;
}

std::string trimCopy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && isAsciiWhitespace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && isAsciiWhitespace(value[end - 1])) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

std::string lowerAscii(std::string_view value) {
    std::string out(value);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return out;
}

std::string upperAscii(std::string_view value) {
    std::string out(value);
    for (char& ch : out) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
    }
    return out;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

bool containsControlByte(std::string_view value) {
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f) {
            return true;
        }
    }
    return false;
}

bool isPrintableAscii(std::uint8_t value) {
    return value >= 0x20 && value <= 0x7e;
}

bool isAsciiWhitespace(char value) {
    return value == ' ' || value == '\t' || value == '\r' ||
           value == '\n' || value == '\f' || value == '\v';
}

bool isAsciiAlpha(char value) {
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
}

bool isAsciiDigit(char value) {
    return value >= '0' && value <= '9';
}

bool isAsciiHexDigit(char value) {
    return isAsciiDigit(value) || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

int hexDigitValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

LineBuilder::LineBuilder(std::size_t indent) : indent_(indent) {}

void LineBuilder::setIndent(std::size_t indent) {
    indent_ = indent;
}

void LineBuilder::increaseIndent(std::size_t amount) {
    indent_ += amount;
}

void LineBuilder::decreaseIndent(std::size_t amount) {
    indent_ = amount > indent_ ? 0 : indent_ - amount;
}

void LineBuilder::appendLine(std::string_view line) {
    output_.append(indent_, ' ');
    output_.append(line);
    output_.push_back('\n');
}

void LineBuilder::appendWrapped(std::string_view text, std::size_t width) {
    std::size_t position = 0;
    while (position < text.size()) {
        std::size_t available = width > indent_ ? width - indent_ : width;
        std::size_t take = std::min(available, text.size() - position);
        std::size_t split = take;
        if (position + take < text.size()) {
            for (std::size_t i = take; i > 0; --i) {
                if (isAsciiWhitespace(text[position + i - 1])) {
                    split = i;
                    break;
                }
            }
        }
        appendLine(trimCopy(text.substr(position, split)));
        position += split;
        while (position < text.size() && isAsciiWhitespace(text[position])) {
            ++position;
        }
    }
}

void LineBuilder::appendKeyValue(std::string_view key, std::string_view value) {
    appendLine(std::string(key) + ": " + std::string(value));
}

void LineBuilder::blankLine() {
    output_.push_back('\n');
}

std::string LineBuilder::str() const {
    return output_;
}

}  // namespace vector
