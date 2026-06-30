#include "diagnostics.h"

#include <algorithm>
#include <sstream>

namespace vector {
namespace {

std::string colorPrefix(Severity severity) {
    switch (severity) {
    case Severity::kNote:
        return "\033[2m";
    case Severity::kInfo:
        return "\033[36m";
    case Severity::kWarning:
        return "\033[33m";
    case Severity::kError:
        return "\033[31m";
    }
    return "";
}

std::string colorReset() {
    return "\033[0m";
}

}  // namespace

bool SourceRange::empty() const {
    return length == 0;
}

std::size_t SourceRange::end() const {
    return offset + length;
}

ByteRange SourceRange::asByteRange() const {
    return {offset, length};
}

std::size_t DiagnosticSummary::total() const {
    return notes + infos + warnings + errors;
}

bool DiagnosticSummary::ok() const {
    return errors == 0;
}

void Diagnostics::add(Issue issue) {
    issues_.push_back(std::move(issue));
}

void Diagnostics::add(Severity severity,
                      IssueCategory category,
                      std::string code,
                      std::string message,
                      SourceRange range,
                      std::string detail,
                      std::string entry_name,
                      int depth) {
    Issue issue;
    issue.severity = severity;
    issue.category = category;
    issue.code = std::move(code);
    issue.message = std::move(message);
    issue.range = range;
    issue.detail = std::move(detail);
    issue.entry_name = std::move(entry_name);
    issue.depth = depth;
    add(std::move(issue));
}

void Diagnostics::note(IssueCategory category,
                       std::string code,
                       std::string message,
                       SourceRange range,
                       std::string detail,
                       std::string entry_name,
                       int depth) {
    add(Severity::kNote, category, std::move(code), std::move(message),
        range, std::move(detail), std::move(entry_name), depth);
}

void Diagnostics::info(IssueCategory category,
                       std::string code,
                       std::string message,
                       SourceRange range,
                       std::string detail,
                       std::string entry_name,
                       int depth) {
    add(Severity::kInfo, category, std::move(code), std::move(message),
        range, std::move(detail), std::move(entry_name), depth);
}

void Diagnostics::warning(IssueCategory category,
                          std::string code,
                          std::string message,
                          SourceRange range,
                          std::string detail,
                          std::string entry_name,
                          int depth) {
    add(Severity::kWarning, category, std::move(code), std::move(message),
        range, std::move(detail), std::move(entry_name), depth);
}

void Diagnostics::error(IssueCategory category,
                        std::string code,
                        std::string message,
                        SourceRange range,
                        std::string detail,
                        std::string entry_name,
                        int depth) {
    add(Severity::kError, category, std::move(code), std::move(message),
        range, std::move(detail), std::move(entry_name), depth);
}

const std::vector<Issue>& Diagnostics::issues() const {
    return issues_;
}

bool Diagnostics::empty() const {
    return issues_.empty();
}

bool Diagnostics::ok() const {
    return !hasErrors();
}

bool Diagnostics::hasWarnings() const {
    return std::any_of(issues_.begin(), issues_.end(), [](const Issue& issue) {
        return issue.severity == Severity::kWarning;
    });
}

bool Diagnostics::hasErrors() const {
    return std::any_of(issues_.begin(), issues_.end(), [](const Issue& issue) {
        return issue.severity == Severity::kError;
    });
}

std::size_t Diagnostics::size() const {
    return issues_.size();
}

DiagnosticSummary Diagnostics::summary() const {
    DiagnosticSummary summary;
    for (const Issue& issue : issues_) {
        switch (issue.severity) {
        case Severity::kNote:
            ++summary.notes;
            break;
        case Severity::kInfo:
            ++summary.infos;
            break;
        case Severity::kWarning:
            ++summary.warnings;
            break;
        case Severity::kError:
            ++summary.errors;
            break;
        }
    }
    return summary;
}

std::vector<Issue> Diagnostics::bySeverity(Severity severity) const {
    std::vector<Issue> out;
    for (const Issue& issue : issues_) {
        if (issue.severity == severity) {
            out.push_back(issue);
        }
    }
    return out;
}

std::vector<Issue> Diagnostics::byCategory(IssueCategory category) const {
    std::vector<Issue> out;
    for (const Issue& issue : issues_) {
        if (issue.category == category) {
            out.push_back(issue);
        }
    }
    return out;
}

void Diagnostics::appendFrom(const Diagnostics& other) {
    issues_.insert(issues_.end(), other.issues_.begin(), other.issues_.end());
}

void Diagnostics::clear() {
    issues_.clear();
}

std::string severityName(Severity severity) {
    switch (severity) {
    case Severity::kNote:
        return "note";
    case Severity::kInfo:
        return "info";
    case Severity::kWarning:
        return "warning";
    case Severity::kError:
        return "error";
    }
    return "unknown";
}

std::string severityShortName(Severity severity) {
    switch (severity) {
    case Severity::kNote:
        return "N";
    case Severity::kInfo:
        return "I";
    case Severity::kWarning:
        return "W";
    case Severity::kError:
        return "E";
    }
    return "?";
}

std::string categoryName(IssueCategory category) {
    switch (category) {
    case IssueCategory::kFormat:
        return "format";
    case IssueCategory::kHeader:
        return "header";
    case IssueCategory::kIndex:
        return "index";
    case IssueCategory::kChunk:
        return "chunk";
    case IssueCategory::kExtension:
        return "extension";
    case IssueCategory::kPath:
        return "path";
    case IssueCategory::kNesting:
        return "nesting";
    case IssueCategory::kCompression:
        return "compression";
    case IssueCategory::kLayout:
        return "layout";
    case IssueCategory::kPolicy:
        return "policy";
    case IssueCategory::kIo:
        return "io";
    }
    return "unknown";
}

std::string issueLocation(const Issue& issue) {
    std::ostringstream out;
    if (issue.range.length != 0) {
        out << "@" << issue.range.offset << ".." << issue.range.end();
    } else {
        out << "@" << issue.range.offset;
    }
    if (issue.depth != 0) {
        out << " depth=" << issue.depth;
    }
    if (!issue.entry_name.empty()) {
        out << " entry=\"" << issue.entry_name << "\"";
    }
    return out.str();
}

std::string issueOneLine(const Issue& issue,
                         const DiagnosticFormatOptions& options) {
    std::ostringstream out;
    if (options.color) {
        out << colorPrefix(issue.severity);
    }
    out << severityName(issue.severity) << "[" << issue.code << "]"
        << " " << categoryName(issue.category) << ": " << issue.message;
    if (options.include_ranges) {
        out << " " << issueLocation(issue);
    }
    if (options.color) {
        out << colorReset();
    }
    if (options.include_details && !issue.detail.empty()) {
        out << "\n  " << issue.detail;
    }
    return out.str();
}

std::string formatDiagnostics(const Diagnostics& diagnostics,
                              const DiagnosticFormatOptions& options) {
    LineBuilder lines;
    std::size_t emitted = 0;
    for (const Issue& issue : diagnostics.issues()) {
        if (!severityEnabled(issue.severity, options)) {
            continue;
        }
        if (options.max_issues != 0 && emitted >= options.max_issues) {
            const std::size_t remaining = diagnostics.size() - emitted;
            lines.appendLine("... " + pluralize(remaining, "additional issue") +
                             " omitted");
            break;
        }
        lines.appendLine(issueOneLine(issue, options));
        ++emitted;
    }
    if (options.include_summary) {
        if (emitted != 0) {
            lines.blankLine();
        }
        lines.appendLine(formatDiagnosticSummary(diagnostics.summary()));
    }
    return lines.str();
}

std::string formatDiagnosticSummary(const DiagnosticSummary& summary) {
    std::vector<std::string> parts;
    if (summary.errors != 0) {
        parts.push_back(pluralize(summary.errors, "error"));
    }
    if (summary.warnings != 0) {
        parts.push_back(pluralize(summary.warnings, "warning"));
    }
    if (summary.infos != 0) {
        parts.push_back(pluralize(summary.infos, "info", "infos"));
    }
    if (summary.notes != 0) {
        parts.push_back(pluralize(summary.notes, "note"));
    }
    if (parts.empty()) {
        return "no issues";
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += parts[i];
    }
    return out;
}

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char ch : value) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                out += "\\u00";
                out += hexByte(ch);
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

