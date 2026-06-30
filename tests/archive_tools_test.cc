#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "archive.h"
#include "archive_stats.h"
#include "byte_io.h"
#include "catalog.h"
#include "diagnostics.h"
#include "format_rules.h"
#include "inspect.h"
#include "manifest.h"
#include "path_utils.h"
#include "path_report.h"
#include "report.h"
#include "validator.h"

namespace {

std::filesystem::path tempRoot() {
    auto root = std::filesystem::temp_directory_path() /
                ("vector-archive-tools-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void byteIoHelpers() {
    std::vector<std::uint8_t> bytes = {0x01, 0x7f, 0x80, 0xff, 'A', '\n'};
    vector::ByteView view(bytes);
    assert(view.size() == bytes.size());
    assert(view.has(1, 3));
    assert(!view.has(4, 10));
    assert(vector::hexByte(0xaf) == "af");
    assert(vector::hexRange(view, 0, 3) == "01 7f 80");
    assert(vector::asciiBytes(view, 4, 2) == "A.");
    assert(vector::humanSize(1024) == "1.00 KiB");
    assert(vector::decimalWithCommas(1234567) == "1,234,567");

    std::vector<std::uint8_t> encoded = vector::encodeVarintToBytes(300);
    vector::VarintInfo info = vector::inspectVarint(vector::ByteView(encoded), 0);
    assert(info.valid);
    assert(info.canonical);
    assert(info.value == 300);
    assert(info.encoded_size == encoded.size());

    vector::CheckedReader reader{vector::ByteView(bytes)};
    std::uint8_t first = 0;
    assert(reader.readByte(&first));
    assert(first == 0x01);
    assert(reader.skip(2));
    assert(reader.position() == 3);
    assert(!reader.skip(100));
    assert(!reader.good());
}

void pathPolicyRejectsUnsafeNames() {
    vector::PathAnalysis parent = vector::analyzePath("../escape.txt");
    assert(!parent.valid);
    assert(parent.normalized == "escape.txt");
    assert(parent.diagnostics.hasWarnings());

    vector::PathAnalysis absolute = vector::analyzePath("/tmp/value.txt");
    assert(!absolute.valid);
    assert(absolute.normalized == "tmp/value.txt");

    vector::PathAnalysis device = vector::analyzePath("CON.txt");
    assert(!device.valid);
    assert(device.normalized == "_CON.txt");

    vector::PathAnalysis clean = vector::analyzePath("dir/name.txt");
    assert(clean.valid);
    assert(clean.normalized == "dir/name.txt");
    assert(!clean.changed);

    vector::CollisionResolver resolver;
    auto first = resolver.assign("Dir/File.txt");
    auto second = resolver.assign("dir/file.txt");
    assert(!first.collided);
    assert(second.collided);
    assert(second.assigned == "dir/file (2).txt");
}

void diagnosticsFormatting() {
    vector::Diagnostics diagnostics;
    diagnostics.warning(vector::IssueCategory::kPath,
                        "path.test",
                        "path warning",
                        {4, 2},
                        "detail",
                        "entry.txt",
                        1);
    diagnostics.error(vector::IssueCategory::kChunk,
                      "chunk.test",
                      "chunk error",
                      {10, 3});
    assert(diagnostics.hasWarnings());
    assert(diagnostics.hasErrors());
    assert(diagnostics.summary().warnings == 1);
    assert(diagnostics.summary().errors == 1);
    std::string text = vector::formatDiagnostics(diagnostics);
    assert(text.find("path.test") != std::string::npos);
    assert(text.find("chunk.test") != std::string::npos);
    std::string json = vector::diagnosticsToJson(diagnostics);
    assert(json.find("\"warnings\":1") != std::string::npos);
}

std::vector<std::uint8_t> sampleArchive() {
    std::vector<vector::FilePayload> files = {
        {"alpha.txt", {'a', 'l', 'p', 'h', 'a'}},
        {"runs.bin", std::vector<std::uint8_t>(300, 'R')},
    };
    return vector::buildArchiveForTest(files, true, true, true);
}

void inspectManifestListsEntries() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::InspectOptions options;
    options.source_name = "memory.vec";
    vector::InspectResult result = vector::inspectArchiveBytes(archive, options);
    assert(result.parsed);
    assert(result.manifest.entries.size() == 2);
    assert(result.manifest.extension_state == vector::ExtensionState::kChecksumValid);
    assert(vector::manifestHasEntry(result.manifest, "alpha.txt"));
    assert(vector::manifestHasCompressedEntries(result.manifest));
    assert(vector::countEntriesRecursive(result.manifest) == 2);
    std::string text = vector::formatManifestText(result.manifest);
    assert(text.find("alpha.txt") != std::string::npos);
    assert(text.find("runs.bin") != std::string::npos);
    std::string json = vector::formatManifestJson(result.manifest);
    assert(json.find("\"entries\"") != std::string::npos);

    vector::ArchiveAnalysis analysis = vector::analyzeArchive(result.manifest);
    assert(analysis.stats.entry_count == 2);
    assert(!analysis.extensions.empty());
    assert(vector::formatAnalysisText(analysis).find("archive analysis") !=
           std::string::npos);
}

void formatRulesDescribeArchiveShape() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::InspectResult result = vector::inspectArchiveBytes(archive);
    assert(result.parsed);

