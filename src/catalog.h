#ifndef VECTOR_ARCHIVE_CATALOG_H_
#define VECTOR_ARCHIVE_CATALOG_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "archive_stats.h"
#include "manifest.h"

namespace vector {

enum class CatalogMatchMode {
    kExact,
    kContains,
    kPrefix,
    kSuffix,
    kCaseInsensitiveContains,
};

enum class CatalogGroupKey {
    kDirectory,
    kExtension,
    kDepth,
    kSizeClass,
    kFlagSet,
};

struct CatalogEntry {
    const ManifestEntry* entry = nullptr;
    std::string path;
    std::string directory;
    std::string basename;
    std::string extension;
    SizeClass size_class = SizeClass::kEmpty;
    double compression_ratio = 0.0;
};

struct CatalogGroup {
    std::string key;
    std::vector<CatalogEntry> entries;
    std::uint64_t packed_total = 0;
    std::uint64_t original_total = 0;
};

struct CatalogDiffEntry {
    std::string name;
    bool only_left = false;
    bool only_right = false;
    bool size_changed = false;
    bool flags_changed = false;
    std::uint64_t left_size = 0;
    std::uint64_t right_size = 0;
};

struct CatalogDiff {
    std::vector<CatalogDiffEntry> entries;
    std::uint64_t added = 0;
    std::uint64_t removed = 0;
    std::uint64_t changed = 0;
};

std::vector<CatalogEntry> buildCatalog(const ArchiveManifest& manifest);
CatalogEntry makeCatalogEntry(const ManifestEntry& entry);
std::vector<CatalogEntry> findCatalogEntries(const std::vector<CatalogEntry>& catalog,
                                             const std::string& pattern,
                                             CatalogMatchMode mode);
std::vector<CatalogEntry> filterCatalogByDepth(const std::vector<CatalogEntry>& catalog,
                                               int min_depth,
                                               int max_depth);
std::vector<CatalogEntry> filterCatalogBySize(const std::vector<CatalogEntry>& catalog,
                                              std::uint64_t min_original,
                                              std::uint64_t max_original);
std::vector<CatalogEntry> filterCatalogByFlags(const std::vector<CatalogEntry>& catalog,
                                               bool require_compressed,
                                               bool require_nested);
std::vector<CatalogGroup> groupCatalog(const std::vector<CatalogEntry>& catalog,
                                       CatalogGroupKey key);
std::map<std::string, CatalogEntry> catalogByNormalizedName(
    const std::vector<CatalogEntry>& catalog);
CatalogDiff diffCatalogs(const ArchiveManifest& left,
                         const ArchiveManifest& right);

std::string catalogGroupKeyName(CatalogGroupKey key);
std::string catalogMatchModeName(CatalogMatchMode mode);
std::string catalogEntryDisplayName(const CatalogEntry& entry);
std::string catalogEntrySummary(const CatalogEntry& entry);
std::string catalogGroupSummary(const CatalogGroup& group);
std::string catalogDiffSummary(const CatalogDiff& diff);
std::string formatCatalogText(const std::vector<CatalogEntry>& catalog);
std::string formatCatalogGroupsText(const std::vector<CatalogGroup>& groups);
std::string formatCatalogDiffText(const CatalogDiff& diff);
std::string formatCatalogJson(const std::vector<CatalogEntry>& catalog);

bool catalogEntryMatches(const CatalogEntry& entry,
                         const std::string& pattern,
                         CatalogMatchMode mode);
bool catalogEntryHasFlags(const CatalogEntry& entry,
                          bool require_compressed,
                          bool require_nested);
std::string groupKeyForEntry(const CatalogEntry& entry, CatalogGroupKey key);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_CATALOG_H_
