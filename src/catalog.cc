#include "catalog.h"

#include <algorithm>
#include <sstream>

#include "byte_io.h"
#include "diagnostics.h"
#include "path_utils.h"

namespace vector {
namespace {

void addCatalogEntry(const ManifestEntry& entry,
                     std::vector<CatalogEntry>* catalog) {
    catalog->push_back(makeCatalogEntry(entry));
    for (const ManifestEntry& child : entry.children) {
        addCatalogEntry(child, catalog);
    }
}

CatalogGroup* findGroup(std::vector<CatalogGroup>* groups,
                        const std::string& key) {
    for (CatalogGroup& group : *groups) {
        if (group.key == key) {
            return &group;
        }
    }
    CatalogGroup group;
    group.key = key;
    groups->push_back(group);
    return &groups->back();
}

std::string compareKey(const CatalogEntry& entry) {
    if (entry.entry == nullptr) {
        return entry.path;
    }
    return canonicalCollisionKey(entry.entry->normalized_name);
}

void addDiff(CatalogDiff* diff, CatalogDiffEntry entry) {
    if (entry.only_left) {
        ++diff->removed;
    } else if (entry.only_right) {
        ++diff->added;
    } else if (entry.size_changed || entry.flags_changed) {
        ++diff->changed;
    }
    diff->entries.push_back(std::move(entry));
}

}  // namespace

std::vector<CatalogEntry> buildCatalog(const ArchiveManifest& manifest) {
    std::vector<CatalogEntry> catalog;
    for (const ManifestEntry& entry : manifest.entries) {
        addCatalogEntry(entry, &catalog);
    }
    std::sort(catalog.begin(), catalog.end(),
              [](const CatalogEntry& a, const CatalogEntry& b) {
                  if (a.path != b.path) {
                      return a.path < b.path;
                  }
                  const int left_depth = a.entry ? a.entry->depth : 0;
                  const int right_depth = b.entry ? b.entry->depth : 0;
                  return left_depth < right_depth;
              });
    return catalog;
}

CatalogEntry makeCatalogEntry(const ManifestEntry& entry) {
    CatalogEntry item;
    item.entry = &entry;
    item.path = entry.name;
    item.directory = dirnameOf(entry.name);
    item.basename = basenameOf(entry.name);
    item.extension = lowerAscii(extensionOf(entry.name));
    if (item.extension.empty()) {
        item.extension = "(none)";
    }
    item.size_class = classifySize(entry.original_size);
    item.compression_ratio = compressionRatio(entry);
    return item;
}

std::vector<CatalogEntry> findCatalogEntries(const std::vector<CatalogEntry>& catalog,
                                             const std::string& pattern,
                                             CatalogMatchMode mode) {
    std::vector<CatalogEntry> out;
    for (const CatalogEntry& entry : catalog) {
        if (catalogEntryMatches(entry, pattern, mode)) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<CatalogEntry> filterCatalogByDepth(const std::vector<CatalogEntry>& catalog,
                                               int min_depth,
                                               int max_depth) {
    std::vector<CatalogEntry> out;
    for (const CatalogEntry& entry : catalog) {
        const int depth = entry.entry ? entry.entry->depth : 0;
        if (depth >= min_depth && depth <= max_depth) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<CatalogEntry> filterCatalogBySize(const std::vector<CatalogEntry>& catalog,
                                              std::uint64_t min_original,
                                              std::uint64_t max_original) {
    std::vector<CatalogEntry> out;
    for (const CatalogEntry& entry : catalog) {
        const std::uint64_t size = entry.entry ? entry.entry->original_size : 0;
        const bool below_max = max_original == 0 || size <= max_original;
        if (size >= min_original && below_max) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<CatalogEntry> filterCatalogByFlags(const std::vector<CatalogEntry>& catalog,
                                               bool require_compressed,
                                               bool require_nested) {
    std::vector<CatalogEntry> out;
    for (const CatalogEntry& entry : catalog) {
        if (catalogEntryHasFlags(entry, require_compressed, require_nested)) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<CatalogGroup> groupCatalog(const std::vector<CatalogEntry>& catalog,
                                       CatalogGroupKey key) {
    std::vector<CatalogGroup> groups;
    for (const CatalogEntry& entry : catalog) {
        CatalogGroup* group = findGroup(&groups, groupKeyForEntry(entry, key));
        group->entries.push_back(entry);
        if (entry.entry != nullptr) {
            group->packed_total += entry.entry->packed_size;
            group->original_total += entry.entry->original_size;
        }
    }
    std::sort(groups.begin(), groups.end(),
              [](const CatalogGroup& a, const CatalogGroup& b) {
                  if (a.entries.size() != b.entries.size()) {
                      return a.entries.size() > b.entries.size();
                  }
                  return a.key < b.key;
              });
    return groups;
}

std::map<std::string, CatalogEntry> catalogByNormalizedName(
    const std::vector<CatalogEntry>& catalog) {
    std::map<std::string, CatalogEntry> out;
    for (const CatalogEntry& entry : catalog) {
        out[compareKey(entry)] = entry;
    }
    return out;
}

CatalogDiff diffCatalogs(const ArchiveManifest& left,
                         const ArchiveManifest& right) {
    std::map<std::string, CatalogEntry> left_entries =
        catalogByNormalizedName(buildCatalog(left));
    std::map<std::string, CatalogEntry> right_entries =
        catalogByNormalizedName(buildCatalog(right));
    CatalogDiff diff;
    for (const auto& item : left_entries) {
        auto found = right_entries.find(item.first);
        if (found == right_entries.end()) {
            CatalogDiffEntry entry;
            entry.name = item.second.path;
            entry.only_left = true;
            entry.left_size = item.second.entry ? item.second.entry->original_size : 0;
            addDiff(&diff, std::move(entry));
            continue;
        }
        const ManifestEntry* left_entry = item.second.entry;
        const ManifestEntry* right_entry = found->second.entry;
        CatalogDiffEntry entry;
        entry.name = item.second.path;
        entry.left_size = left_entry ? left_entry->original_size : 0;
        entry.right_size = right_entry ? right_entry->original_size : 0;
        entry.size_changed = entry.left_size != entry.right_size;
        entry.flags_changed = left_entry && right_entry &&
                              left_entry->flags != right_entry->flags;
        if (entry.size_changed || entry.flags_changed) {
            addDiff(&diff, std::move(entry));
        }
    }
    for (const auto& item : right_entries) {
        if (left_entries.find(item.first) == left_entries.end()) {
            CatalogDiffEntry entry;
            entry.name = item.second.path;
            entry.only_right = true;
            entry.right_size = item.second.entry ? item.second.entry->original_size : 0;
            addDiff(&diff, std::move(entry));
        }
    }
    std::sort(diff.entries.begin(), diff.entries.end(),
              [](const CatalogDiffEntry& a, const CatalogDiffEntry& b) {
                  return a.name < b.name;
              });
    return diff;
}

std::string catalogGroupKeyName(CatalogGroupKey key) {
    switch (key) {
    case CatalogGroupKey::kDirectory:
        return "directory";
    case CatalogGroupKey::kExtension:
        return "extension";
    case CatalogGroupKey::kDepth:
        return "depth";
    case CatalogGroupKey::kSizeClass:
        return "size-class";
    case CatalogGroupKey::kFlagSet:
        return "flags";
    }
    return "unknown";
}

std::string catalogMatchModeName(CatalogMatchMode mode) {
    switch (mode) {
    case CatalogMatchMode::kExact:
        return "exact";
    case CatalogMatchMode::kContains:
        return "contains";
    case CatalogMatchMode::kPrefix:
        return "prefix";
    case CatalogMatchMode::kSuffix:
        return "suffix";
    case CatalogMatchMode::kCaseInsensitiveContains:
        return "case-insensitive-contains";
    }
    return "unknown";
}

std::string catalogEntryDisplayName(const CatalogEntry& entry) {
    if (entry.entry == nullptr) {
        return entry.path;
    }
    if (entry.entry->name_changed) {
        return entry.entry->name + " -> " + entry.entry->normalized_name;
    }
    return entry.entry->name;
}

std::string catalogEntrySummary(const CatalogEntry& entry) {
    std::ostringstream out;
    out << catalogEntryDisplayName(entry);
    if (entry.entry != nullptr) {
        out << " depth=" << entry.entry->depth
            << " original=" << humanSize(entry.entry->original_size)
            << " packed=" << humanSize(entry.entry->packed_size)
            << " class=" << sizeClassName(entry.size_class)
            << " ratio=" << ratioString(entry.entry->original_size,
                                         entry.entry->packed_size);
        if (entry.entry->compressed) {
            out << " compressed";
        }
        if (entry.entry->nested) {
            out << " nested";
        }
    }
    return out.str();
}

std::string catalogGroupSummary(const CatalogGroup& group) {
    std::ostringstream out;
    out << group.key << ": " << pluralize(group.entries.size(), "entry")
        << ", packed " << humanSize(group.packed_total)
        << ", original " << humanSize(group.original_total);
    return out.str();
}

std::string catalogDiffSummary(const CatalogDiff& diff) {
    std::ostringstream out;
    out << "added " << diff.added
        << ", removed " << diff.removed
        << ", changed " << diff.changed
        << ", total differences " << diff.entries.size();
    return out.str();
}

std::string formatCatalogText(const std::vector<CatalogEntry>& catalog) {
    LineBuilder lines;
    lines.appendLine("catalog:");
    lines.increaseIndent(2);
    for (const CatalogEntry& entry : catalog) {
        lines.appendLine(catalogEntrySummary(entry));
    }
    if (catalog.empty()) {
        lines.appendLine("(empty)");
    }
    return lines.str();
}

std::string formatCatalogGroupsText(const std::vector<CatalogGroup>& groups) {
    LineBuilder lines;
    lines.appendLine("catalog groups:");
    lines.increaseIndent(2);
    for (const CatalogGroup& group : groups) {
        lines.appendLine(catalogGroupSummary(group));
    }
    if (groups.empty()) {
        lines.appendLine("(empty)");
    }
    return lines.str();
}

std::string formatCatalogDiffText(const CatalogDiff& diff) {
    LineBuilder lines;
    lines.appendLine("catalog diff: " + catalogDiffSummary(diff));
    lines.increaseIndent(2);
    for (const CatalogDiffEntry& entry : diff.entries) {
        std::ostringstream row;
        row << entry.name;
        if (entry.only_left) {
            row << " removed size=" << humanSize(entry.left_size);
        } else if (entry.only_right) {
            row << " added size=" << humanSize(entry.right_size);
        } else {
            row << " changed";
            if (entry.size_changed) {
                row << " size " << humanSize(entry.left_size)
                    << " -> " << humanSize(entry.right_size);
            }
            if (entry.flags_changed) {
                row << " flags";
            }
        }
        lines.appendLine(row.str());
    }
    return lines.str();
}

std::string formatCatalogJson(const std::vector<CatalogEntry>& catalog) {
    std::ostringstream out;
    out << "{\n  \"entries\": [";
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const CatalogEntry& entry = catalog[i];
        if (i != 0) {
            out << ",";
        }
        out << "\n    {\"name\":\"" << jsonEscape(entry.path)
            << "\",\"directory\":\"" << jsonEscape(entry.directory)
            << "\",\"basename\":\"" << jsonEscape(entry.basename)
            << "\",\"extension\":\"" << jsonEscape(entry.extension)
            << "\",\"sizeClass\":\"" << sizeClassName(entry.size_class)
            << "\"";
        if (entry.entry != nullptr) {
            out << ",\"depth\":" << entry.entry->depth
                << ",\"packed\":" << entry.entry->packed_size
                << ",\"original\":" << entry.entry->original_size
                << ",\"compressed\":" << (entry.entry->compressed ? "true" : "false")
                << ",\"nested\":" << (entry.entry->nested ? "true" : "false");
        }
        out << "}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

bool catalogEntryMatches(const CatalogEntry& entry,
                         const std::string& pattern,
                         CatalogMatchMode mode) {
    switch (mode) {
    case CatalogMatchMode::kExact:
        return entry.path == pattern || entry.basename == pattern;
    case CatalogMatchMode::kContains:
        return entry.path.find(pattern) != std::string::npos;
    case CatalogMatchMode::kPrefix:
        return startsWith(entry.path, pattern);
    case CatalogMatchMode::kSuffix:
        return endsWith(entry.path, pattern);
    case CatalogMatchMode::kCaseInsensitiveContains:
        return lowerAscii(entry.path).find(lowerAscii(pattern)) != std::string::npos;
    }
    return false;
}

bool catalogEntryHasFlags(const CatalogEntry& entry,
                          bool require_compressed,
                          bool require_nested) {
    if (entry.entry == nullptr) {
        return false;
    }
    if (require_compressed && !entry.entry->compressed) {
        return false;
    }
    if (require_nested && !entry.entry->nested) {
        return false;
    }
    return true;
}

std::string groupKeyForEntry(const CatalogEntry& entry, CatalogGroupKey key) {
    switch (key) {
    case CatalogGroupKey::kDirectory:
        return entry.directory.empty() ? "(root)" : entry.directory;
    case CatalogGroupKey::kExtension:
        return entry.extension;
    case CatalogGroupKey::kDepth:
        return "depth-" + std::to_string(entry.entry ? entry.entry->depth : 0);
    case CatalogGroupKey::kSizeClass:
        return sizeClassName(entry.size_class);
    case CatalogGroupKey::kFlagSet:
        if (entry.entry == nullptr) {
            return "none";
        }
        if (entry.entry->compressed && entry.entry->nested) {
            return "compressed,nested";
        }
        if (entry.entry->compressed) {
            return "compressed";
        }
        if (entry.entry->nested) {
            return "nested";
        }
        return "none";
    }
    return "unknown";
}

}  // namespace vector
