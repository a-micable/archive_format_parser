#include "validator.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "byte_io.h"
#include "format_rules.h"
#include "header.h"
#include "index.h"

namespace vector {
namespace {

void validateDuplicates(const ArchiveManifest& manifest,
                        const ValidationOptions& options,
                        Diagnostics* diagnostics) {
    if (!options.warn_on_duplicates) {
        return;
    }
    std::map<std::string, const ManifestEntry*> seen;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        const std::string key = canonicalCollisionKey(entry->normalized_name);
        auto inserted = seen.emplace(key, entry);
        if (!inserted.second) {
            diagnostics->warning(
                IssueCategory::kPath,
                "path.duplicate",
                "duplicate entry name after normalization",
                {static_cast<std::size_t>(entry->chunk.absolute_offset), 0},
                "first entry: " + inserted.first->second->name,
                entry->name,
                entry->depth);
        }
    }
}

void validateLayoutOrder(const ArchiveManifest& manifest,
                         const ValidationOptions& options,
                         Diagnostics* diagnostics) {
    if (!options.warn_on_sparse_layout) {
        return;
    }
    std::vector<const ManifestEntry*> entries = flattenEntries(manifest);
    std::sort(entries.begin(), entries.end(),
              [](const ManifestEntry* a, const ManifestEntry* b) {
                  return a->chunk.absolute_offset < b->chunk.absolute_offset;
              });
    std::uint64_t previous_end = manifest.chunk_stream_offset;
    for (const ManifestEntry* entry : entries) {
        if (entry->depth != 0) {
            continue;
        }
        if (!entry->chunk.readable) {
            continue;
        }
        if (entry->chunk.absolute_offset < previous_end) {
            diagnostics->error(
                IssueCategory::kLayout,
                "layout.overlap",
                "chunk overlaps previous chunk",
                {static_cast<std::size_t>(entry->chunk.absolute_offset),
                 static_cast<std::size_t>(entry->chunk.encoded_end -
                                          entry->chunk.relative_offset)},
                "",
                entry->name,
                entry->depth);
        } else if (entry->chunk.absolute_offset > previous_end) {
            diagnostics->info(
                IssueCategory::kLayout,
                "layout.gap",
                "unused bytes appear between chunks",
                {static_cast<std::size_t>(previous_end),
                 static_cast<std::size_t>(entry->chunk.absolute_offset - previous_end)},
                "",
                entry->name,
                entry->depth);
        }
        previous_end = std::max(previous_end,
                                entry->chunk.absolute_offset +
                                    entry->chunk.header_size +
                                    entry->chunk.payload_size);
    }
}

}  // namespace

ValidationResult validateArchiveBytes(const std::uint8_t* data,
                                      std::size_t size,
                                      const ValidationOptions& options) {
    InspectResult inspected = inspectArchiveBytes(data, size, options.inspect);
    ValidationResult result;
    result.manifest = std::move(inspected.manifest);
    result.diagnostics.appendFrom(result.manifest.diagnostics);
    validateManifest(result.manifest, options, &result.diagnostics);
    result.ok = !result.diagnostics.hasErrors();
    return result;
}

ValidationResult validateArchiveBytes(const std::vector<std::uint8_t>& bytes,
                                      const ValidationOptions& options) {
    return validateArchiveBytes(bytes.data(), bytes.size(), options);
}

ValidationResult validateArchiveFile(const std::filesystem::path& path,
                                     const ValidationOptions& options) {
    std::string error;
    std::vector<std::uint8_t> bytes = readFileBytes(path, &error);
    if (!error.empty()) {
        ValidationResult result;
        result.diagnostics.error(IssueCategory::kIo,
                                 "io.read",
                                 error,
                                 {0, 0});
        result.manifest.diagnostics.appendFrom(result.diagnostics);
        return result;
    }
    ValidationOptions actual = options;
    if (actual.inspect.source_name.empty()) {
        actual.inspect.source_name = path.string();
    }
    return validateArchiveBytes(bytes, actual);
}

void validateManifest(const ArchiveManifest& manifest,
                      const ValidationOptions& options,
                      Diagnostics* diagnostics) {
    checkArchiveFormatRules(manifest, defaultFormatLimits(), diagnostics);
    validateArchiveTotals(manifest, options, diagnostics);
    validateDuplicates(manifest, options, diagnostics);
    validateLayoutOrder(manifest, options, diagnostics);
    if (options.require_extension_checksum &&
        manifest.extension_state == ExtensionState::kChecksumInvalid) {
        diagnostics->error(IssueCategory::kExtension,
                           "extension.required_checksum",
                           "extension checksum is required but invalid",
                           {static_cast<std::size_t>(manifest.extension_offset),
                            static_cast<std::size_t>(manifest.extension_size)});
    }
    for (const ManifestEntry& entry : manifest.entries) {
        validateEntryTree(entry, options, diagnostics);
    }
}