std::string diagnosticsToJson(const Diagnostics& diagnostics,
                              const DiagnosticFormatOptions& options) {
    std::ostringstream out;
    out << "{\n  \"issues\": [\n";
    bool first = true;
    std::size_t emitted = 0;
    for (const Issue& issue : diagnostics.issues()) {
        if (!severityEnabled(issue.severity, options)) {
            continue;
        }
        if (options.max_issues != 0 && emitted >= options.max_issues) {
            break;
        }
        if (!first) {
            out << ",\n";
        }
        first = false;
        out << "    {"
            << "\"severity\":\"" << severityName(issue.severity) << "\","
            << "\"category\":\"" << categoryName(issue.category) << "\","
            << "\"code\":\"" << jsonEscape(issue.code) << "\","
            << "\"message\":\"" << jsonEscape(issue.message) << "\","
            << "\"detail\":\"" << jsonEscape(issue.detail) << "\","
            << "\"entry\":\"" << jsonEscape(issue.entry_name) << "\","
            << "\"depth\":" << issue.depth << ","
            << "\"offset\":" << issue.range.offset << ","
            << "\"length\":" << issue.range.length
            << "}";
        ++emitted;
    }
    const DiagnosticSummary summary = diagnostics.summary();
    out << "\n  ],\n"
        << "  \"summary\": {"
        << "\"notes\":" << summary.notes << ","
        << "\"infos\":" << summary.infos << ","
        << "\"warnings\":" << summary.warnings << ","
        << "\"errors\":" << summary.errors
        << "}\n"
        << "}\n";
    return out.str();
}

