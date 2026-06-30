#include "inspect.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "archive.h"
#include "checksum.h"
#include "chunk.h"
#include "header.h"
#include "index.h"
#include "nested.h"
#include "path_utils.h"
#include "pool.h"

namespace vector {
namespace {

InspectResult inspectArchiveBytesInternal(const std::uint8_t* data,
                                          std::size_t size,
                                          const InspectOptions& options,
                                          int depth,
                                          std::uint64_t base_offset) {
    InspectResult result;
    result.manifest.source_name = options.source_name;
    result.manifest.archive_size = size;
    result.manifest.index_offset = 6;

    if (depth > options.max_depth) {
        result.manifest.diagnostics.error(
            IssueCategory::kNesting,
            "nesting.depth_limit",
            "nested archive depth limit exceeded",
            {static_cast<std::size_t>(base_offset), size},
            "limit is " + std::to_string(options.max_depth),
            "",
            depth);
        return result;
    }

    Reader reader(data, size);
    Header header;
    std::string error;
    if (!parseHeader(&reader, &header, &error)) {
        result.manifest.diagnostics.error(
            IssueCategory::kHeader,
            "header.parse",
            error,
            {static_cast<std::size_t>(base_offset), std::min<std::size_t>(size, 6)},
            "",
            "",
            depth);
        return result;
    }
    result.manifest.version = header.version;
    result.manifest.flags = header.flags;

    IndexTable index;
    const std::size_t index_start = reader.position();
    if (!parseIndex(&reader, &index, &error)) {
        result.manifest.diagnostics.error(
            IssueCategory::kIndex,
            "index.parse",
            error,
            {static_cast<std::size_t>(base_offset + index_start),
             size > index_start ? size - index_start : 0},
            "",
            "",
            depth);
        return result;
    }

    const std::size_t chunk_start = reader.position();
    result.manifest.chunk_stream_offset = base_offset + chunk_start;
    const std::uint8_t* chunk_stream = data + chunk_start;
    const std::size_t chunk_available = size - chunk_start;
    std::uint64_t chunk_end = 0;
    BufferPool pool;
    CollisionResolver resolver;

    for (const IndexEntry& index_entry : index.entries) {
        ManifestEntry entry;
        entry.name.assign(index_entry.name.begin(), index_entry.name.end());
        entry.normalized_name = sanitizeArchiveName(entry.name);
        entry.name_changed = entry.normalized_name != entry.name;
        entry.packed_size = index_entry.packed_size;
        entry.original_size = index_entry.original_size;
        entry.index_offset = index_entry.offset;
        entry.flags = index_entry.flags;
        entry.compressed = (index_entry.flags & kEntryCompressed) != 0;
        entry.nested = (index_entry.flags & kEntryNested) != 0;
        entry.depth = depth;

        CollisionResult collision = resolver.assign(entry.name);
        if (collision.collided) {
            result.manifest.diagnostics.warning(
                IssueCategory::kPath,
                "path.collision",
                "entry name collides after normalization",
                {0, entry.name.size()},
                "assigned extraction name: " + collision.assigned,
                entry.name,
                depth);
        }
        PathAnalysis path = analyzePath(entry.name);
        result.manifest.diagnostics.appendFrom(path.diagnostics);

        if (!readChunkLayout(chunk_stream, chunk_available,
                             result.manifest.chunk_stream_offset,
                             index_entry, &entry.chunk, &error)) {
            result.manifest.diagnostics.error(
                IssueCategory::kChunk,
                "chunk.layout",
                error,
                {static_cast<std::size_t>(result.manifest.chunk_stream_offset +
                                          index_entry.offset),
                 0},
                "",
                entry.name,
                depth);
            result.manifest.entries.push_back(std::move(entry));
            if (!options.continue_after_errors) {
                return result;
            }
            continue;
        }
        chunk_end = std::max(chunk_end, entry.chunk.encoded_end);

        if (options.decode_chunks) {
            ChunkHandle handle;
            if (!readChunk(chunk_stream, chunk_available, index_entry,
                           &pool, &handle, &error)) {
                result.manifest.diagnostics.error(
                    IssueCategory::kChunk,
                    "chunk.decode",
                    error,
                    {static_cast<std::size_t>(entry.chunk.absolute_offset),
                     static_cast<std::size_t>(entry.chunk.encoded_end -
                                              entry.chunk.relative_offset)},
                    "",
                    entry.name,
                    depth);
            } else {
                const bool payload_looks_nested = looksLikeVec(handle.bytes, handle.size);
                if (payload_looks_nested) {
                    entry.nested = true;
                }
                if (options.recurse_nested && entry.nested && handle.size != 0) {
                    InspectOptions child_options = options;
                    child_options.source_name = entry.name;
                    InspectResult child = inspectArchiveBytesInternal(
                        handle.bytes, handle.size, child_options, depth + 1,
                        entry.chunk.payload_offset);
                    entry.children = std::move(child.manifest.entries);
                    result.manifest.diagnostics.appendFrom(child.manifest.diagnostics);
                }
            }
        }
        result.manifest.entries.push_back(std::move(entry));
    }

    result.manifest.chunk_stream_size = chunk_end;
    if (chunk_end > chunk_available) {
        result.manifest.diagnostics.error(
            IssueCategory::kLayout,
            "layout.chunk_stream_bounds",
            "chunk stream extends beyond archive",
            {static_cast<std::size_t>(result.manifest.chunk_stream_offset),
             chunk_available},
            "",
            "",
            depth);
        return result;
    }

    std::uint64_t consumed = 0;
    const std::size_t extension_offset = chunk_start + static_cast<std::size_t>(chunk_end);
    result.manifest.extension_offset = base_offset + extension_offset;
    result.manifest.extension_state = inspectExtensionState(
        data + extension_offset, size - extension_offset,
        (header.flags & kFlagExtension) != 0,
        result.manifest.extension_offset,
        &consumed,
        &result.manifest.diagnostics);
    result.manifest.extension_size = consumed;
    result.parsed = !result.manifest.diagnostics.hasErrors();
    return result;
}

}  // namespace

InspectResult inspectArchiveBytes(const std::uint8_t* data,
                                  std::size_t size,
                                  const InspectOptions& options) {
    return inspectArchiveBytesInternal(data, size, options, 0, 0);
}

InspectResult inspectArchiveBytes(const std::vector<std::uint8_t>& bytes,
                                  const InspectOptions& options) {
    return inspectArchiveBytes(bytes.data(), bytes.size(), options);
}

InspectResult inspectArchiveFile(const std::filesystem::path& path,
                                 const InspectOptions& options) {
    std::string error;
    std::vector<std::uint8_t> bytes = readFileBytes(path, &error);
    InspectOptions actual = options;
    if (actual.source_name.empty()) {
        actual.source_name = path.string();
    }
    if (!error.empty()) {
        InspectResult result;
        result.manifest.source_name = path.string();
        result.manifest.diagnostics.error(IssueCategory::kIo,
                                          "io.read",
                                          error,
                                          {0, 0});
        return result;
    }
    return inspectArchiveBytes(bytes, actual);
}

bool inspectArchiveFile(const std::filesystem::path& path,
                        ArchiveManifest* manifest,
                        std::string* error,
                        const InspectOptions& options) {
    InspectResult result = inspectArchiveFile(path, options);
    *manifest = std::move(result.manifest);
    if (!result.parsed && error) {
        *error = formatDiagnosticSummary(manifest->diagnostics.summary());
    }
    return result.parsed;
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path,
                                        std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) {
            *error = "failed to open input: " + path.string();
        }
        return {};
    }
    input.seekg(0, std::ios::end);
    std::streamoff length = input.tellg();
    if (length < 0) {
        if (error) {
            *error = "failed to determine input size: " + path.string();
        }
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty() &&
        !input.read(reinterpret_cast<char*>(bytes.data()), length)) {
        if (error) {
            *error = "failed to read input: " + path.string();
        }
        return {};
    }
    if (error) {
        error->clear();
    }
    return bytes;
}

