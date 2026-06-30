#ifndef VECTOR_ARCHIVE_REPORT_H_
#define VECTOR_ARCHIVE_REPORT_H_

#include <cstddef>
#include <string>
#include <vector>

#include "archive_stats.h"
#include "validator.h"

namespace vector {

enum class ReportSection {
    kOverview,
    kEntries,
    kDiagnostics,
    kAnalysis,
    kLayout,
    kRules,
};

struct ReportOptions {
    bool include_overview = true;
    bool include_entries = true;
    bool include_diagnostics = true;
    bool include_analysis = true;
    bool include_layout = true;
    bool include_rules = false;
    bool include_json = false;
    bool compact = false;
    std::size_t entry_limit = 0;
    std::size_t layout_limit = 0;
};

struct ReportSectionText {
    ReportSection section = ReportSection::kOverview;
    std::string title;
    std::string body;
};

struct ArchiveReport {
    ArchiveManifest manifest;
    ValidationResult validation;
    ArchiveAnalysis analysis;
    std::vector<ReportSectionText> sections;
    bool ok = false;
};

std::string reportSectionName(ReportSection section);
ArchiveReport buildArchiveReport(const ArchiveManifest& manifest,
                                  const ValidationResult& validation,
                                  const ReportOptions& options = {});
ArchiveReport buildArchiveReport(const ValidationResult& validation,
                                  const ReportOptions& options = {});
std::vector<ReportSectionText> buildReportSections(const ArchiveManifest& manifest,
                                                   const ValidationResult& validation,
                                                   const ArchiveAnalysis& analysis,
                                                   const ReportOptions& options);
std::string renderReportText(const ArchiveReport& report,
                             const ReportOptions& options = {});
std::string renderReportJson(const ArchiveReport& report,
                             const ReportOptions& options = {});
std::string renderOverviewSection(const ArchiveManifest& manifest,
                                  const ValidationResult& validation,
                                  const ArchiveAnalysis& analysis);
std::string renderEntriesSection(const ArchiveManifest& manifest,
                                 const ReportOptions& options);
std::string renderDiagnosticsSection(const Diagnostics& diagnostics,
                                     const ReportOptions& options);
std::string renderAnalysisSection(const ArchiveAnalysis& analysis,
                                  const ReportOptions& options);
std::string renderLayoutSection(const ArchiveAnalysis& analysis,
                                const ReportOptions& options);
std::string renderRulesSection();

std::string reportStatusLine(const ArchiveReport& report);
std::string reportEntryTable(const std::vector<const ManifestEntry*>& entries,
                             std::size_t limit = 0);
std::string reportLayoutTable(const std::vector<LayoutSegment>& segments,
                              std::size_t limit = 0);
std::string reportTopEntries(const ArchiveManifest& manifest,
                             EntrySortKey key,
                             std::size_t limit);
std::string reportExtensionTable(const std::vector<ExtensionSummary>& summaries);
std::string reportDepthTable(const std::vector<DepthSummary>& summaries);
std::string reportBucketTable(const std::vector<SizeBucket>& buckets);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_REPORT_H_