    vector::ArchiveFormatProfile profile =
        vector::profileArchiveFormat(result.manifest);
    assert(profile.archive_size_class != vector::SizeClass::kEmpty);
    assert(profile.entries.size() == 2);
    assert(vector::joinFlagNames(profile.flags).find("extension") !=
           std::string::npos);

    const vector::ManifestEntry* alpha =
        vector::findEntryByName(result.manifest, "alpha.txt");
    assert(alpha != nullptr);
    vector::EntryFormatProfile entry_profile =
        vector::profileEntryFormat(*alpha);
    assert(entry_profile.name_class == vector::NameClass::kSimple);
    assert(vector::describeEntryProfile(entry_profile).find("alpha.txt") !=
           std::string::npos);

    vector::Diagnostics diagnostics;
    vector::checkArchiveFormatRules(result.manifest,
                                    vector::defaultFormatLimits(),
                                    &diagnostics);
    assert(!diagnostics.hasErrors());
    assert(vector::formatRulesText().find("archive format rules") !=
           std::string::npos);
    assert(vector::formatRulesJson().find("\"fields\"") != std::string::npos);
}

void archiveAnalysisSummarizesEntries() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::InspectResult result = vector::inspectArchiveBytes(archive);
    vector::ArchiveAnalysis analysis = vector::analyzeArchive(result.manifest);

    assert(analysis.stats.entry_count == 2);
    assert(analysis.compression.compressed_entries >= 1);
    assert(!analysis.original_buckets.empty());
    assert(!analysis.extensions.empty());
    assert(!analysis.depths.empty());
    assert(!analysis.layout.empty());
    assert(vector::totalSegmentBytes(analysis.layout,
                                     vector::LayoutSegmentKind::kChunkPayload) > 0);

    std::vector<const vector::ManifestEntry*> largest =
        vector::largestEntries(result.manifest, 1);
    assert(largest.size() == 1);
    assert(largest.front()->original_size >= 5);
    assert(!vector::entriesWithExtension(result.manifest, ".txt").empty());
    assert(!vector::entriesMatchingName(result.manifest, "alpha").empty());
    assert(vector::formatAnalysisJson(analysis).find("\"layout\"") !=
           std::string::npos);
}

void reportBuilderRendersValidationContext() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::ValidationResult validation = vector::validateArchiveBytes(archive);
    assert(validation.ok);

    vector::ReportOptions options;
    options.include_rules = true;
    options.entry_limit = 8;
    options.layout_limit = 16;
    vector::ArchiveReport report = vector::buildArchiveReport(validation, options);
    assert(report.ok);
    assert(!report.sections.empty());
    assert(vector::reportStatusLine(report).find("archive report: ok") !=
           std::string::npos);

    std::string text = vector::renderReportText(report, options);
    assert(text.find("Overview") != std::string::npos);
    assert(text.find("Entries") != std::string::npos);
    assert(text.find("Format Rules") != std::string::npos);

    std::string json = vector::renderReportJson(report, options);
    assert(json.find("\"ok\": true") != std::string::npos);
    assert(json.find("\"analysis\"") != std::string::npos);
}

void catalogSearchGroupsAndDiffsEntries() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::InspectResult result = vector::inspectArchiveBytes(archive);
    std::vector<vector::CatalogEntry> catalog =
        vector::buildCatalog(result.manifest);
    assert(catalog.size() == 2);

    std::vector<vector::CatalogEntry> alpha =
        vector::findCatalogEntries(catalog,
                                   "ALPHA",
                                   vector::CatalogMatchMode::kCaseInsensitiveContains);
    assert(alpha.size() == 1);
    assert(alpha.front().basename == "alpha.txt");

    std::vector<vector::CatalogEntry> compressed =
        vector::filterCatalogByFlags(catalog, true, false);
    assert(!compressed.empty());
    std::vector<vector::CatalogGroup> by_extension =
        vector::groupCatalog(catalog, vector::CatalogGroupKey::kExtension);
    assert(!by_extension.empty());
    assert(vector::formatCatalogGroupsText(by_extension).find("catalog groups") !=
           std::string::npos);
    assert(vector::formatCatalogJson(catalog).find("\"entries\"") !=
           std::string::npos);

    std::vector<vector::FilePayload> changed_files = {
        {"alpha.txt", {'a', 'l', 'p', 'h', 'a', '!'}},
        {"new.txt", {'n', 'e', 'w'}},
    };
    std::vector<std::uint8_t> changed =
        vector::buildArchiveForTest(changed_files, false, false, true);
    vector::InspectResult changed_result = vector::inspectArchiveBytes(changed);
    vector::CatalogDiff diff =
        vector::diffCatalogs(result.manifest, changed_result.manifest);
    assert(diff.added == 1);
    assert(diff.removed == 1);
    assert(diff.changed == 1);
    assert(vector::formatCatalogDiffText(diff).find("catalog diff") !=
           std::string::npos);
}

