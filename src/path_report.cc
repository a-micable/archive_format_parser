#include "path_report.h"

#include <sstream>

#include "byte_io.h"

namespace vector {
namespace {

void addEntryToPlan(const ManifestEntry& entry,
                    CollisionResolver* resolver,
                    const PathPolicy& policy,
                    std::vector<ExtractionPlanEntry>* entries) {
    entries->push_back(makeExtractionPlanEntry(entry, resolver, policy));
    for (const ManifestEntry& child : entry.children) {
        addEntryToPlan(child, resolver, policy, entries);
    }
}

void countIssues(const Diagnostics& diagnostics,
                 std::map<std::string, std::uint64_t>* counts) {
    for (const Issue& issue : diagnostics.issues()) {
        ++(*counts)[issue.code];
    }
}

}  // namespace

ExtractionPlan buildExtractionPlan(const ArchiveManifest& manifest,
                                   const PathPolicy& policy) {
    ExtractionPlan plan;
    CollisionResolver resolver(policy);
    for (const ManifestEntry& entry : manifest.entries) {
        addEntryToPlan(entry, &resolver, policy, &plan.entries);
    }
    plan.summary = summarizeExtractionPlan(plan.entries);
    plan.diagnostics = collectPathDiagnostics(plan.entries);
    return plan;
}

ExtractionPlanEntry makeExtractionPlanEntry(const ManifestEntry& entry,
                                            CollisionResolver* resolver,
                                            const PathPolicy& policy) {
    ExtractionPlanEntry out;
    out.archive_name = entry.name;
    out.original_size = entry.original_size;
    out.depth = entry.depth;

    PathAnalysis analysis = analyzePath(entry.name, policy);
    out.normalized_name = analysis.normalized;
    out.valid = analysis.valid;
    out.changed = analysis.changed;
    out.diagnostics.appendFrom(analysis.diagnostics);

    CollisionResult collision = resolver->assign(entry.name);
    out.assigned_name = collision.assigned;
    out.collided = collision.collided;
    if (collision.collided) {
        out.diagnostics.warning(IssueCategory::kPath,
                                "path.collision",
                                "entry name collides with an earlier output name",
                                {0, entry.name.size()},
                                "assigned name: " + collision.assigned,
                                entry.name,
                                entry.depth);
    }
    return out;
}

PathPolicySummary summarizeExtractionPlan(const std::vector<ExtractionPlanEntry>& entries) {
    PathPolicySummary summary;
    summary.entries = entries.size();
    for (const ExtractionPlanEntry& entry : entries) {
        if (entry.valid) {
            ++summary.valid;
        } else {
            ++summary.invalid;
        }
        if (entry.changed) {
            ++summary.changed;
        }
        if (entry.collided) {
            ++summary.collisions;
        }
        countIssues(entry.diagnostics, &summary.issue_counts);
    }
    return summary;
}

Diagnostics collectPathDiagnostics(const std::vector<ExtractionPlanEntry>& entries) {
    Diagnostics diagnostics;
    for (const ExtractionPlanEntry& entry : entries) {
        diagnostics.appendFrom(entry.diagnostics);
    }
    return diagnostics;
}

std::vector<ExtractionPlanEntry> invalidPlanEntries(const ExtractionPlan& plan) {
    std::vector<ExtractionPlanEntry> out;
    for (const ExtractionPlanEntry& entry : plan.entries) {
        if (!entry.valid) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<ExtractionPlanEntry> collidedPlanEntries(const ExtractionPlan& plan) {
    std::vector<ExtractionPlanEntry> out;
    for (const ExtractionPlanEntry& entry : plan.entries) {
        if (entry.collided) {
            out.push_back(entry);
        }
    }
    return out;
}

std::vector<ExtractionPlanEntry> changedPlanEntries(const ExtractionPlan& plan) {
    std::vector<ExtractionPlanEntry> out;
    for (const ExtractionPlanEntry& entry : plan.entries) {
        if (entry.changed) {
            out.push_back(entry);
        }
    }
    return out;
}

std::string pathPolicySummaryText(const PathPolicySummary& summary) {
    std::ostringstream out;
    out << pluralize(summary.entries, "entry")
        << ", valid " << summary.valid
        << ", invalid " << summary.invalid
        << ", changed " << summary.changed
        << ", collisions " << summary.collisions;
    if (!summary.issue_counts.empty()) {
        out << ", issues: " << formatPathIssueCounts(summary.issue_counts);
    }
    return out.str();
}

std::string extractionPlanEntryText(const ExtractionPlanEntry& entry) {
    std::ostringstream out;
    out << entry.archive_name << " -> " << entry.assigned_name
        << " size=" << humanSize(entry.original_size)
        << " depth=" << entry.depth;
    if (!entry.valid) {
        out << " invalid";
    }
    if (entry.changed) {
        out << " normalized=" << entry.normalized_name;
    }
    if (entry.collided) {
        out << " collision";
    }
    return out.str();
}

std::string formatExtractionPlanText(const ExtractionPlan& plan) {
    LineBuilder lines;
    lines.appendLine("extraction plan");
    lines.increaseIndent(2);
    lines.appendLine(pathPolicySummaryText(plan.summary));
    for (const ExtractionPlanEntry& entry : plan.entries) {
        lines.appendLine(extractionPlanEntryText(entry));
    }
    if (!plan.diagnostics.empty()) {
        lines.blankLine();
        lines.appendLine("path diagnostics:");
        lines.increaseIndent(2);
        DiagnosticFormatOptions options;
        options.include_summary = true;
        lines.appendLine(formatDiagnostics(plan.diagnostics, options));
    }
    return lines.str();
}

std::string formatExtractionPlanJson(const ExtractionPlan& plan) {
    std::ostringstream out;
    out << "{\n"
        << "  \"summary\": {"
        << "\"entries\":" << plan.summary.entries << ","
        << "\"valid\":" << plan.summary.valid << ","
        << "\"invalid\":" << plan.summary.invalid << ","
        << "\"changed\":" << plan.summary.changed << ","
        << "\"collisions\":" << plan.summary.collisions
        << "},\n"
        << "  \"entries\": [";
    for (std::size_t i = 0; i < plan.entries.size(); ++i) {
        const ExtractionPlanEntry& entry = plan.entries[i];
        if (i != 0) {
            out << ",";
        }
        out << "\n    {\"archiveName\":\"" << jsonEscape(entry.archive_name)
            << "\",\"normalizedName\":\"" << jsonEscape(entry.normalized_name)
            << "\",\"assignedName\":\"" << jsonEscape(entry.assigned_name)
            << "\",\"valid\":" << (entry.valid ? "true" : "false")
            << ",\"changed\":" << (entry.changed ? "true" : "false")
            << ",\"collided\":" << (entry.collided ? "true" : "false")
            << ",\"size\":" << entry.original_size
            << ",\"depth\":" << entry.depth << "}";
    }
    out << "\n  ],\n  \"diagnostics\": "
        << diagnosticsToJson(plan.diagnostics)
        << "}\n";
    return out.str();
}

std::string formatPathIssueCounts(const std::map<std::string, std::uint64_t>& counts) {
    std::string out;
    for (const auto& item : counts) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item.first + "=" + std::to_string(item.second);
    }
    return out.empty() ? "none" : out;
}

bool extractionPlanHasErrors(const ExtractionPlan& plan) {
    return plan.diagnostics.hasErrors();
}

bool extractionPlanHasCollisions(const ExtractionPlan& plan) {
    return plan.summary.collisions != 0;
}

bool assignedNameIsUnique(const ExtractionPlan& plan, const std::string& assigned_name) {
    std::uint64_t count = 0;
    for (const ExtractionPlanEntry& entry : plan.entries) {
        if (entry.assigned_name == assigned_name) {
            ++count;
        }
    }
    return count == 1;
}

std::map<std::string, std::vector<ExtractionPlanEntry>> entriesByAssignedDirectory(
    const ExtractionPlan& plan) {
    std::map<std::string, std::vector<ExtractionPlanEntry>> groups;
    for (const ExtractionPlanEntry& entry : plan.entries) {
        std::string dir = dirnameOf(entry.assigned_name);
        if (dir.empty()) {
            dir = "(root)";
        }
        groups[dir].push_back(entry);
    }
    return groups;
}

}  // namespace vector