void validateEntryTree(const ManifestEntry& entry,
                       const ValidationOptions& options,
                       Diagnostics* diagnostics) {
    validatePathPolicy(entry, options, diagnostics);
    validateChunkLayout(entry, options, diagnostics);
    validateCompressionProfile(entry, options, diagnostics);
    validateFlagProfile(entry, options, diagnostics);
    for (const ManifestEntry& child : entry.children) {
        validateEntryTree(child, options, diagnostics);
    }
}

void validatePathPolicy(const ManifestEntry& entry,
                        const ValidationOptions& options,
                        Diagnostics* diagnostics) {
    PathAnalysis analysis = analyzePath(entry.name, options.path_policy);
    diagnostics->appendFrom(analysis.diagnostics);
    if (options.require_canonical_paths && analysis.changed) {
        diagnostics->warning(IssueCategory::kPath,
                             "path.not_canonical",
                             "entry name is not canonical",
                             {0, entry.name.size()},
                             "canonical form: " + analysis.normalized,
                             entry.name,
                             entry.depth);
    }
    if (analysis.normalized.empty()) {
        diagnostics->error(IssueCategory::kPath,
                           "path.no_output_name",
                           "entry has no usable output name",
                           {0, entry.name.size()},
                           "",
                           entry.name,
                           entry.depth);
    }
}

void validateChunkLayout(const ManifestEntry& entry,
                         const ValidationOptions& options,
                         Diagnostics* diagnostics) {
    if (!entry.chunk.readable) {
        diagnostics->error(IssueCategory::kChunk,
                           "chunk.unreadable",
                           "chunk layout could not be read",
                           {static_cast<std::size_t>(entry.chunk.absolute_offset), 0},
                           "",
                           entry.name,
                           entry.depth);
        return;
    }
    if (entry.original_size > options.max_entry_size) {
        diagnostics->warning(
            IssueCategory::kChunk,
            "chunk.large_entry",
            "entry original size exceeds validation policy",
            {static_cast<std::size_t>(entry.chunk.payload_offset),
             static_cast<std::size_t>(entry.chunk.payload_size)},
            "limit is " + humanSize(options.max_entry_size),
            entry.name,
            entry.depth);
    }
    if (options.warn_on_empty_entries && entry.original_size == 0) {
        diagnostics->info(IssueCategory::kChunk,
                          "chunk.empty_entry",
                          "entry payload is empty",
                          {static_cast<std::size_t>(entry.chunk.payload_offset), 0},
                          "",
                          entry.name,
                          entry.depth);
    }
    if (entry.chunk.payload_size != entry.packed_size) {
        diagnostics->error(IssueCategory::kChunk,
                           "chunk.size_mismatch",
                           "manifest packed size does not match chunk payload size",
                           {static_cast<std::size_t>(entry.chunk.payload_offset),
                            static_cast<std::size_t>(entry.chunk.payload_size)},
                           "",
                           entry.name,
                           entry.depth);
    }
}

void validateCompressionProfile(const ManifestEntry& entry,
                                const ValidationOptions& options,
                                Diagnostics* diagnostics) {
    if (!options.warn_on_ratio_anomalies) {
        return;
    }
    if (entry.packed_size == 0 && entry.original_size != 0) {
        diagnostics->warning(IssueCategory::kCompression,
                             "compression.zero_packed",
                             "non-empty entry has zero packed size",
                             {static_cast<std::size_t>(entry.chunk.payload_offset), 0},
                             "",
                             entry.name,
                             entry.depth);
        return;
    }
    if (entry.packed_size != 0) {
        const double expansion = static_cast<double>(entry.original_size) /
                                 static_cast<double>(entry.packed_size);
        if (entry.compressed && expansion > options.high_compression_ratio) {
            diagnostics->info(
                IssueCategory::kCompression,
                "compression.high_ratio",
                "entry has a high compression ratio",
                {static_cast<std::size_t>(entry.chunk.payload_offset),
                 static_cast<std::size_t>(entry.chunk.payload_size)},
                "ratio is " + ratioString(entry.original_size, entry.packed_size),
                entry.name,
                entry.depth);
        }
        const double packed_growth = static_cast<double>(entry.packed_size) /
                                     static_cast<double>(std::max<std::uint64_t>(1, entry.original_size));
        if (packed_growth > options.expansion_ratio) {
            diagnostics->warning(
                IssueCategory::kCompression,
                "compression.expansion",
                "packed payload is much larger than original size",
                {static_cast<std::size_t>(entry.chunk.payload_offset),
                 static_cast<std::size_t>(entry.chunk.payload_size)},
                "ratio is " + ratioString(entry.packed_size,
                                           std::max<std::uint64_t>(1, entry.original_size)),
                entry.name,
                entry.depth);
        }
    }
    if (entry.compressed && entry.packed_size >= entry.original_size &&
        entry.original_size != 0) {
        diagnostics->info(IssueCategory::kCompression,
                          "compression.no_savings",
                          "compressed entry does not reduce size",
                          {static_cast<std::size_t>(entry.chunk.payload_offset),
                           static_cast<std::size_t>(entry.chunk.payload_size)},
                          "",
                          entry.name,
                          entry.depth);
    }
    EntryProfile profile = profileManifestEntry(entry);
    if (entry.compressed && profile.commonly_compressed) {
        diagnostics->info(IssueCategory::kCompression,
                          "compression.already_compressed_type",
                          "entry extension usually already contains compressed data",
                          {static_cast<std::size_t>(entry.chunk.payload_offset),
                           static_cast<std::size_t>(entry.chunk.payload_size)},
                          describeEntryProfile(profile),
                          entry.name,
                          entry.depth);
    }
    for (EntryNameAdvisory advisory : profile.advisories) {
        if (advisory == EntryNameAdvisory::kTemporary ||
            advisory == EntryNameAdvisory::kBackup ||
            advisory == EntryNameAdvisory::kEditorSwap ||
            advisory == EntryNameAdvisory::kSystemMetadata) {
            diagnostics->info(IssueCategory::kPolicy,
                              "name." + advisoryName(advisory),
                              advisoryDescription(advisory),
                              {0, entry.name.size()},
                              describeEntryProfile(profile),
                              entry.name,
                              entry.depth);
        }
    }
}

