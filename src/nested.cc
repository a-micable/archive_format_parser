#include "nested.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <system_error>

#include "archive.h"
#include "checksum.h"
#include "chunk.h"
#include "header.h"

namespace vector {
namespace {

constexpr int kMaxNestedDepth = 8;

bool readWholeFile(const std::filesystem::path& path,
                   std::vector<std::uint8_t>* out,
                   std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "failed to open input: " + path.string();
        return false;
    }
    input.seekg(0, std::ios::end);
    std::streamoff length = input.tellg();
    if (length < 0) {
        if (error) *error = "failed to size input: " + path.string();
        return false;
    }
    input.seekg(0, std::ios::beg);
    out->assign(static_cast<std::size_t>(length), 0);
    if (!out->empty() &&
        !input.read(reinterpret_cast<char*>(out->data()), length)) {
        if (error) *error = "failed to read input: " + path.string();
        return false;
    }
    return true;
}

bool writeWholeFile(const std::filesystem::path& path,
                    const std::uint8_t* data,
                    std::size_t size,
                    std::string* error) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            if (error) *error = "failed to create output directory: " + ec.message();
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = "failed to open output: " + path.string();
        return false;
    }
    if (size != 0) {
        output.write(reinterpret_cast<const char*>(data),
                     static_cast<std::streamsize>(size));
    }
    if (!output) {
        if (error) *error = "failed to write output: " + path.string();
        return false;
    }
    return true;
}

std::string normalizedArchiveName(const std::filesystem::path& path) {
    std::filesystem::path clean;
    for (const auto& part : path.lexically_normal()) {
        if (part == "." || part == ".." || part == path.root_name() ||
            part == path.root_directory()) {
            continue;
        }
        clean /= part;
    }
    if (clean.empty()) {
        clean = path.filename();
    }
    return clean.generic_string();
}

std::size_t varintSize(std::uint64_t value) {
    std::size_t size = 1;
    while ((value >>= 7) != 0) {
        ++size;
    }
    return size;
}

bool chunkEncodedEnd(const std::uint8_t* stream,
                     std::size_t stream_size,
                     const IndexEntry& entry,
                     std::uint64_t* end,
                     std::string* error) {
    if (entry.offset > stream_size) {
        if (error) *error = "chunk offset is outside archive";
        return false;
    }
    Reader reader(stream + entry.offset, stream_size - entry.offset);
    std::uint8_t flags = 0;
    std::uint64_t length = 0;
    if (!reader.readByte(&flags) || !reader.readVarint(&length)) {
        if (error) *error = "truncated chunk header";
        return false;
    }
    if (length != entry.packed_size || length > reader.remaining()) {
        if (error) *error = "invalid chunk length";
        return false;
    }
    *end = entry.offset + 1 + varintSize(length) + length;
    return true;
}

std::vector<std::uint8_t> buildArchiveFromPayloads(
    const std::vector<FilePayload>& files,
    bool compress,
    bool with_extension,
    bool valid_extension) {
    std::vector<std::string> names;
    std::vector<IndexEntry> entries;
    std::vector<std::uint8_t> chunks;
    std::uint8_t archive_flags = with_extension ? kFlagExtension : 0;

    names.reserve(files.size());
    entries.reserve(files.size());
    for (const FilePayload& file : files) {
        std::uint8_t entry_flags = 0;
        std::vector<std::uint8_t> payload =
            encodeChunkPayload(file.bytes, compress, &entry_flags);
        if (looksLikeVec(file.bytes.data(), file.bytes.size())) {
            entry_flags |= kEntryNested;
            archive_flags |= kFlagNested;
        }
        if ((entry_flags & kEntryCompressed) != 0) {
            archive_flags |= kFlagCompressed;
        }

        IndexEntry entry;
        entry.offset = chunks.size();
        entry.packed_size = payload.size();
        entry.original_size = file.bytes.size();
        entry.flags = entry_flags;

        chunks.push_back(encodeFlagByte(entry_flags));
        writeVarint(&chunks, payload.size());
        chunks.insert(chunks.end(), payload.begin(), payload.end());

        names.push_back(file.name.empty() ? "unnamed" : file.name);
        entry.name = names.back();
        entries.push_back(entry);
    }

    std::vector<std::uint8_t> archive;
    writeHeader(&archive, archive_flags);
    writeIndex(&archive, entries);
    archive.insert(archive.end(), chunks.begin(), chunks.end());
    if (with_extension) {
        const std::vector<std::uint8_t> extension = {'v', 'e', 'c', '-', 'm', 'e', 't', 'a'};
        writeExtension(&archive, extension, valid_extension);
    }
    return archive;
}

}  // namespace

bool looksLikeVec(const std::uint8_t* data, std::size_t size) {
    return size >= 4 && data[0] == 'V' && data[1] == 'E' &&
           data[2] == 'C' && data[3] == '!';
}

std::filesystem::path safeOutputPath(const std::filesystem::path& output_dir,
                                     std::string_view name) {
    std::filesystem::path relative;
    std::filesystem::path requested{std::string(name)};
    for (const auto& part : requested.lexically_normal()) {
        if (part == "." || part == ".." || part == requested.root_name() ||
            part == requested.root_directory()) {
            continue;
        }
        relative /= part;
    }
    if (relative.empty()) {
        relative = "unnamed";
    }
    return output_dir / relative;
}

