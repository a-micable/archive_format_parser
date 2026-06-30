#include "manifest.h"

#include <algorithm>
#include <sstream>

#include "byte_io.h"
#include "header.h"
#include "index.h"

namespace vector {
namespace {

void collectEntries(const std::vector<ManifestEntry>& entries,
                    std::vector<const ManifestEntry*>* out) {
    for (const ManifestEntry& entry : entries) {
        out->push_back(&entry);
        collectEntries(entry.children, out);
    }
}

void writeEntryText(LineBuilder* lines,
                    const ManifestEntry& entry,
                    const ManifestFormatOptions& options) {
    const std::string prefix(static_cast<std::size_t>(entry.depth) * 2, ' ');
    lines->appendLine(prefix + describeEntry(entry, options));
    if (options.show_nested_entries) {
        for (const ManifestEntry& child : entry.children) {
            writeEntryText(lines, child, options);
        }
    }
}

void writeEntryJson(std::ostringstream* out,
                    const ManifestEntry& entry,
                    int indent,
                    const ManifestFormatOptions& options) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    const std::string inner(static_cast<std::size_t>(indent + 2), ' ');
    *out << pad << "{\n";
    *out << inner << "\"name\": \"" << jsonEscape(entry.name) << "\",\n";
    *out << inner << "\"normalizedName\": \""
         << jsonEscape(entry.normalized_name) << "\",\n";
    *out << inner << "\"packedSize\": " << entry.packed_size << ",\n";
    *out << inner << "\"originalSize\": " << entry.original_size << ",\n";
    *out << inner << "\"compressed\": " << (entry.compressed ? "true" : "false") << ",\n";
    *out << inner << "\"nested\": " << (entry.nested ? "true" : "false") << ",\n";
    *out << inner << "\"depth\": " << entry.depth << ",\n";
    *out << inner << "\"chunk\": {"
         << "\"relativeOffset\":" << entry.chunk.relative_offset << ","
         << "\"absoluteOffset\":" << entry.chunk.absolute_offset << ","
         << "\"payloadOffset\":" << entry.chunk.payload_offset << ","
         << "\"payloadSize\":" << entry.chunk.payload_size << ","
         << "\"encodedEnd\":" << entry.chunk.encoded_end
         << "}";
    if (options.show_nested_entries && !entry.children.empty()) {
        *out << ",\n" << inner << "\"children\": [\n";
        for (std::size_t i = 0; i < entry.children.size(); ++i) {
            if (i != 0) {
                *out << ",\n";
            }
            writeEntryJson(out, entry.children[i], indent + 4, options);
        }
        *out << "\n" << inner << "]";
    }
    *out << "\n" << pad << "}";
}

void accumulateStats(const ManifestEntry& entry, ArchiveStats* stats) {
    ++stats->entry_count;
    stats->total_packed_size += entry.packed_size;
    stats->total_original_size += entry.original_size;
    stats->max_depth = std::max(stats->max_depth, entry.depth);
    if (entry.compressed) {
        ++stats->compressed_entry_count;
    }
    if (entry.nested) {
        ++stats->nested_archive_count;
    }
    if (entry.original_size >= stats->largest_entry_size) {
        stats->largest_entry_size = entry.original_size;
        stats->largest_entry_name = entry.name;
    }
    for (const ManifestEntry& child : entry.children) {
        accumulateStats(child, stats);
    }
}

}  // namespace

std::string extensionStateName(ExtensionState state) {
    switch (state) {
    case ExtensionState::kAbsent:
        return "absent";
    case ExtensionState::kPresentUnchecked:
        return "present";
    case ExtensionState::kChecksumValid:
        return "checksum-valid";
    case ExtensionState::kChecksumInvalid:
        return "checksum-invalid";
    case ExtensionState::kTruncated:
        return "truncated";
    }
    return "unknown";
}

