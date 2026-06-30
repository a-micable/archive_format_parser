#include "archive_stats.h"

#include <algorithm>
#include <sstream>

#include "byte_io.h"
#include "path_utils.h"

namespace vector {
namespace {

void addToBucket(SizeBucket* bucket, const ManifestEntry& entry) {
    ++bucket->count;
    bucket->packed_total += entry.packed_size;
    bucket->original_total += entry.original_size;
}

SizeBucket* findBucket(std::vector<SizeBucket>* buckets, std::uint64_t size) {
    for (SizeBucket& bucket : *buckets) {
        const bool below_max = bucket.max_exclusive == 0 || size < bucket.max_exclusive;
        if (size >= bucket.min_inclusive && below_max) {
            return &bucket;
        }
    }
    return buckets->empty() ? nullptr : &buckets->back();
}

ExtensionSummary* findExtension(std::vector<ExtensionSummary>* summaries,
                                const std::string& extension) {
    for (ExtensionSummary& summary : *summaries) {
        if (summary.extension == extension) {
            return &summary;
        }
    }
    summaries->push_back({extension});
    return &summaries->back();
}

DepthSummary* findDepth(std::vector<DepthSummary>* summaries, int depth) {
    for (DepthSummary& summary : *summaries) {
        if (summary.depth == depth) {
            return &summary;
        }
    }
    DepthSummary summary;
    summary.depth = depth;
    summaries->push_back(summary);
    return &summaries->back();
}

void appendEntryLayout(const ManifestEntry& entry,
                       std::vector<LayoutSegment>* segments) {
    if (entry.chunk.readable) {
        LayoutSegment header;
        header.kind = LayoutSegmentKind::kChunkHeader;
        header.offset = entry.chunk.absolute_offset;
        header.length = entry.chunk.header_size;
        header.label = "chunk header";
        header.entry_name = entry.name;
        header.depth = entry.depth;
        segments->push_back(header);

        LayoutSegment payload;
        payload.kind = LayoutSegmentKind::kChunkPayload;
        payload.offset = entry.chunk.payload_offset;
        payload.length = entry.chunk.payload_size;
        payload.label = "chunk payload";
        payload.entry_name = entry.name;
        payload.depth = entry.depth;
        segments->push_back(payload);
    }
    for (const ManifestEntry& child : entry.children) {
        appendEntryLayout(child, segments);
    }
}

void sortAndAddGaps(std::vector<LayoutSegment>* segments,
                    std::uint64_t archive_size) {
    std::sort(segments->begin(), segments->end(),
              [](const LayoutSegment& a, const LayoutSegment& b) {
                  if (a.offset != b.offset) {
                      return a.offset < b.offset;
                  }
                  return a.length < b.length;
              });
    std::vector<LayoutSegment> with_gaps;
    std::uint64_t cursor = 0;
    for (const LayoutSegment& segment : *segments) {
        if (segment.depth == 0 && segment.offset > cursor) {
            LayoutSegment gap;
            gap.kind = LayoutSegmentKind::kGap;
            gap.offset = cursor;
            gap.length = segment.offset - cursor;
            gap.label = "unassigned bytes";
            with_gaps.push_back(gap);
        }
        with_gaps.push_back(segment);
        if (segment.depth == 0) {
            cursor = std::max(cursor, segmentEnd(segment));
        }
    }
    if (archive_size > cursor) {
        LayoutSegment trailing;
        trailing.kind = LayoutSegmentKind::kTrailing;
        trailing.offset = cursor;
        trailing.length = archive_size - cursor;
        trailing.label = "trailing bytes";
        with_gaps.push_back(trailing);
    }
    *segments = std::move(with_gaps);
}

std::vector<const ManifestEntry*> sortedByName(std::vector<const ManifestEntry*> entries,
                                               bool descending) {
    std::sort(entries.begin(), entries.end(),
              [descending](const ManifestEntry* a, const ManifestEntry* b) {
                  if (descending) {
                      return a->name > b->name;
                  }
                  return a->name < b->name;
              });
    return entries;
}

}  // namespace

std::string layoutSegmentKindName(LayoutSegmentKind kind) {
    switch (kind) {
    case LayoutSegmentKind::kHeader:
        return "header";
    case LayoutSegmentKind::kIndex:
        return "index";
    case LayoutSegmentKind::kChunkHeader:
        return "chunk-header";
    case LayoutSegmentKind::kChunkPayload:
        return "chunk-payload";
    case LayoutSegmentKind::kExtension:
        return "extension";
    case LayoutSegmentKind::kGap:
        return "gap";
    case LayoutSegmentKind::kTrailing:
        return "trailing";
    }
    return "unknown";
}

std::vector<SizeBucket> defaultSizeBuckets() {
    return {
        {"empty", 0, 1, 0, 0, 0},
        {"1 B..1 KiB", 1, 1024, 0, 0, 0},
        {"1 KiB..64 KiB", 1024, 64ull * 1024ull, 0, 0, 0},
        {"64 KiB..1 MiB", 64ull * 1024ull, 1024ull * 1024ull, 0, 0, 0},
        {"1 MiB..16 MiB", 1024ull * 1024ull, 16ull * 1024ull * 1024ull, 0, 0, 0},
        {"16 MiB+", 16ull * 1024ull * 1024ull, 0, 0, 0, 0},
    };
}

ArchiveAnalysis analyzeArchive(const ArchiveManifest& manifest) {
    ArchiveAnalysis analysis;
    analysis.stats = computeArchiveStats(manifest);
    analysis.compression = analyzeCompression(manifest);
    analysis.paths = analyzePaths(manifest);
    analysis.packed_buckets = bucketEntriesByPackedSize(manifest);
    analysis.original_buckets = bucketEntriesByOriginalSize(manifest);
    analysis.extensions = summarizeExtensions(manifest);
    analysis.depths = summarizeDepths(manifest);
    analysis.layout = buildLayoutSegments(manifest);
    return analysis;
}

CompressionSummary analyzeCompression(const ArchiveManifest& manifest) {
    CompressionSummary summary;
    bool saw_ratio = false;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        if (entry->compressed) {
            ++summary.compressed_entries;
            summary.compressed_packed_total += entry->packed_size;
            summary.compressed_original_total += entry->original_size;
            const double ratio = compressionRatio(*entry);
            if (!saw_ratio || ratio > summary.best_ratio) {
                summary.best_ratio = ratio;
                summary.best_entry = entry->name;
            }
            if (!saw_ratio || ratio < summary.worst_ratio) {
                summary.worst_ratio = ratio;
                summary.worst_entry = entry->name;
            }
            saw_ratio = true;
        } else {
            ++summary.uncompressed_entries;
            summary.uncompressed_total += entry->original_size;
        }
    }
    return summary;
}

