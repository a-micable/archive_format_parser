#ifndef VECTOR_ARCHIVE_PATH_REPORT_H_
#define VECTOR_ARCHIVE_PATH_REPORT_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "manifest.h"
#include "path_utils.h"

namespace vector {

struct ExtractionPlanEntry {
    std::string archive_name;
    std::string normalized_name;
    std::string assigned_name;
    bool valid = true;
    bool changed = false;
    bool collided = false;
    std::uint64_t original_size = 0;
    int depth = 0;
    Diagnostics diagnostics;
};

struct PathPolicySummary {
    std::uint64_t entries = 0;
    std::uint64_t valid = 0;
    std::uint64_t invalid = 0;
    std::uint64_t changed = 0;
    std::uint64_t collisions = 0;
    std::map<std::string, std::uint64_t> issue_counts;
};

struct ExtractionPlan {
    std::vector<ExtractionPlanEntry> entries;
    PathPolicySummary summary;
    Diagnostics diagnostics;
};

ExtractionPlan buildExtractionPlan(const ArchiveManifest& manifest,
                                   const PathPolicy& policy = defaultArchivePathPolicy());
ExtractionPlanEntry makeExtractionPlanEntry(const ManifestEntry& entry,
                                            CollisionResolver* resolver,
                                            const PathPolicy& policy);
PathPolicySummary summarizeExtractionPlan(const std::vector<ExtractionPlanEntry>& entries);
Diagnostics collectPathDiagnostics(const std::vector<ExtractionPlanEntry>& entries);
std::vector<ExtractionPlanEntry> invalidPlanEntries(const ExtractionPlan& plan);
std::vector<ExtractionPlanEntry> collidedPlanEntries(const ExtractionPlan& plan);
std::vector<ExtractionPlanEntry> changedPlanEntries(const ExtractionPlan& plan);

std::string pathPolicySummaryText(const PathPolicySummary& summary);
std::string extractionPlanEntryText(const ExtractionPlanEntry& entry);
std::string formatExtractionPlanText(const ExtractionPlan& plan);
std::string formatExtractionPlanJson(const ExtractionPlan& plan);
std::string formatPathIssueCounts(const std::map<std::string, std::uint64_t>& counts);

bool extractionPlanHasErrors(const ExtractionPlan& plan);
bool extractionPlanHasCollisions(const ExtractionPlan& plan);
bool assignedNameIsUnique(const ExtractionPlan& plan, const std::string& assigned_name);
std::map<std::string, std::vector<ExtractionPlanEntry>> entriesByAssignedDirectory(
    const ExtractionPlan& plan);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_PATH_REPORT_H_