std::string entryFlagsText(const ManifestEntry& entry) {
    std::vector<std::string> flags;
    if (entry.compressed || (entry.flags & kEntryCompressed) != 0) {
        flags.push_back("compressed");
    }
    if (entry.nested || (entry.flags & kEntryNested) != 0) {
        flags.push_back("nested");
    }
    if (flags.empty()) {
        return "none";
    }
    std::string out;
    for (std::size_t i = 0; i < flags.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += flags[i];
    }
    return out;
}

std::string archiveFlagsText(std::uint8_t flags) {
    std::vector<std::string> names;
    if ((flags & kFlagCompressed) != 0) {
        names.push_back("compressed");
    }
    if ((flags & kFlagExtension) != 0) {
        names.push_back("extension");
    }
    if ((flags & kFlagNested) != 0) {
        names.push_back("nested");
    }
    if (names.empty()) {
        return "none";
    }
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += names[i];
    }
    return out;
}

std::string formatManifestText(const ArchiveManifest& manifest,
                               const ManifestFormatOptions& options) {
    LineBuilder lines;
    lines.appendLine(manifestSummaryLine(manifest));
    lines.appendKeyValue("version", std::to_string(manifest.version));
    lines.appendKeyValue("flags", archiveFlagsText(manifest.flags));
    lines.appendKeyValue("archive size", humanSize(manifest.archive_size));
    lines.appendKeyValue("chunk stream offset", std::to_string(manifest.chunk_stream_offset));
    lines.appendKeyValue("chunk stream size", humanSize(manifest.chunk_stream_size));
    lines.appendKeyValue("extension", extensionStateName(manifest.extension_state));
    lines.blankLine();
    lines.appendLine("entries:");
    lines.increaseIndent(2);
    if (manifest.entries.empty()) {
        lines.appendLine("(none)");
    } else {
        for (const ManifestEntry& entry : manifest.entries) {
            writeEntryText(&lines, entry, options);
        }
    }
    lines.decreaseIndent(2);
    if (options.show_diagnostics && !manifest.diagnostics.empty()) {
        lines.blankLine();
        lines.appendLine("diagnostics:");
        lines.increaseIndent(2);
        DiagnosticFormatOptions diagnostic_options;
        diagnostic_options.include_summary = true;
        diagnostic_options.include_details = true;
        lines.appendLine(formatDiagnostics(manifest.diagnostics, diagnostic_options));
    }
    return lines.str();
}

std::string formatManifestJson(const ArchiveManifest& manifest,
                               const ManifestFormatOptions& options) {
    std::ostringstream out;
    const ArchiveStats stats = computeArchiveStats(manifest);
    out << "{\n";
    out << "  \"source\": \"" << jsonEscape(manifest.source_name) << "\",\n";
    out << "  \"version\": " << static_cast<int>(manifest.version) << ",\n";
    out << "  \"flags\": \"" << archiveFlagsText(manifest.flags) << "\",\n";
    out << "  \"archiveSize\": " << manifest.archive_size << ",\n";
    out << "  \"chunkStreamOffset\": " << manifest.chunk_stream_offset << ",\n";
    out << "  \"chunkStreamSize\": " << manifest.chunk_stream_size << ",\n";
    out << "  \"extension\": \"" << extensionStateName(manifest.extension_state) << "\",\n";
    out << "  \"stats\": {"
        << "\"entries\":" << stats.entry_count << ","
        << "\"nestedArchives\":" << stats.nested_archive_count << ","
        << "\"compressedEntries\":" << stats.compressed_entry_count << ","
        << "\"packedSize\":" << stats.total_packed_size << ","
        << "\"originalSize\":" << stats.total_original_size << ","
        << "\"maxDepth\":" << stats.max_depth
        << "},\n";
    out << "  \"entries\": [\n";
    for (std::size_t i = 0; i < manifest.entries.size(); ++i) {
        if (i != 0) {
            out << ",\n";
        }
        writeEntryJson(&out, manifest.entries[i], 4, options);
    }
    out << "\n  ]";
    if (options.show_diagnostics) {
        out << ",\n  \"diagnostics\": ";
        std::string diagnostic_json = diagnosticsToJson(manifest.diagnostics);
        std::istringstream in(diagnostic_json);
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (!first) {
                out << "\n  ";
            }
            out << line;
            first = false;
        }
    }
    out << "\n}\n";
    return out.str();
}