PathSummary analyzePaths(const ArchiveManifest& manifest) {
    PathSummary summary;
    std::map<std::string, std::uint64_t> counts;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        switch (classifyArchiveName(entry->name)) {
        case NameClass::kEmpty:
            break;
        case NameClass::kSimple:
            ++summary.simple_names;
            break;
        case NameClass::kDirectory:
            ++summary.directory_names;
            break;
        case NameClass::kAbsolute:
            ++summary.absolute_names;
            break;
        case NameClass::kRelativeWithDots:
            ++summary.dot_names;
            break;
        case NameClass::kDevice:
            ++summary.device_names;
            break;
        case NameClass::kControl:
            ++summary.control_names;
            break;
        case NameClass::kLong:
            ++summary.long_names;
            break;
        }
        if (entry->name_changed) {
            ++summary.normalized_changes;
        }
        std::string key = canonicalCollisionKey(entry->normalized_name);
        ++counts[key];
    }
    for (const auto& item : counts) {
        if (item.second > 1) {
            summary.duplicate_normalized += item.second - 1;
        }
    }
    return summary;
}

std::vector<SizeBucket> bucketEntriesByPackedSize(const ArchiveManifest& manifest) {
    std::vector<SizeBucket> buckets = defaultSizeBuckets();
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        SizeBucket* bucket = findBucket(&buckets, entry->packed_size);
        if (bucket) {
            addToBucket(bucket, *entry);
        }
    }
    return buckets;
}

std::vector<SizeBucket> bucketEntriesByOriginalSize(const ArchiveManifest& manifest) {
    std::vector<SizeBucket> buckets = defaultSizeBuckets();
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        SizeBucket* bucket = findBucket(&buckets, entry->original_size);
        if (bucket) {
            addToBucket(bucket, *entry);
        }
    }
    return buckets;
}

