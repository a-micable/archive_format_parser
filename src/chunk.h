#ifndef VECTOR_ARCHIVE_CHUNK_H_
#define VECTOR_ARCHIVE_CHUNK_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "header.h"
#include "index.h"
#include "pool.h"

namespace vector {

struct ChunkHandle {
    const std::uint8_t* bytes = nullptr;
    std::size_t size = 0;
    std::uint8_t flags = 0;
    std::size_t pool_generation = 0;
};

bool readChunk(const std::uint8_t* chunk_stream,
               std::size_t chunk_stream_size,
               const IndexEntry& entry,
               BufferPool* pool,
               ChunkHandle* handle,
               std::string* error);

std::vector<std::uint8_t> encodeChunkPayload(const std::vector<std::uint8_t>& input,
                                             bool compress,
                                             std::uint8_t* flags);

std::vector<std::uint8_t> encodeRle(const std::vector<std::uint8_t>& input);
bool decodeRle(const std::uint8_t* data,
               std::size_t size,
               std::size_t expected_size,
               BufferPool* pool,
               ChunkHandle* handle,
               std::string* error);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_CHUNK_H_