bool severityEnabled(Severity severity, const DiagnosticFormatOptions& options) {
    switch (severity) {
    case Severity::kNote:
        return options.include_notes;
    case Severity::kInfo:
        return options.include_infos;
    case Severity::kWarning:
        return options.include_warnings;
    case Severity::kError:
        return options.include_errors;
    }
    return true;
}

Severity maxSeverity(Severity a, Severity b) {
    return severityRank(a) >= severityRank(b) ? a : b;
}

Severity highestSeverity(const Diagnostics& diagnostics) {
    Severity highest = Severity::kNote;
    for (const Issue& issue : diagnostics.issues()) {
        highest = maxSeverity(highest, issue.severity);
    }
    return highest;
}

int severityRank(Severity severity) {
    switch (severity) {
    case Severity::kNote:
        return 0;
    case Severity::kInfo:
        return 1;
    case Severity::kWarning:
        return 2;
    case Severity::kError:
        return 3;
    }
    return -1;
}

IssueBuilder::IssueBuilder(std::string code) {
    issue_.code = std::move(code);
}

IssueBuilder& IssueBuilder::severity(Severity value) {
    issue_.severity = value;
    return *this;
}

IssueBuilder& IssueBuilder::category(IssueCategory value) {
    issue_.category = value;
    return *this;
}

IssueBuilder& IssueBuilder::message(std::string value) {
    issue_.message = std::move(value);
    return *this;
}

IssueBuilder& IssueBuilder::detail(std::string value) {
    issue_.detail = std::move(value);
    return *this;
}

IssueBuilder& IssueBuilder::entry(std::string value) {
    issue_.entry_name = std::move(value);
    return *this;
}

IssueBuilder& IssueBuilder::depth(int value) {
    issue_.depth = value;
    return *this;
}

IssueBuilder& IssueBuilder::range(SourceRange value) {
    issue_.range = value;
    return *this;
}

Issue IssueBuilder::build() const {
    return issue_;
}

}  // namespace vector
