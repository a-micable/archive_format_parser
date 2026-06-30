#ifndef VECTOR_ARCHIVE_ARCHIVE_STATS_H_
#define VECTOR_ARCHIVE_ARCHIVE_STATS_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "format_rules.h"
#include "manifest.h"

namespace vector {

enum class LayoutSegmentKind {
    kHeader,
    kIndex,
    kChunkHeader,
    kChunkPayload,
    kExtension,
    kGap,
    kTrailing,
};

enum class EntrySortKey {
    kName,
    kPackedSize,
    kOriginalSize,
    kCompressionRatio,
    kOffset,
    kDepth,
};

struct SizeBucket {
    std::string label;
    std::uint64_t min_inclusive = 0;
    std::uint64_t max_exclusive = 0;
    std::uint64_t count = 0;
    std::uint64_t packed_total = 0;
    std::uint64_t original_total = 0;
};

struct ExtensionSummary {
    std::string extension;
    std::uint64_t count = 0;
    std::uint64_t packed_total = 0;
    std::uint64_t original_total = 0;
    std::uint64_t compressed_count = 0;
    std::uint64_t nested_count = 0;
};

struct DepthSummary {
    int depth = 0;
    std::uint64_t count = 0;
    std::uint64_t packed_total = 0;
    std::uint64_t original_total = 0;
};

struct LayoutSegment {
    LayoutSegmentKind kind = LayoutSegmentKind::kGap;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::string label;
    std::string entry_name;
    int depth = 0;
};

struct CompressionSummary {
    std::uint64_t compressed_entries = 0;
    std::uint64_t uncompressed_entries = 0;
    std::uint64_t compressed_packed_total = 0;
    std::uint64_t compressed_original_total = 0;
    std::uint64_t uncompressed_total = 0;
    double best_ratio = 0.0;
    double worst_ratio = 0.0;
    std::string best_entry;
    std::string worst_entry;
};

struct PathSummary {
    std::uint64_t simple_names = 0;
    std::uint64_t directory_names = 0;
    std::uint64_t absolute_names = 0;
    std::uint64_t dot_names = 0;
    std::uint64_t device_names = 0;
    std::uint64_t control_names = 0;
    std::uint64_t long_names = 0;
    std::uint64_t normalized_changes = 0;
    std::uint64_t duplicate_normalized = 0;
};

struct ArchiveAnalysis {
    ArchiveStats stats;
    CompressionSummary compression;
    PathSummary paths;
    std::vector<SizeBucket> packed_buckets;
    std::vector<SizeBucket> original_buckets;
    std::vector<ExtensionSummary> extensions;
    std::vector<DepthSummary> depths;
    std::vector<LayoutSegment> layout;
};

std::string layoutSegmentKindName(LayoutSegmentKind kind);
std::vector<SizeBucket> defaultSizeBuckets();
ArchiveAnalysis analyzeArchive(const ArchiveManifest& manifest);
CompressionSummary analyzeCompression(const ArchiveManifest& manifest);
PathSummary analyzePaths(const ArchiveManifest& manifest);
std::vector<SizeBucket> bucketEntriesByPackedSize(const ArchiveManifest& manifest);
std::vector<SizeBucket> bucketEntriesByOriginalSize(const ArchiveManifest& manifest);
std::vector<ExtensionSummary> summarizeExtensions(const ArchiveManifest& manifest);
std::vector<DepthSummary> summarizeDepths(const ArchiveManifest& manifest);
std::vector<LayoutSegment> buildLayoutSegments(const ArchiveManifest& manifest);
std::vector<const ManifestEntry*> sortedEntries(const ArchiveManifest& manifest,
                                                EntrySortKey key,
                                                bool descending = false);
std::vector<const ManifestEntry*> largestEntries(const ArchiveManifest& manifest,
                                                 std::size_t limit);
std::vector<const ManifestEntry*> mostCompressedEntries(const ArchiveManifest& manifest,
                                                        std::size_t limit);
std::vector<const ManifestEntry*> entriesAtDepth(const ArchiveManifest& manifest,
                                                 int depth);
std::vector<const ManifestEntry*> entriesWithExtension(const ArchiveManifest& manifest,
                                                       const std::string& extension);
std::vector<const ManifestEntry*> entriesMatchingName(const ArchiveManifest& manifest,
                                                      const std::string& needle);
std::map<std::string, std::uint64_t> normalizedNameCounts(const ArchiveManifest& manifest);

double compressionRatio(const ManifestEntry& entry);
double packedGrowthRatio(const ManifestEntry& entry);
std::uint64_t segmentEnd(const LayoutSegment& segment);
std::uint64_t totalSegmentBytes(const std::vector<LayoutSegment>& segments,
                                LayoutSegmentKind kind);
std::string describeSizeBucket(const SizeBucket& bucket);
std::string describeExtensionSummary(const ExtensionSummary& summary);
std::string describeDepthSummary(const DepthSummary& summary);
std::string describeCompressionSummary(const CompressionSummary& summary);
std::string describePathSummary(const PathSummary& summary);
std::string describeLayoutSegment(const LayoutSegment& segment);
std::string formatAnalysisText(const ArchiveAnalysis& analysis);
std::string formatAnalysisJson(const ArchiveAnalysis& analysis);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_ARCHIVE_STATS_H_
