#ifndef VECTOR_ARCHIVE_DIAGNOSTICS_H_
#define VECTOR_ARCHIVE_DIAGNOSTICS_H_

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "byte_io.h"

namespace vector {

enum class Severity {
    kNote,
    kInfo,
    kWarning,
    kError,
};

enum class IssueCategory {
    kFormat,
    kHeader,
    kIndex,
    kChunk,
    kExtension,
    kPath,
    kNesting,
    kCompression,
    kLayout,
    kPolicy,
    kIo,
};

struct SourceRange {
    std::size_t offset = 0;
    std::size_t length = 0;

    bool empty() const;
    std::size_t end() const;
    ByteRange asByteRange() const;
};

struct Issue {
    Severity severity = Severity::kInfo;
    IssueCategory category = IssueCategory::kFormat;
    std::string code;
    std::string message;
    std::string detail;
    std::string entry_name;
    int depth = 0;
    SourceRange range;
};

struct DiagnosticSummary {
    std::size_t notes = 0;
    std::size_t infos = 0;
    std::size_t warnings = 0;
    std::size_t errors = 0;

    std::size_t total() const;
    bool ok() const;
};

class Diagnostics {
public:
    void add(Issue issue);
    void add(Severity severity,
             IssueCategory category,
             std::string code,
             std::string message,
             SourceRange range = {},
             std::string detail = {},
             std::string entry_name = {},
             int depth = 0);

    void note(IssueCategory category,
              std::string code,
              std::string message,
              SourceRange range = {},
              std::string detail = {},
              std::string entry_name = {},
              int depth = 0);
    void info(IssueCategory category,
              std::string code,
              std::string message,
              SourceRange range = {},
              std::string detail = {},
              std::string entry_name = {},
              int depth = 0);
    void warning(IssueCategory category,
                 std::string code,
                 std::string message,
                 SourceRange range = {},
                 std::string detail = {},
                 std::string entry_name = {},
                 int depth = 0);
    void error(IssueCategory category,
               std::string code,
               std::string message,
               SourceRange range = {},
               std::string detail = {},
               std::string entry_name = {},
               int depth = 0);

    const std::vector<Issue>& issues() const;
    bool empty() const;
    bool ok() const;
    bool hasWarnings() const;
    bool hasErrors() const;
    std::size_t size() const;
    DiagnosticSummary summary() const;
    std::vector<Issue> bySeverity(Severity severity) const;
    std::vector<Issue> byCategory(IssueCategory category) const;
    void appendFrom(const Diagnostics& other);
    void clear();

private:
    std::vector<Issue> issues_;
};

struct DiagnosticFormatOptions {
    bool include_notes = true;
    bool include_infos = true;
    bool include_warnings = true;
    bool include_errors = true;
    bool include_details = true;
    bool include_ranges = true;
    bool include_entry_names = true;
    bool include_summary = true;
    bool color = false;
    std::size_t max_issues = 0;
};

std::string severityName(Severity severity);
std::string severityShortName(Severity severity);
std::string categoryName(IssueCategory category);
std::string issueLocation(const Issue& issue);
std::string issueOneLine(const Issue& issue,
                         const DiagnosticFormatOptions& options = {});
std::string formatDiagnostics(const Diagnostics& diagnostics,
                              const DiagnosticFormatOptions& options = {});
std::string formatDiagnosticSummary(const DiagnosticSummary& summary);
std::string jsonEscape(std::string_view value);
std::string diagnosticsToJson(const Diagnostics& diagnostics,
                              const DiagnosticFormatOptions& options = {});
bool severityEnabled(Severity severity, const DiagnosticFormatOptions& options);
Severity maxSeverity(Severity a, Severity b);
Severity highestSeverity(const Diagnostics& diagnostics);
int severityRank(Severity severity);

class IssueBuilder {
public:
    explicit IssueBuilder(std::string code);

    IssueBuilder& severity(Severity value);
    IssueBuilder& category(IssueCategory value);
    IssueBuilder& message(std::string value);
    IssueBuilder& detail(std::string value);
    IssueBuilder& entry(std::string value);
    IssueBuilder& depth(int value);
    IssueBuilder& range(SourceRange value);
    Issue build() const;

private:
    Issue issue_;
};

}  // namespace vector

#endif  // VECTOR_ARCHIVE_DIAGNOSTICS_H_
