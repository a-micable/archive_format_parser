#include "report.h"

#include <sstream>

#include "byte_io.h"
#include "format_rules.h"

namespace vector {
namespace {

void appendSection(LineBuilder* lines, const ReportSectionText& section) {
    lines->appendLine(section.title);
    lines->appendLine(repeatString("-", section.title.size()));
    lines->appendLine(section.body);
}

std::string boolText(bool value) {
    return value ? "yes" : "no";
}

}  // namespace

std::string reportSectionName(ReportSection section) {
    switch (section) {
    case ReportSection::kOverview:
        return "overview";
    case ReportSection::kEntries:
        return "entries";
    case ReportSection::kDiagnostics:
        return "diagnostics";
    case ReportSection::kAnalysis:
        return "analysis";
    case ReportSection::kLayout:
        return "layout";
    case ReportSection::kRules:
        return "rules";
    }
    return "unknown";
}

ArchiveReport buildArchiveReport(const ArchiveManifest& manifest,
                                  const ValidationResult& validation,
                                  const ReportOptions& options) {
    ArchiveReport report;
    report.manifest = manifest;
    report.validation = validation;
    report.analysis = analyzeArchive(manifest);
    report.ok = validation.ok;
    report.sections = buildReportSections(report.manifest,
                                          report.validation,
                                          report.analysis,
                                          options);
    return report;
}

ArchiveReport buildArchiveReport(const ValidationResult& validation,
                                  const ReportOptions& options) {
    return buildArchiveReport(validation.manifest, validation, options);
}

std::vector<ReportSectionText> buildReportSections(const ArchiveManifest& manifest,
                                                   const ValidationResult& validation,
                                                   const ArchiveAnalysis& analysis,
                                                   const ReportOptions& options) {
    std::vector<ReportSectionText> sections;
    if (options.include_overview) {
        sections.push_back({ReportSection::kOverview,
                            "Overview",
                            renderOverviewSection(manifest, validation, analysis)});
    }
    if (options.include_entries) {
        sections.push_back({ReportSection::kEntries,
                            "Entries",
                            renderEntriesSection(manifest, options)});
    }
    if (options.include_diagnostics) {
        sections.push_back({ReportSection::kDiagnostics,
                            "Diagnostics",
                            renderDiagnosticsSection(validation.diagnostics, options)});
    }
    if (options.include_analysis) {
        sections.push_back({ReportSection::kAnalysis,
                            "Analysis",
                            renderAnalysisSection(analysis, options)});
    }
    if (options.include_layout) {
        sections.push_back({ReportSection::kLayout,
                            "Layout",
                            renderLayoutSection(analysis, options)});
    }
    if (options.include_rules) {
        sections.push_back({ReportSection::kRules,
                            "Format Rules",
                            renderRulesSection()});
    }
    return sections;
}

std::string renderReportText(const ArchiveReport& report,
                             const ReportOptions& options) {
    LineBuilder lines;
    lines.appendLine(reportStatusLine(report));
    if (!options.compact) {
        lines.blankLine();
    }
    for (std::size_t i = 0; i < report.sections.size(); ++i) {
        if (i != 0) {
            lines.blankLine();
        }
        appendSection(&lines, report.sections[i]);
    }
    return lines.str();
}

std::string renderReportJson(const ArchiveReport& report,
                             const ReportOptions& options) {
    (void)options;
    std::ostringstream out;
    out << "{\n"
        << "  \"ok\": " << (report.ok ? "true" : "false") << ",\n"
        << "  \"status\": \"" << jsonEscape(reportStatusLine(report)) << "\",\n"
        << "  \"manifest\": " << formatManifestJson(report.manifest) << ",\n"
        << "  \"analysis\": " << formatAnalysisJson(report.analysis) << ",\n"
        << "  \"diagnostics\": " << diagnosticsToJson(report.validation.diagnostics)
        << "}\n";
    return out.str();
}

std::string renderOverviewSection(const ArchiveManifest& manifest,
                                  const ValidationResult& validation,
                                  const ArchiveAnalysis& analysis) {
    LineBuilder lines;
    lines.appendKeyValue("source", manifest.source_name.empty() ? "(memory)" : manifest.source_name);
    lines.appendKeyValue("status", validation.ok ? "ok" : "failed");
    lines.appendKeyValue("archive size", humanSize(manifest.archive_size));
    lines.appendKeyValue("entries", std::to_string(analysis.stats.entry_count));
    lines.appendKeyValue("nested archives", std::to_string(analysis.stats.nested_archive_count));
    lines.appendKeyValue("compressed entries", std::to_string(analysis.stats.compressed_entry_count));
    lines.appendKeyValue("max depth", std::to_string(analysis.stats.max_depth));
    lines.appendKeyValue("extension", extensionStateName(manifest.extension_state));
    lines.appendKeyValue("diagnostics", formatDiagnosticSummary(validation.diagnostics.summary()));
    return lines.str();
}

std::string renderEntriesSection(const ArchiveManifest& manifest,
                                 const ReportOptions& options) {
    std::vector<const ManifestEntry*> entries =
        sortedEntries(manifest, EntrySortKey::kName, false);
    return reportEntryTable(entries, options.entry_limit);
}

std::string renderDiagnosticsSection(const Diagnostics& diagnostics,
                                     const ReportOptions& options) {
    DiagnosticFormatOptions format_options;
    format_options.include_infos = !options.compact;
    format_options.include_notes = !options.compact;
    format_options.include_summary = true;
    format_options.max_issues = options.compact ? 12 : 0;
    return formatDiagnostics(diagnostics, format_options);
}

std::string renderAnalysisSection(const ArchiveAnalysis& analysis,
                                  const ReportOptions& options) {
    LineBuilder lines;
    lines.appendLine(describeArchiveStats(analysis.stats));
    lines.appendLine(describeCompressionSummary(analysis.compression));
    lines.appendLine(describePathSummary(analysis.paths));
    if (!options.compact) {
        lines.blankLine();
        lines.appendLine("by extension:");
        lines.increaseIndent(2);
        lines.appendLine(reportExtensionTable(analysis.extensions));
        lines.decreaseIndent(2);
        lines.appendLine("by depth:");
        lines.increaseIndent(2);
        lines.appendLine(reportDepthTable(analysis.depths));
        lines.decreaseIndent(2);
        lines.appendLine("original size buckets:");
        lines.increaseIndent(2);
        lines.appendLine(reportBucketTable(analysis.original_buckets));
    }
    return lines.str();
}

std::string renderLayoutSection(const ArchiveAnalysis& analysis,
                                const ReportOptions& options) {
    return reportLayoutTable(analysis.layout, options.layout_limit);
}

std::string renderRulesSection() {
    return formatRulesText();
}

std::string reportStatusLine(const ArchiveReport& report) {
    std::ostringstream out;
    out << "archive report: " << (report.ok ? "ok" : "failed")
        << ", " << pluralize(report.analysis.stats.entry_count, "entry")
        << ", diagnostics "
        << formatDiagnosticSummary(report.validation.diagnostics.summary());
    return out.str();
}

std::string reportEntryTable(const std::vector<const ManifestEntry*>& entries,
                             std::size_t limit) {
    LineBuilder lines;
    std::size_t emitted = 0;
    for (const ManifestEntry* entry : entries) {
        if (limit != 0 && emitted >= limit) {
            lines.appendLine("... " + pluralize(entries.size() - emitted, "entry") + " omitted");
            break;
        }
        std::ostringstream row;
        row << entry->name
            << " depth=" << entry->depth
            << " packed=" << humanSize(entry->packed_size)
            << " original=" << humanSize(entry->original_size)
            << " compressed=" << boolText(entry->compressed)
            << " nested=" << boolText(entry->nested)
            << " offset=" << entry->chunk.absolute_offset;
        if (entry->name_changed) {
            row << " normalized=" << entry->normalized_name;
        }
        lines.appendLine(row.str());
        ++emitted;
    }
    if (entries.empty()) {
        lines.appendLine("(no entries)");
    }
    return lines.str();
}

std::string reportLayoutTable(const std::vector<LayoutSegment>& segments,
                              std::size_t limit) {
    LineBuilder lines;
    std::size_t emitted = 0;
    for (const LayoutSegment& segment : segments) {
        if (limit != 0 && emitted >= limit) {
            lines.appendLine("... " + pluralize(segments.size() - emitted, "segment") + " omitted");
            break;
        }
        lines.appendLine(describeLayoutSegment(segment));
        ++emitted;
    }
    if (segments.empty()) {
        lines.appendLine("(no layout segments)");
    }
    return lines.str();
}

std::string reportTopEntries(const ArchiveManifest& manifest,
                             EntrySortKey key,
                             std::size_t limit) {
    return reportEntryTable(sortedEntries(manifest, key, true), limit);
}

std::string reportExtensionTable(const std::vector<ExtensionSummary>& summaries) {
    LineBuilder lines;
    for (const ExtensionSummary& summary : summaries) {
        lines.appendLine(describeExtensionSummary(summary));
    }
    if (summaries.empty()) {
        lines.appendLine("(none)");
    }
    return lines.str();
}

std::string reportDepthTable(const std::vector<DepthSummary>& summaries) {
    LineBuilder lines;
    for (const DepthSummary& summary : summaries) {
        lines.appendLine(describeDepthSummary(summary));
    }
    if (summaries.empty()) {
        lines.appendLine("(none)");
    }
    return lines.str();
}

std::string reportBucketTable(const std::vector<SizeBucket>& buckets) {
    LineBuilder lines;
    for (const SizeBucket& bucket : buckets) {
        lines.appendLine(describeSizeBucket(bucket));
    }
    if (buckets.empty()) {
        lines.appendLine("(none)");
    }
    return lines.str();
}

}  // namespace vector