bool writeTextFile(const std::filesystem::path& path,
                   const std::string& text,
                   std::string* error) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            if (error) {
                *error = "failed to create output directory: " + ec.message();
            }
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) {
            *error = "failed to open output: " + path.string();
        }
        return false;
    }
    output << text;
    if (!output) {
        if (error) {
            *error = "failed to write output: " + path.string();
        }
        return false;
    }
    return true;
}

bool readChunkLayout(const std::uint8_t* chunk_stream,
                     std::size_t chunk_stream_size,
                     std::uint64_t chunk_stream_absolute_offset,
                     const IndexEntry& entry,
                     ChunkLayout* layout,
                     std::string* error) {
    if (entry.offset > chunk_stream_size) {
        if (error) {
            *error = "chunk offset is outside chunk stream";
        }
        return false;
    }
    Reader reader(chunk_stream + entry.offset, chunk_stream_size - entry.offset);
    std::uint8_t encoded_flags = 0;
    std::uint8_t flags = 0;
    std::uint64_t length = 0;
    const std::size_t before = reader.position();
    if (!reader.readByte(&encoded_flags) || !reader.readVarint(&length)) {
        if (error) {
            *error = "truncated chunk header";
        }
        return false;
    }
    if (!decodeFlagByte(encoded_flags, &flags)) {
        if (error) {
            *error = "invalid chunk flags";
        }
        return false;
    }
    if (length > reader.remaining()) {
        if (error) {
            *error = "chunk payload exceeds stream bounds";
        }
        return false;
    }
    if (length != entry.packed_size) {
        if (error) {
            *error = "index and chunk length disagree";
        }
        return false;
    }
    layout->relative_offset = entry.offset;
    layout->absolute_offset = chunk_stream_absolute_offset + entry.offset;
    layout->header_size = reader.position() - before;
    layout->payload_offset = layout->absolute_offset + layout->header_size;
    layout->payload_size = length;
    layout->encoded_end = entry.offset + layout->header_size + length;
    layout->chunk_flags = flags;
    layout->readable = true;
    return true;
}