ArchiveStats computeArchiveStats(const ArchiveManifest& manifest) {
    ArchiveStats stats;
    stats.archive_size = manifest.archive_size;
    for (const ManifestEntry& entry : manifest.entries) {
        accumulateStats(entry, &stats);
    }
    return stats;
}

std::vector<const ManifestEntry*> flattenEntries(const ArchiveManifest& manifest) {
    std::vector<const ManifestEntry*> out;
    collectEntries(manifest.entries, &out);
    return out;
}

std::vector<const ManifestEntry*> flattenEntries(const ManifestEntry& entry) {
    std::vector<const ManifestEntry*> out;
    out.push_back(&entry);
    collectEntries(entry.children, &out);
    return out;
}

const ManifestEntry* findEntryByName(const ArchiveManifest& manifest,
                                     const std::string& name) {
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        if (entry->name == name || entry->normalized_name == name) {
            return entry;
        }
    }
    return nullptr;
}

std::string describeEntry(const ManifestEntry& entry,
                          const ManifestFormatOptions& options) {
    std::ostringstream out;
    out << entry.name;
    if (options.show_normalized_names && entry.name_changed) {
        out << " -> " << entry.normalized_name;
    }
    if (options.show_sizes) {
        out << " packed=" << humanSize(entry.packed_size)
            << " original=" << humanSize(entry.original_size);
        if (entry.original_size != 0) {
            out << " ratio=" << ratioString(entry.packed_size, entry.original_size);
        }
    }
    if (options.show_flags) {
        out << " flags=" << entryFlagsText(entry);
    }
    if (options.show_offsets) {
        out << " index_offset=" << entry.index_offset
            << " chunk_offset=" << entry.chunk.absolute_offset
            << " payload_offset=" << entry.chunk.payload_offset;
    }
    return out.str();
}

std::string describeArchiveStats(const ArchiveStats& stats) {
    std::ostringstream out;
    out << pluralize(stats.entry_count, "entry")
        << ", " << pluralize(stats.nested_archive_count, "nested archive")
        << ", " << pluralize(stats.compressed_entry_count, "compressed entry")
        << ", packed " << humanSize(stats.total_packed_size)
        << ", original " << humanSize(stats.total_original_size)
        << ", max depth " << stats.max_depth;
    if (!stats.largest_entry_name.empty()) {
        out << ", largest " << stats.largest_entry_name
            << " (" << humanSize(stats.largest_entry_size) << ")";
    }
    return out.str();
}

std::string manifestSummaryLine(const ArchiveManifest& manifest) {
    const ArchiveStats stats = computeArchiveStats(manifest);
    std::ostringstream out;
    out << "Vector archive";
    if (!manifest.source_name.empty()) {
        out << " " << manifest.source_name;
    }
    out << ": " << describeArchiveStats(stats);
    return out.str();
}

bool manifestHasEntry(const ArchiveManifest& manifest, const std::string& name) {
    return findEntryByName(manifest, name) != nullptr;
}

bool manifestHasNestedArchives(const ArchiveManifest& manifest) {
    return computeArchiveStats(manifest).nested_archive_count != 0;
}

bool manifestHasCompressedEntries(const ArchiveManifest& manifest) {
    return computeArchiveStats(manifest).compressed_entry_count != 0;
}

std::uint64_t countEntriesRecursive(const ArchiveManifest& manifest) {
    return computeArchiveStats(manifest).entry_count;
}

std::uint64_t countEntriesRecursive(const ManifestEntry& entry) {
    std::uint64_t count = 1;
    for (const ManifestEntry& child : entry.children) {
        count += countEntriesRecursive(child);
    }
    return count;
}

std::uint64_t sumPackedRecursive(const ArchiveManifest& manifest) {
    return computeArchiveStats(manifest).total_packed_size;
}

std::uint64_t sumOriginalRecursive(const ArchiveManifest& manifest) {
    return computeArchiveStats(manifest).total_original_size;
}

}  // namespace vector