void validateFlagProfile(const ManifestEntry& entry,
                         const ValidationOptions& options,
                         Diagnostics* diagnostics) {
    if (!options.warn_on_unknown_flags) {
        return;
    }
    constexpr std::uint64_t known = kEntryCompressed | kEntryNested;
    const std::uint64_t unknown = entry.flags & ~known;
    if (unknown != 0) {
        diagnostics->warning(IssueCategory::kIndex,
                             "index.unknown_flags",
                             "entry uses unknown flag bits",
                             {static_cast<std::size_t>(entry.chunk.absolute_offset), 1},
                             "unknown bits: " + hexWord(unknown),
                             entry.name,
                             entry.depth);
    }
    if (entry.compressed && (entry.chunk.chunk_flags & kEntryCompressed) == 0) {
        diagnostics->warning(IssueCategory::kChunk,
                             "chunk.flag_disagreement",
                             "index marks entry compressed but chunk does not",
                             {static_cast<std::size_t>(entry.chunk.absolute_offset), 1},
                             "",
                             entry.name,
                             entry.depth);
    }
    if (!entry.compressed && (entry.chunk.chunk_flags & kEntryCompressed) != 0) {
        diagnostics->warning(IssueCategory::kChunk,
                             "chunk.flag_disagreement",
                             "chunk marks entry compressed but index does not",
                             {static_cast<std::size_t>(entry.chunk.absolute_offset), 1},
                             "",
                             entry.name,
                             entry.depth);
    }
}

void validateArchiveTotals(const ArchiveManifest& manifest,
                           const ValidationOptions& options,
                           Diagnostics* diagnostics) {
    if (manifest.archive_size > options.max_archive_size) {
        diagnostics->warning(IssueCategory::kFormat,
                             "archive.large",
                             "archive size exceeds validation policy",
                             {0, static_cast<std::size_t>(manifest.archive_size)},
                             "limit is " + humanSize(options.max_archive_size));
    }
    const ArchiveStats stats = computeArchiveStats(manifest);
    if (stats.entry_count == 0 && manifest.diagnostics.ok()) {
        diagnostics->info(IssueCategory::kIndex,
                          "index.empty",
                          "archive contains no entries",
                          {static_cast<std::size_t>(manifest.index_offset), 0});
    }
    if (stats.max_depth > options.inspect.max_depth) {
        diagnostics->error(IssueCategory::kNesting,
                           "nesting.depth",
                           "archive nesting exceeds validation policy",
                           {0, static_cast<std::size_t>(manifest.archive_size)},
                           "max depth is " + std::to_string(stats.max_depth));
    }
}

std::string formatValidationText(const ValidationResult& result) {
    LineBuilder lines;
    lines.appendLine(manifestSummaryLine(result.manifest));
    lines.appendLine(result.ok ? "validation: ok" : "validation: failed");
    lines.blankLine();
    DiagnosticFormatOptions options;
    options.include_summary = true;
    lines.appendLine(formatDiagnostics(result.diagnostics, options));
    return lines.str();
}

std::string formatValidationJson(const ValidationResult& result) {
    std::ostringstream out;
    out << "{\n"
        << "  \"ok\": " << (result.ok ? "true" : "false") << ",\n"
        << "  \"manifest\": " << formatManifestJson(result.manifest) << ",\n"
        << "  \"diagnostics\": " << diagnosticsToJson(result.diagnostics)
        << "}\n";
    return out.str();
}

}  // namespace vector
