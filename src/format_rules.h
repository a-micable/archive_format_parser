#ifndef VECTOR_ARCHIVE_FORMAT_RULES_H_
#define VECTOR_ARCHIVE_FORMAT_RULES_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "byte_io.h"
#include "diagnostics.h"
#include "manifest.h"

namespace vector {

enum class FieldKind {
    kMagic,
    kVersion,
    kFlags,
    kVarint,
    kIndexCount,
    kIndexEntry,
    kChunkHeader,
    kChunkPayload,
    kExtension,
    kName,
};

enum class FlagDomain {
    kArchive,
    kEntry,
    kChunk,
};

enum class NameClass {
    kEmpty,
    kSimple,
    kDirectory,
    kAbsolute,
    kRelativeWithDots,
    kDevice,
    kControl,
    kLong,
};

enum class SizeClass {
    kEmpty,
    kTiny,
    kSmall,
    kMedium,
    kLarge,
    kHuge,
};

struct FlagRule {
    FlagDomain domain = FlagDomain::kArchive;
    std::uint8_t bit = 0;
    std::string name;
    std::string description;
};

struct FieldRule {
    FieldKind kind = FieldKind::kMagic;
    std::string name;
    std::string description;
    std::size_t minimum_size = 0;
    bool variable_size = false;
};

struct FormatLimits {
    std::uint64_t max_entries = 4096;
    std::uint64_t max_name_length = 4096;
    std::uint64_t max_chunk_payload = 64ull * 1024ull * 1024ull;
    std::uint64_t max_nested_depth = 8;
    std::uint64_t max_extension_payload = 16ull * 1024ull * 1024ull;
    std::uint64_t max_reasonable_archive = 1024ull * 1024ull * 1024ull;
};

struct FlagBreakdown {
    FlagDomain domain = FlagDomain::kArchive;
    std::uint8_t raw = 0;
    std::uint8_t known = 0;
    std::uint8_t unknown = 0;
    std::vector<std::string> names;
};

struct EntryFormatProfile {
    std::string name;
    NameClass name_class = NameClass::kEmpty;
    SizeClass packed_class = SizeClass::kEmpty;
    SizeClass original_class = SizeClass::kEmpty;
    FlagBreakdown flags;
    double packed_to_original = 0.0;
    double original_to_packed = 0.0;
    bool has_size_mismatch = false;
    bool claims_compressed = false;
    bool claims_nested = false;
};

struct ArchiveFormatProfile {
    FlagBreakdown flags;
    SizeClass archive_size_class = SizeClass::kEmpty;
    std::vector<EntryFormatProfile> entries;
    FormatLimits limits;
};

FormatLimits defaultFormatLimits();
std::vector<FieldRule> formatFieldRules();
std::vector<FlagRule> formatFlagRules();
std::vector<FlagRule> archiveFlagRules();
std::vector<FlagRule> entryFlagRules();
std::vector<FlagRule> chunkFlagRules();

std::string fieldKindName(FieldKind kind);
std::string flagDomainName(FlagDomain domain);
std::string nameClassName(NameClass value);
std::string sizeClassName(SizeClass value);
std::string describeFieldRule(const FieldRule& rule);
std::string describeFlagRule(const FlagRule& rule);
std::string formatRulesText();
std::string formatRulesJson();

NameClass classifyArchiveName(std::string_view name);
SizeClass classifySize(std::uint64_t size);
FlagBreakdown decodeKnownFlags(FlagDomain domain, std::uint8_t flags);
std::string joinFlagNames(const FlagBreakdown& breakdown);
std::uint8_t knownFlagMask(FlagDomain domain);
bool hasUnknownFlags(FlagDomain domain, std::uint8_t flags);
bool flagSet(std::uint8_t flags, std::uint8_t bit);
bool archiveFlagsAllow(const ArchiveManifest& manifest, std::uint8_t bit);
bool entryFlagsAllow(const ManifestEntry& entry, std::uint8_t bit);

EntryFormatProfile profileEntryFormat(const ManifestEntry& entry);
ArchiveFormatProfile profileArchiveFormat(const ArchiveManifest& manifest,
                                          const FormatLimits& limits = defaultFormatLimits());
std::vector<EntryFormatProfile> profileEntryTree(const ManifestEntry& entry);
std::string describeEntryProfile(const EntryFormatProfile& profile);
std::string describeArchiveProfile(const ArchiveFormatProfile& profile);

void checkArchiveFormatRules(const ArchiveManifest& manifest,
                             const FormatLimits& limits,
                             Diagnostics* diagnostics);
void checkEntryFormatRules(const ManifestEntry& entry,
                           const FormatLimits& limits,
                           Diagnostics* diagnostics);
void checkFlagRules(FlagDomain domain,
                    std::uint8_t flags,
                    SourceRange range,
                    std::string entry_name,
                    int depth,
                    Diagnostics* diagnostics);
void checkNameRules(std::string_view name,
                    SourceRange range,
                    int depth,
                    Diagnostics* diagnostics);
void checkSizeRules(const ManifestEntry& entry,
                    const FormatLimits& limits,
                    Diagnostics* diagnostics);

std::string recommendedExtensionForName(std::string_view name);
std::string formatByteSignature(ByteView view, std::size_t max_bytes = 16);
bool signatureLooksLikeArchive(ByteView view);
bool signatureLooksText(ByteView view);
bool extensionMatchesContentHint(std::string_view name, ByteView view);
std::string contentHint(ByteView view);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_FORMAT_RULES_H_
#ifndef VECTOR_ARCHIVE_EXTENSION_CATALOG_H_
#define VECTOR_ARCHIVE_EXTENSION_CATALOG_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "manifest.h"

namespace vector {

enum class FileKind {
    kUnknown,
    kText,
    kSource,
    kConfig,
    kDocument,
    kImage,
    kAudio,
    kVideo,
    kArchive,
    kExecutable,
    kDatabase,
    kFont,
    kCertificate,
    kTemporary,
};

enum class EntryNameAdvisory {
    kNone,
    kHidden,
    kBackup,
    kTemporary,
    kEditorSwap,
    kPackageMetadata,
    kSystemMetadata,
    kVeryLong,
    kNoExtension,
    kMultipleExtensions,
};

struct ExtensionRule {
    std::string extension;
    FileKind kind = FileKind::kUnknown;
    bool usually_binary = false;
    bool commonly_compressed = false;
    bool may_contain_paths = false;
    std::string description;
};

struct EntryProfile {
    std::string name;
    std::string extension;
    FileKind kind = FileKind::kUnknown;
    bool usually_binary = false;
    bool commonly_compressed = false;
    bool may_contain_paths = false;
    bool extension_known = false;
    std::vector<EntryNameAdvisory> advisories;
    std::string description;
};

const std::vector<ExtensionRule>& extensionRules();
const ExtensionRule* findExtensionRule(std::string_view extension);
EntryProfile profileEntryName(std::string_view name);
EntryProfile profileManifestEntry(const ManifestEntry& entry);
std::string fileKindName(FileKind kind);
std::string fileKindDescription(FileKind kind);
std::string advisoryName(EntryNameAdvisory advisory);
std::string advisoryDescription(EntryNameAdvisory advisory);
std::string describeEntryProfile(const EntryProfile& profile);
std::vector<EntryNameAdvisory> nameAdvisories(std::string_view name);
bool extensionUsuallyBinary(std::string_view extension);
bool extensionCommonlyCompressed(std::string_view extension);
bool extensionMayContainPaths(std::string_view extension);
bool nameLooksTemporary(std::string_view name);
bool nameLooksBackup(std::string_view name);
bool nameLooksEditorSwap(std::string_view name);
bool nameLooksSystemMetadata(std::string_view name);
bool nameLooksPackageMetadata(std::string_view name);
bool nameHasMultipleExtensions(std::string_view name);
std::string normalizedExtension(std::string_view name_or_extension);
std::uint64_t estimateEntropyScore(const std::uint8_t* data, std::size_t size);
std::string entropyLabel(std::uint64_t score);
bool bytesLookText(const std::uint8_t* data, std::size_t size);
bool bytesLookUtf8(const std::uint8_t* data, std::size_t size);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_EXTENSION_CATALOG_H_