std::vector<ExtensionSummary> summarizeExtensions(const ArchiveManifest& manifest) {
    std::vector<ExtensionSummary> summaries;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        std::string ext = lowerAscii(extensionOf(entry->name));
        if (ext.empty()) {
            ext = "(none)";
        }
        ExtensionSummary* summary = findExtension(&summaries, ext);
        ++summary->count;
        summary->packed_total += entry->packed_size;
        summary->original_total += entry->original_size;
        if (entry->compressed) {
            ++summary->compressed_count;
        }
        if (entry->nested) {
            ++summary->nested_count;
        }
    }
    std::sort(summaries.begin(), summaries.end(),
              [](const ExtensionSummary& a, const ExtensionSummary& b) {
                  if (a.count != b.count) {
                      return a.count > b.count;
                  }
                  return a.extension < b.extension;
              });
    return summaries;
}

std::vector<DepthSummary> summarizeDepths(const ArchiveManifest& manifest) {
    std::vector<DepthSummary> summaries;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        DepthSummary* summary = findDepth(&summaries, entry->depth);
        ++summary->count;
        summary->packed_total += entry->packed_size;
        summary->original_total += entry->original_size;
    }
    std::sort(summaries.begin(), summaries.end(),
              [](const DepthSummary& a, const DepthSummary& b) {
                  return a.depth < b.depth;
              });
    return summaries;
}

std::vector<LayoutSegment> buildLayoutSegments(const ArchiveManifest& manifest) {
    std::vector<LayoutSegment> segments;
    LayoutSegment header;
    header.kind = LayoutSegmentKind::kHeader;
    header.offset = 0;
    header.length = manifest.index_offset;
    header.label = "archive header";
    segments.push_back(header);

    LayoutSegment index;
    index.kind = LayoutSegmentKind::kIndex;
    index.offset = manifest.index_offset;
    index.length = manifest.chunk_stream_offset > manifest.index_offset
                       ? manifest.chunk_stream_offset - manifest.index_offset
                       : 0;
    index.label = "index table";
    segments.push_back(index);

    for (const ManifestEntry& entry : manifest.entries) {
        appendEntryLayout(entry, &segments);
    }
    if (manifest.extension_size != 0) {
        LayoutSegment extension;
        extension.kind = LayoutSegmentKind::kExtension;
        extension.offset = manifest.extension_offset;
        extension.length = manifest.extension_size;
        extension.label = "extension block";
        segments.push_back(extension);
    }
    sortAndAddGaps(&segments, manifest.archive_size);
    return segments;
}

std::vector<const ManifestEntry*> sortedEntries(const ArchiveManifest& manifest,
                                                EntrySortKey key,
                                                bool descending) {
    std::vector<const ManifestEntry*> entries = flattenEntries(manifest);
    switch (key) {
    case EntrySortKey::kName:
        return sortedByName(std::move(entries), descending);
    case EntrySortKey::kPackedSize:
        std::sort(entries.begin(), entries.end(),
                  [descending](const ManifestEntry* a, const ManifestEntry* b) {
                      return descending ? a->packed_size > b->packed_size
                                        : a->packed_size < b->packed_size;
                  });
        break;
    case EntrySortKey::kOriginalSize:
        std::sort(entries.begin(), entries.end(),
                  [descending](const ManifestEntry* a, const ManifestEntry* b) {
                      return descending ? a->original_size > b->original_size
                                        : a->original_size < b->original_size;
                  });
        break;
    case EntrySortKey::kCompressionRatio:
        std::sort(entries.begin(), entries.end(),
                  [descending](const ManifestEntry* a, const ManifestEntry* b) {
                      return descending ? compressionRatio(*a) > compressionRatio(*b)
                                        : compressionRatio(*a) < compressionRatio(*b);
                  });
        break;
    case EntrySortKey::kOffset:
        std::sort(entries.begin(), entries.end(),
                  [descending](const ManifestEntry* a, const ManifestEntry* b) {
                      return descending ? a->chunk.absolute_offset > b->chunk.absolute_offset
                                        : a->chunk.absolute_offset < b->chunk.absolute_offset;
                  });
        break;
    case EntrySortKey::kDepth:
        std::sort(entries.begin(), entries.end(),
                  [descending](const ManifestEntry* a, const ManifestEntry* b) {
                      return descending ? a->depth > b->depth : a->depth < b->depth;
                  });
        break;
    }
    return entries;
}

std::vector<const ManifestEntry*> largestEntries(const ArchiveManifest& manifest,
                                                 std::size_t limit) {
    std::vector<const ManifestEntry*> entries =
        sortedEntries(manifest, EntrySortKey::kOriginalSize, true);
    if (entries.size() > limit) {
        entries.resize(limit);
    }
    return entries;
}