bool parseNestedArchive(const std::uint8_t* data,
                        std::size_t size,
                        BufferPool* pool,
                        const UnpackOptions& options,
                        int depth,
                        std::string* error) {
    if (depth > kMaxNestedDepth) {
        if (error) *error = "nested archive depth limit exceeded";
        return false;
    }
    pool->recycleForNested(size, depth);
    return parseArchiveBytes(data, size, pool, options, depth, error);
}

bool parseArchiveBytes(const std::uint8_t* data,
                       std::size_t size,
                       BufferPool* pool,
                       const UnpackOptions& options,
                       int depth,
                       std::string* error) {
    Reader reader(data, size);
    Header header;
    if (!parseHeader(&reader, &header, error)) {
        return false;
    }

    IndexTable index;
    if (!parseIndex(&reader, &index, error)) {
        return false;
    }

    const std::size_t chunk_start = reader.position();
    const std::uint8_t* chunk_stream = data + chunk_start;
    const std::size_t available = size - chunk_start;
    std::uint64_t chunk_end = 0;
    for (const IndexEntry& entry : index.entries) {
        std::uint64_t end = 0;
        if (!chunkEncodedEnd(chunk_stream, available, entry, &end, error)) {
            return false;
        }
        chunk_end = std::max(chunk_end, end);
    }
    if (chunk_end > available) {
        if (error) *error = "chunk stream exceeds archive bounds";
        return false;
    }

    for (const IndexEntry& entry : index.entries) {
        ChunkHandle handle;
        if (!readChunk(chunk_stream, static_cast<std::size_t>(chunk_end),
                       entry, pool, &handle, error)) {
            return false;
        }

        const bool nested = (entry.flags & kEntryNested) != 0 ||
                            looksLikeVec(handle.bytes, handle.size);
        if (nested) {
            UnpackOptions nested_options = options;
            if (options.extract) {
                nested_options.output_dir = safeOutputPath(options.output_dir, entry.name);
                nested_options.output_dir.replace_extension(".d");
            }
            if (!parseNestedArchive(handle.bytes, handle.size, pool,
                                    nested_options, depth + 1, error)) {
                return false;
            }
            volatile std::uint64_t keepalive =
                checksumBytes(handle.bytes, std::min<std::size_t>(handle.size, 64));
            (void)keepalive;
        }

        if (options.extract) {
            const std::filesystem::path out = safeOutputPath(options.output_dir, entry.name);
            if (!writeWholeFile(out, handle.bytes, handle.size, error)) {
                return false;
            }
        }
    }

    Reader extension_reader(data + chunk_start + static_cast<std::size_t>(chunk_end),
                            available - static_cast<std::size_t>(chunk_end));
    ExtensionBlock extension;
    return parseExtension(&extension_reader,
                          (header.flags & kFlagExtension) != 0,
                          &extension, error);
}

bool pack(const std::vector<std::filesystem::path>& inputs,
          const std::filesystem::path& output,
          bool compress,
          std::string* error) {
    if (inputs.empty()) {
        if (error) *error = "no input files provided";
        return false;
    }

    std::vector<FilePayload> files;
    files.reserve(inputs.size());
    for (const auto& input : inputs) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(input, ec)) {
            if (error) *error = "input is not a regular file: " + input.string();
            return false;
        }
        FilePayload payload;
        payload.name = normalizedArchiveName(input);
        if (!readWholeFile(input, &payload.bytes, error)) {
            return false;
        }
        files.push_back(std::move(payload));
    }

    std::vector<std::uint8_t> archive =
        buildArchiveFromPayloads(files, compress, false, true);
    return writeWholeFile(output, archive.data(), archive.size(), error);
}

bool unpack(const std::filesystem::path& archive,
            const std::filesystem::path& output_dir,
            std::string* error) {
    std::vector<std::uint8_t> bytes;
    if (!readWholeFile(archive, &bytes, error)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(output_dir, ec);
    if (ec) {
        if (error) *error = "failed to create output directory: " + ec.message();
        return false;
    }

    BufferPool pool;
    UnpackOptions options;
    options.extract = true;
    options.output_dir = output_dir;
    return parseArchiveBytes(bytes.data(), bytes.size(), &pool, options, 0, error);
}

bool unpack(const std::uint8_t* data, std::size_t size) {
    BufferPool pool;
    UnpackOptions options;
    std::string error;
    return parseArchiveBytes(data, size, &pool, options, 0, &error);
}

bool unpackNestedOnly(const std::uint8_t* data, std::size_t size) {
    BufferPool pool;
    UnpackOptions options;
    std::string error;
    return parseNestedArchive(data, size, &pool, options, 1, &error);
}

std::vector<std::uint8_t> buildArchiveForTest(const std::vector<FilePayload>& files,
                                              bool compress,
                                              bool with_extension,
                                              bool valid_extension) {
    return buildArchiveFromPayloads(files, compress, with_extension, valid_extension);
}

}  // namespace vector