void extractionPlanReportsPathPolicy() {
    std::vector<vector::FilePayload> files = {
        {"../escape.txt", {'x'}},
        {"dir/File.txt", {'a'}},
        {"dir/file.txt", {'b'}},
    };
    std::vector<std::uint8_t> archive =
        vector::buildArchiveForTest(files, false, false, true);
    vector::InspectResult result = vector::inspectArchiveBytes(archive);
    vector::ExtractionPlan plan = vector::buildExtractionPlan(result.manifest);

    assert(plan.summary.entries == 3);
    assert(plan.summary.changed >= 1);
    assert(plan.summary.collisions >= 1);
    assert(vector::extractionPlanHasCollisions(plan));
    assert(!vector::changedPlanEntries(plan).empty());
    assert(!vector::collidedPlanEntries(plan).empty());
    assert(vector::assignedNameIsUnique(plan, "escape.txt"));
    assert(!vector::entriesByAssignedDirectory(plan).empty());
    assert(vector::formatExtractionPlanText(plan).find("extraction plan") !=
           std::string::npos);
    assert(vector::formatExtractionPlanJson(plan).find("\"entries\"") !=
           std::string::npos);
}

void inspectNestedManifest() {
    auto leaf = vector::buildArchiveForTest({{"leaf.txt", {'l', 'e', 'a', 'f'}}},
                                            false, true, true);
    auto root = vector::buildArchiveForTest({{"leaf.vec", leaf},
                                             {"root.txt", {'r'}}},
                                            false, false, true);
    vector::InspectResult result = vector::inspectArchiveBytes(root);
    assert(result.parsed);
    assert(result.manifest.entries.size() == 2);
    const vector::ManifestEntry* nested = vector::findEntryByName(result.manifest, "leaf.vec");
    assert(nested != nullptr);
    assert(nested->nested);
    assert(!nested->children.empty());
    assert(nested->children.front().name == "leaf.txt");
    assert(vector::manifestHasNestedArchives(result.manifest));
}

void validateGoodArchive() {
    std::vector<std::uint8_t> archive = sampleArchive();
    vector::ValidationResult result = vector::validateArchiveBytes(archive);
    assert(result.ok);
    assert(!result.diagnostics.hasErrors());
    std::string text = vector::formatValidationText(result);
    assert(text.find("validation: ok") != std::string::npos);
}

void validateBadMagic() {
    std::vector<std::uint8_t> archive = sampleArchive();
    archive[0] = 'X';
    vector::ValidationResult result = vector::validateArchiveBytes(archive);
    assert(!result.ok);
    assert(result.diagnostics.hasErrors());
    std::string text = vector::formatValidationText(result);
    assert(text.find("validation: failed") != std::string::npos);
}

void validatePathWarnings() {
    std::vector<vector::FilePayload> files = {
        {"../escape.txt", {'x'}},
        {"safe.txt", {'s'}},
    };
    std::vector<std::uint8_t> archive =
        vector::buildArchiveForTest(files, false, false, true);
    vector::ValidationOptions options;
    options.require_canonical_paths = true;
    vector::ValidationResult result = vector::validateArchiveBytes(archive, options);
    assert(result.ok);
    assert(result.diagnostics.hasWarnings());
    assert(vector::formatDiagnostics(result.diagnostics).find("path") != std::string::npos);
}

void fileInspectionRoundTrip() {
    const auto root = tempRoot();
    const auto input_dir = root / "input";
    writeText(input_dir / "one.txt", "one one one");
    writeText(input_dir / "two.txt", "two");
    const auto archive_path = root / "bundle.vec";
    std::string error;
    assert(vector::pack({input_dir / "one.txt", input_dir / "two.txt"},
                        archive_path, true, &error));

    vector::InspectResult inspected = vector::inspectArchiveFile(archive_path);
    assert(inspected.parsed);
    assert(inspected.manifest.entries.size() == 2);

    vector::ValidationResult validated = vector::validateArchiveFile(archive_path);
    assert(validated.ok);
    assert(vector::formatManifestText(inspected.manifest).find("one.txt") !=
           std::string::npos);
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    byteIoHelpers();
    pathPolicyRejectsUnsafeNames();
    diagnosticsFormatting();
    inspectManifestListsEntries();
    formatRulesDescribeArchiveShape();
    archiveAnalysisSummarizesEntries();
    reportBuilderRendersValidationContext();
    catalogSearchGroupsAndDiffsEntries();
    extractionPlanReportsPathPolicy();
    inspectNestedManifest();
    validateGoodArchive();
    validateBadMagic();
    validatePathWarnings();
    fileInspectionRoundTrip();
    return 0;
}
