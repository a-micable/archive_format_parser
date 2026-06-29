#include "chunk.h"

#include <algorithm>
#include <cstring>

namespace vector {
namespace {

constexpr std::uint64_t kMaxChunkSize = 64ull * 1024ull * 1024ull;

}  // namespace

std::vector<std::uint8_t> encodeRle(const std::vector<std::uint8_t>& input) {
    std::vector<std::uint8_t> out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        std::uint8_t value = input[i];
        std::size_t count = 1;
        while (i + count < input.size() && input[i + count] == value && count < 255) {
            ++count;
        }
        out.push_back(static_cast<std::uint8_t>(count));
        out.push_back(value);
        i += count;
    }
    return out;
}

bool decodeRle(const std::uint8_t* data,
               std::size_t size,
               std::size_t expected_size,
               BufferPool* pool,
               ChunkHandle* handle,
               std::string* error) {
    BufferBlock block = pool->acquire(expected_size);
    std::size_t out = 0;
    for (std::size_t i = 0; i < size;) {
        if (i + 2 > size) {
            if (error) *error = "truncated compressed chunk";
            return false;
        }
        std::uint8_t count = data[i++];
        std::uint8_t value = data[i++];
        if (count == 0 || out + count > expected_size) {
            if (error) *error = "invalid compressed chunk run";
            return false;
        }
        std::memset(block.data + out, value, count);
        out += count;
    }
    if (out != expected_size) {
        if (error) *error = "compressed chunk size mismatch";
        return false;
    }
    handle->bytes = block.data;
    handle->size = out;
    handle->flags = kEntryCompressed;
    handle->pool_generation = pool->generation();
    return true;
}

bool readChunk(const std::uint8_t* chunk_stream,
               std::size_t chunk_stream_size,
               const IndexEntry& entry,
               BufferPool* pool,
               ChunkHandle* handle,
               std::string* error) {
    if (entry.offset > chunk_stream_size) {
        if (error) *error = "chunk offset is outside chunk stream";
        return false;
    }

    Reader reader(chunk_stream + entry.offset, chunk_stream_size - entry.offset);
    std::uint8_t chunk_flags = 0;
    std::uint64_t length = 0;
    if (!reader.readByte(&chunk_flags) || !reader.readVarint(&length)) {
        if (error) *error = "truncated chunk header";
        return false;
    }
    if (!decodeFlagByte(chunk_flags, &chunk_flags)) {
        if (error) *error = "invalid chunk flags";
        return false;
    }
    if (length > kMaxChunkSize || length > reader.remaining()) {
        if (error) *error = "invalid chunk length";
        return false;
    }
    if (length != entry.packed_size) {
        if (error) *error = "index and chunk length disagree";
        return false;
    }

    const std::uint8_t* payload = nullptr;
    if (!reader.readBytes(static_cast<std::size_t>(length), &payload)) {
        if (error) *error = "truncated chunk payload";
        return false;
    }

    if ((chunk_flags & kEntryCompressed) != 0) {
        return decodeRle(payload, static_cast<std::size_t>(length),
                         static_cast<std::size_t>(entry.original_size),
                         pool, handle, error);
    }

    if (entry.original_size != length) {
        if (error) *error = "uncompressed chunk size mismatch";
        return false;
    }

    BufferBlock block = pool->acquire(static_cast<std::size_t>(length));
    if (length != 0) {
        std::memcpy(block.data, payload, static_cast<std::size_t>(length));
    }
    handle->bytes = block.data;
    handle->size = static_cast<std::size_t>(length);
    handle->flags = chunk_flags;
    handle->pool_generation = pool->generation();
    return true;
}

std::vector<std::uint8_t> encodeChunkPayload(const std::vector<std::uint8_t>& input,
                                             bool compress,
                                             std::uint8_t* flags) {
    *flags = 0;
    if (!compress || input.empty()) {
        return input;
    }

    std::vector<std::uint8_t> encoded = encodeRle(input);
    if (encoded.size() >= input.size()) {
        return input;
    }

    *flags |= kEntryCompressed;
    return encoded;
}

}  // namespace vector