std::vector<const ManifestEntry*> mostCompressedEntries(const ArchiveManifest& manifest,
                                                        std::size_t limit) {
    std::vector<const ManifestEntry*> entries =
        sortedEntries(manifest, EntrySortKey::kCompressionRatio, true);
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const ManifestEntry* entry) {
                                     return !entry->compressed;
                                 }),
                  entries.end());
    if (entries.size() > limit) {
        entries.resize(limit);
    }
    return entries;
}

std::vector<const ManifestEntry*> entriesAtDepth(const ArchiveManifest& manifest,
                                                 int depth) {
    std::vector<const ManifestEntry*> out;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        if (entry->depth == depth) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<const ManifestEntry*> entriesWithExtension(const ArchiveManifest& manifest,
                                                       const std::string& extension) {
    std::vector<const ManifestEntry*> out;
    const std::string wanted = lowerAscii(extension);
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        if (lowerAscii(extensionOf(entry->name)) == wanted) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<const ManifestEntry*> entriesMatchingName(const ArchiveManifest& manifest,
                                                      const std::string& needle) {
    std::vector<const ManifestEntry*> out;
    const std::string lower_needle = lowerAscii(needle);
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        if (lowerAscii(entry->name).find(lower_needle) != std::string::npos) {
            out.push_back(entry);
        }
    }
    return out;
}

std::map<std::string, std::uint64_t> normalizedNameCounts(const ArchiveManifest& manifest) {
    std::map<std::string, std::uint64_t> counts;
    for (const ManifestEntry* entry : flattenEntries(manifest)) {
        ++counts[canonicalCollisionKey(entry->normalized_name)];
    }
    return counts;
}

double compressionRatio(const ManifestEntry& entry) {
    if (entry.packed_size == 0) {
        return entry.original_size == 0 ? 1.0 : 0.0;
    }
    return static_cast<double>(entry.original_size) /
           static_cast<double>(entry.packed_size);
}

double packedGrowthRatio(const ManifestEntry& entry) {
    if (entry.original_size == 0) {
        return entry.packed_size == 0 ? 1.0 : static_cast<double>(entry.packed_size);
    }
    return static_cast<double>(entry.packed_size) /
           static_cast<double>(entry.original_size);
}

std::uint64_t segmentEnd(const LayoutSegment& segment) {
    return segment.offset + segment.length;
}

std::uint64_t totalSegmentBytes(const std::vector<LayoutSegment>& segments,
                                LayoutSegmentKind kind) {
    std::uint64_t total = 0;
    for (const LayoutSegment& segment : segments) {
        if (segment.kind == kind) {
            total += segment.length;
        }
    }
    return total;
}

std::string describeSizeBucket(const SizeBucket& bucket) {
    std::ostringstream out;
    out << bucket.label << ": " << pluralize(bucket.count, "entry")
        << ", packed " << humanSize(bucket.packed_total)
        << ", original " << humanSize(bucket.original_total);
    return out.str();
}

std::string describeExtensionSummary(const ExtensionSummary& summary) {
    std::ostringstream out;
    out << summary.extension << ": " << pluralize(summary.count, "entry")
        << ", packed " << humanSize(summary.packed_total)
        << ", original " << humanSize(summary.original_total)
        << ", compressed " << summary.compressed_count
        << ", nested " << summary.nested_count;
    return out.str();
}

std::string describeDepthSummary(const DepthSummary& summary) {
    std::ostringstream out;
    out << "depth " << summary.depth << ": "
        << pluralize(summary.count, "entry")
        << ", packed " << humanSize(summary.packed_total)
        << ", original " << humanSize(summary.original_total);
    return out.str();
}

std::string describeCompressionSummary(const CompressionSummary& summary) {
    std::ostringstream out;
    out << pluralize(summary.compressed_entries, "compressed entry")
        << ", " << pluralize(summary.uncompressed_entries, "uncompressed entry")
        << ", compressed packed " << humanSize(summary.compressed_packed_total)
        << ", compressed original " << humanSize(summary.compressed_original_total);
    if (!summary.best_entry.empty()) {
        out << ", best " << summary.best_entry << " "
            << ratioString(static_cast<std::uint64_t>(summary.best_ratio * 1000), 1000);
    }
    if (!summary.worst_entry.empty()) {
        out << ", worst " << summary.worst_entry << " "
            << ratioString(static_cast<std::uint64_t>(summary.worst_ratio * 1000), 1000);
    }
    return out.str();
}

std::string describePathSummary(const PathSummary& summary) {
    std::ostringstream out;
    out << "simple " << summary.simple_names
        << ", directory " << summary.directory_names
        << ", absolute " << summary.absolute_names
        << ", dot " << summary.dot_names
        << ", device " << summary.device_names
        << ", control " << summary.control_names
        << ", long " << summary.long_names
        << ", normalized changes " << summary.normalized_changes
        << ", duplicate normalized " << summary.duplicate_normalized;
    return out.str();
}

std::string describeLayoutSegment(const LayoutSegment& segment) {
    std::ostringstream out;
    out << layoutSegmentKindName(segment.kind) << " @ " << segment.offset
        << ".." << segmentEnd(segment)
        << " (" << humanSize(segment.length) << ")";
    if (!segment.entry_name.empty()) {
        out << " " << segment.entry_name;
    }
    if (segment.depth != 0) {
        out << " depth=" << segment.depth;
    }
    if (!segment.label.empty()) {
        out << " " << segment.label;
    }
    return out.str();
}

std::string formatAnalysisText(const ArchiveAnalysis& analysis) {
    LineBuilder lines;
    lines.appendLine("archive analysis");
    lines.increaseIndent(2);
    lines.appendLine(describeArchiveStats(analysis.stats));
    lines.appendLine(describeCompressionSummary(analysis.compression));
    lines.appendLine(describePathSummary(analysis.paths));
    lines.decreaseIndent(2);
    lines.blankLine();
    lines.appendLine("original size buckets:");
    lines.increaseIndent(2);
    for (const SizeBucket& bucket : analysis.original_buckets) {
        lines.appendLine(describeSizeBucket(bucket));
    }
    lines.decreaseIndent(2);
    lines.blankLine();
    lines.appendLine("extensions:");
    lines.increaseIndent(2);
    for (const ExtensionSummary& extension : analysis.extensions) {
        lines.appendLine(describeExtensionSummary(extension));
    }
    lines.decreaseIndent(2);
    lines.blankLine();
    lines.appendLine("layout:");
    lines.increaseIndent(2);
    for (const LayoutSegment& segment : analysis.layout) {
        lines.appendLine(describeLayoutSegment(segment));
    }
    return lines.str();
}

std::string formatAnalysisJson(const ArchiveAnalysis& analysis) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"stats\": {\"entries\":" << analysis.stats.entry_count
        << ",\"nested\":" << analysis.stats.nested_archive_count
        << ",\"compressed\":" << analysis.stats.compressed_entry_count
        << ",\"packed\":" << analysis.stats.total_packed_size
        << ",\"original\":" << analysis.stats.total_original_size
        << ",\"maxDepth\":" << analysis.stats.max_depth << "},\n";
    out << "  \"paths\": {\"simple\":" << analysis.paths.simple_names
        << ",\"directory\":" << analysis.paths.directory_names
        << ",\"absolute\":" << analysis.paths.absolute_names
        << ",\"dot\":" << analysis.paths.dot_names
        << ",\"device\":" << analysis.paths.device_names
        << ",\"control\":" << analysis.paths.control_names
        << ",\"long\":" << analysis.paths.long_names
        << ",\"changed\":" << analysis.paths.normalized_changes
        << ",\"duplicates\":" << analysis.paths.duplicate_normalized << "},\n";
    out << "  \"extensions\": [";
    for (std::size_t i = 0; i < analysis.extensions.size(); ++i) {
        const ExtensionSummary& item = analysis.extensions[i];
        if (i != 0) {
            out << ",";
        }
        out << "{\"extension\":\"" << jsonEscape(item.extension)
            << "\",\"count\":" << item.count
            << ",\"packed\":" << item.packed_total
            << ",\"original\":" << item.original_total << "}";
    }
    out << "],\n  \"layout\": [";
    for (std::size_t i = 0; i < analysis.layout.size(); ++i) {
        const LayoutSegment& item = analysis.layout[i];
        if (i != 0) {
            out << ",";
        }
        out << "{\"kind\":\"" << layoutSegmentKindName(item.kind)
            << "\",\"offset\":" << item.offset
            << ",\"length\":" << item.length
            << ",\"entry\":\"" << jsonEscape(item.entry_name) << "\"}";
    }
    out << "]\n}\n";
    return out.str();
}

}  // namespace vector