ExtensionState inspectExtensionState(const std::uint8_t* data,
                                     std::size_t size,
                                     bool expected,
                                     std::uint64_t absolute_offset,
                                     std::uint64_t* consumed,
                                     Diagnostics* diagnostics) {
    *consumed = 0;
    if (!expected) {
        if (size != 0) {
            diagnostics->warning(IssueCategory::kExtension,
                                 "extension.trailing_bytes",
                                 "archive has trailing bytes after chunk stream",
                                 {static_cast<std::size_t>(absolute_offset), size});
        }
        return ExtensionState::kAbsent;
    }
    Reader reader(data, size);
    std::uint64_t expected_checksum = 0;
    std::uint64_t length = 0;
    if (!reader.readVarint(&expected_checksum) || !reader.readVarint(&length)) {
        diagnostics->error(IssueCategory::kExtension,
                           "extension.truncated_header",
                           "extension header is truncated",
                           {static_cast<std::size_t>(absolute_offset), size});
        return ExtensionState::kTruncated;
    }
    if (length > reader.remaining()) {
        diagnostics->error(IssueCategory::kExtension,
                           "extension.truncated_payload",
                           "extension payload is truncated",
                           {static_cast<std::size_t>(absolute_offset + reader.position()),
                            reader.remaining()});
        return ExtensionState::kTruncated;
    }
    const std::uint8_t* payload = nullptr;
    if (!reader.readBytes(static_cast<std::size_t>(length), &payload)) {
        diagnostics->error(IssueCategory::kExtension,
                           "extension.read",
                           "extension payload could not be read",
                           {static_cast<std::size_t>(absolute_offset + reader.position()), 0});
        return ExtensionState::kTruncated;
    }
    *consumed = reader.position();
    const std::uint64_t actual = checksumBytes(payload, static_cast<std::size_t>(length));
    if (actual != expected_checksum) {
        diagnostics->warning(IssueCategory::kExtension,
                             "extension.checksum",
                             "extension checksum does not match",
                             {static_cast<std::size_t>(absolute_offset), reader.position()},
                             "expected " + std::to_string(expected_checksum) +
                                 ", actual " + std::to_string(actual));
        return ExtensionState::kChecksumInvalid;
    }
    return ExtensionState::kChecksumValid;
}

}  // namespace vector
