#include "format_rules.h"

#include <algorithm>
#include <sstream>

#include "header.h"
#include "index.h"
#include "path_utils.h"

namespace vector {
namespace {

void collectProfiles(const ManifestEntry& entry,
                     std::vector<EntryFormatProfile>* profiles) {
    profiles->push_back(profileEntryFormat(entry));
    for (const ManifestEntry& child : entry.children) {
        collectProfiles(child, profiles);
    }
}

std::uint64_t safeDenominator(std::uint64_t value) {
    return value == 0 ? 1 : value;
}

IssueCategory flagIssueCategory(FlagDomain domain) {
    switch (domain) {
    case FlagDomain::kArchive:
        return IssueCategory::kHeader;
    case FlagDomain::kEntry:
        return IssueCategory::kIndex;
    case FlagDomain::kChunk:
        return IssueCategory::kChunk;
    }
    return IssueCategory::kFormat;
}

}  // namespace

FormatLimits defaultFormatLimits() {
    FormatLimits limits;
    return limits;
}

std::vector<FieldRule> formatFieldRules() {
    return {
        {FieldKind::kMagic, "magic", "four byte VEC! file signature", 4, false},
        {FieldKind::kVersion, "version", "one byte format version", 1, false},
        {FieldKind::kFlags, "archive flags", "encoded archive-wide feature bits", 1, false},
        {FieldKind::kIndexCount, "entry count", "varint number of index records", 1, true},
        {FieldKind::kIndexEntry, "index entry", "offset, sizes, flags, and name", 5, true},
        {FieldKind::kChunkHeader, "chunk header", "encoded chunk flags and payload size", 2, true},
        {FieldKind::kChunkPayload, "chunk payload", "stored bytes for one entry", 0, true},
        {FieldKind::kExtension, "extension block", "checksum, length, and metadata payload", 2, true},
        {FieldKind::kName, "entry name", "UTF-8 path-like archive entry name", 1, true},
        {FieldKind::kVarint, "varint", "little-endian base-128 integer", 1, true},
    };
}

std::vector<FlagRule> archiveFlagRules() {
    return {
        {FlagDomain::kArchive, kFlagCompressed, "compressed",
         "one or more entries may use compressed chunk payloads"},
        {FlagDomain::kArchive, kFlagExtension, "extension",
         "an extension block follows the chunk stream"},
        {FlagDomain::kArchive, kFlagNested, "nested",
         "one or more entries may contain another archive"},
    };
}

std::vector<FlagRule> entryFlagRules() {
    return {
        {FlagDomain::kEntry, kEntryCompressed, "compressed",
         "entry payload is run-length encoded in the chunk stream"},
        {FlagDomain::kEntry, kEntryNested, "nested",
         "entry payload is expected to contain another archive"},
    };
}

std::vector<FlagRule> chunkFlagRules() {
    return {
        {FlagDomain::kChunk, kEntryCompressed, "compressed",
         "chunk payload should be decoded before use"},
        {FlagDomain::kChunk, kEntryNested, "nested",
         "chunk payload is nested archive data"},
    };
}

std::vector<FlagRule> formatFlagRules() {
    std::vector<FlagRule> out = archiveFlagRules();
    std::vector<FlagRule> entries = entryFlagRules();
    std::vector<FlagRule> chunks = chunkFlagRules();
    out.insert(out.end(), entries.begin(), entries.end());
    out.insert(out.end(), chunks.begin(), chunks.end());
    return out;
}

std::string fieldKindName(FieldKind kind) {
    switch (kind) {
    case FieldKind::kMagic:
        return "magic";
    case FieldKind::kVersion:
        return "version";
    case FieldKind::kFlags:
        return "flags";
    case FieldKind::kVarint:
        return "varint";
    case FieldKind::kIndexCount:
        return "index-count";
    case FieldKind::kIndexEntry:
        return "index-entry";
    case FieldKind::kChunkHeader:
        return "chunk-header";
    case FieldKind::kChunkPayload:
        return "chunk-payload";
    case FieldKind::kExtension:
        return "extension";
    case FieldKind::kName:
        return "name";
    }
    return "unknown";
}

std::string flagDomainName(FlagDomain domain) {
    switch (domain) {
    case FlagDomain::kArchive:
        return "archive";
    case FlagDomain::kEntry:
        return "entry";
    case FlagDomain::kChunk:
        return "chunk";
    }
    return "unknown";
}

std::string nameClassName(NameClass value) {
    switch (value) {
    case NameClass::kEmpty:
        return "empty";
    case NameClass::kSimple:
        return "simple";
    case NameClass::kDirectory:
        return "directory";
    case NameClass::kAbsolute:
        return "absolute";
    case NameClass::kRelativeWithDots:
        return "relative-with-dots";
    case NameClass::kDevice:
        return "reserved-device";
    case NameClass::kControl:
        return "control";
    case NameClass::kLong:
        return "long";
    }
    return "unknown";
}

std::string sizeClassName(SizeClass value) {
    switch (value) {
    case SizeClass::kEmpty:
        return "empty";
    case SizeClass::kTiny:
        return "tiny";
    case SizeClass::kSmall:
        return "small";
    case SizeClass::kMedium:
        return "medium";
    case SizeClass::kLarge:
        return "large";
    case SizeClass::kHuge:
        return "huge";
    }
    return "unknown";
}

std::string describeFieldRule(const FieldRule& rule) {
    std::ostringstream out;
    out << rule.name << " (" << fieldKindName(rule.kind) << "): "
        << rule.description << ", minimum " << rule.minimum_size << " byte";
    if (rule.minimum_size != 1) {
        out << "s";
    }
    if (rule.variable_size) {
        out << ", variable length";
    }
    return out.str();
}

std::string describeFlagRule(const FlagRule& rule) {
    std::ostringstream out;
    out << flagDomainName(rule.domain) << "." << rule.name
        << " bit=" << hexWord(rule.bit) << ": " << rule.description;
    return out.str();
}

std::string formatRulesText() {
    LineBuilder lines;
    lines.appendLine("Vector archive format rules");
    lines.blankLine();
    lines.appendLine("fields:");
    lines.increaseIndent(2);
    for (const FieldRule& rule : formatFieldRules()) {
        lines.appendLine(describeFieldRule(rule));
    }
    lines.decreaseIndent(2);
    lines.blankLine();
    lines.appendLine("flags:");
    lines.increaseIndent(2);
    for (const FlagRule& rule : formatFlagRules()) {
        lines.appendLine(describeFlagRule(rule));
    }
    return lines.str();
}

std::string formatRulesJson() {
    std::ostringstream out;
    out << "{\n  \"fields\": [\n";
    std::vector<FieldRule> fields = formatFieldRules();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldRule& rule = fields[i];
        if (i != 0) {
            out << ",\n";
        }
        out << "    {\"kind\":\"" << fieldKindName(rule.kind)
            << "\",\"name\":\"" << jsonEscape(rule.name)
            << "\",\"minimumSize\":" << rule.minimum_size
            << ",\"variable\":" << (rule.variable_size ? "true" : "false")
            << ",\"description\":\"" << jsonEscape(rule.description) << "\"}";
    }
    out << "\n  ],\n  \"flags\": [\n";
    std::vector<FlagRule> flags = formatFlagRules();
    for (std::size_t i = 0; i < flags.size(); ++i) {
        const FlagRule& rule = flags[i];
        if (i != 0) {
            out << ",\n";
        }
        out << "    {\"domain\":\"" << flagDomainName(rule.domain)
            << "\",\"bit\":" << static_cast<int>(rule.bit)
            << ",\"name\":\"" << jsonEscape(rule.name)
            << "\",\"description\":\"" << jsonEscape(rule.description) << "\"}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

NameClass classifyArchiveName(std::string_view name) {
    if (name.empty()) {
        return NameClass::kEmpty;
    }
    if (name.size() > defaultFormatLimits().max_name_length) {
        return NameClass::kLong;
    }
    if (containsControlByte(name)) {
        return NameClass::kControl;
    }
    if (isAbsoluteArchivePath(name)) {
        return NameClass::kAbsolute;
    }
    if (hasParentReference(name) || hasCurrentReference(name)) {
        return NameClass::kRelativeWithDots;
    }
    if (isReservedDeviceName(name)) {
        return NameClass::kDevice;
    }
    if (name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        return NameClass::kDirectory;
    }
    return NameClass::kSimple;
}

SizeClass classifySize(std::uint64_t size) {
    if (size == 0) {
        return SizeClass::kEmpty;
    }
    if (size < 1024) {
        return SizeClass::kTiny;
    }
    if (size < 64ull * 1024ull) {
        return SizeClass::kSmall;
    }
    if (size < 16ull * 1024ull * 1024ull) {
        return SizeClass::kMedium;
    }
    if (size < 1024ull * 1024ull * 1024ull) {
        return SizeClass::kLarge;
    }
    return SizeClass::kHuge;
}

FlagBreakdown decodeKnownFlags(FlagDomain domain, std::uint8_t flags) {
    FlagBreakdown breakdown;
    breakdown.domain = domain;
    breakdown.raw = flags;
    breakdown.known = static_cast<std::uint8_t>(flags & knownFlagMask(domain));
    breakdown.unknown = static_cast<std::uint8_t>(flags & ~knownFlagMask(domain));
    std::vector<FlagRule> rules;
    switch (domain) {
    case FlagDomain::kArchive:
        rules = archiveFlagRules();
        break;
    case FlagDomain::kEntry:
        rules = entryFlagRules();
        break;
    case FlagDomain::kChunk:
        rules = chunkFlagRules();
        break;
    }
    for (const FlagRule& rule : rules) {
        if ((flags & rule.bit) != 0) {
            breakdown.names.push_back(rule.name);
        }
    }
    return breakdown;
}

std::string joinFlagNames(const FlagBreakdown& breakdown) {
    if (breakdown.names.empty() && breakdown.unknown == 0) {
        return "none";
    }
    std::string out;
    for (std::size_t i = 0; i < breakdown.names.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += breakdown.names[i];
    }
    if (breakdown.unknown != 0) {
        if (!out.empty()) {
            out += ",";
        }
        out += "unknown(" + hexWord(breakdown.unknown) + ")";
    }
    return out;
}

std::uint8_t knownFlagMask(FlagDomain domain) {
    std::uint8_t mask = 0;
    std::vector<FlagRule> rules;
    switch (domain) {
    case FlagDomain::kArchive:
        rules = archiveFlagRules();
        break;
    case FlagDomain::kEntry:
        rules = entryFlagRules();
        break;
    case FlagDomain::kChunk:
        rules = chunkFlagRules();
        break;
    }
    for (const FlagRule& rule : rules) {
        mask = static_cast<std::uint8_t>(mask | rule.bit);
    }
    return mask;
}

bool hasUnknownFlags(FlagDomain domain, std::uint8_t flags) {
    return (flags & ~knownFlagMask(domain)) != 0;
}

bool flagSet(std::uint8_t flags, std::uint8_t bit) {
    return (flags & bit) != 0;
}

bool archiveFlagsAllow(const ArchiveManifest& manifest, std::uint8_t bit) {
    return flagSet(manifest.flags, bit);
}

bool entryFlagsAllow(const ManifestEntry& entry, std::uint8_t bit) {
    return flagSet(static_cast<std::uint8_t>(entry.flags), bit);
}

EntryFormatProfile profileEntryFormat(const ManifestEntry& entry) {
    EntryFormatProfile profile;
    profile.name = entry.name;
    profile.name_class = classifyArchiveName(entry.name);
    profile.packed_class = classifySize(entry.packed_size);
    profile.original_class = classifySize(entry.original_size);
    profile.flags = decodeKnownFlags(FlagDomain::kEntry,
                                     static_cast<std::uint8_t>(entry.flags));
    profile.packed_to_original =
        static_cast<double>(entry.packed_size) /
        static_cast<double>(safeDenominator(entry.original_size));
    profile.original_to_packed =
        static_cast<double>(entry.original_size) /
        static_cast<double>(safeDenominator(entry.packed_size));
    profile.has_size_mismatch =
        entry.chunk.readable && entry.chunk.payload_size != entry.packed_size;
    profile.claims_compressed = entry.compressed;
    profile.claims_nested = entry.nested;
    return profile;
}

ArchiveFormatProfile profileArchiveFormat(const ArchiveManifest& manifest,
                                          const FormatLimits& limits) {
    ArchiveFormatProfile profile;
    profile.flags = decodeKnownFlags(FlagDomain::kArchive, manifest.flags);
    profile.archive_size_class = classifySize(manifest.archive_size);
    profile.limits = limits;
    for (const ManifestEntry& entry : manifest.entries) {
        collectProfiles(entry, &profile.entries);
    }
    return profile;
}

std::vector<EntryFormatProfile> profileEntryTree(const ManifestEntry& entry) {
    std::vector<EntryFormatProfile> profiles;
    collectProfiles(entry, &profiles);
    return profiles;
}

std::string describeEntryProfile(const EntryFormatProfile& profile) {
    std::ostringstream out;
    out << profile.name << ": name=" << nameClassName(profile.name_class)
        << " packed=" << sizeClassName(profile.packed_class)
        << " original=" << sizeClassName(profile.original_class)
        << " flags=" << joinFlagNames(profile.flags);
    if (profile.claims_compressed) {
        out << " original/packed=" << ratioString(
            static_cast<std::uint64_t>(profile.original_to_packed * 1000),
            1000);
    }
    if (profile.has_size_mismatch) {
        out << " size-mismatch";
    }
    return out.str();
}

std::string describeArchiveProfile(const ArchiveFormatProfile& profile) {
    LineBuilder lines;
    lines.appendLine("archive profile:");
    lines.increaseIndent(2);
    lines.appendKeyValue("size class", sizeClassName(profile.archive_size_class));
    lines.appendKeyValue("flags", joinFlagNames(profile.flags));
    lines.appendKeyValue("entries", std::to_string(profile.entries.size()));
    for (const EntryFormatProfile& entry : profile.entries) {
        lines.appendLine(describeEntryProfile(entry));
    }
    return lines.str();
}

void checkArchiveFormatRules(const ArchiveManifest& manifest,
                             const FormatLimits& limits,
                             Diagnostics* diagnostics) {
    checkFlagRules(FlagDomain::kArchive, manifest.flags, {4, 1}, "", 0, diagnostics);
    if (manifest.archive_size > limits.max_reasonable_archive) {
        diagnostics->warning(IssueCategory::kFormat,
                             "format.archive_size",
                             "archive is larger than the configured format profile",
                             {0, static_cast<std::size_t>(manifest.archive_size)},
                             "limit is " + humanSize(limits.max_reasonable_archive));
    }
    if (manifest.extension_size > limits.max_extension_payload) {
        diagnostics->warning(IssueCategory::kExtension,
                             "format.extension_size",
                             "extension block is larger than expected",
                             {static_cast<std::size_t>(manifest.extension_offset),
                              static_cast<std::size_t>(manifest.extension_size)},
                             "limit is " + humanSize(limits.max_extension_payload));
    }
    for (const ManifestEntry& entry : manifest.entries) {
        checkEntryFormatRules(entry, limits, diagnostics);
    }
}

void checkEntryFormatRules(const ManifestEntry& entry,
                           const FormatLimits& limits,
                           Diagnostics* diagnostics) {
    checkFlagRules(FlagDomain::kEntry,
                   static_cast<std::uint8_t>(entry.flags),
                   {static_cast<std::size_t>(entry.chunk.absolute_offset), 1},
                   entry.name,
                   entry.depth,
                   diagnostics);
    checkNameRules(entry.name,
                   {0, entry.name.size()},
                   entry.depth,
                   diagnostics);
    checkSizeRules(entry, limits, diagnostics);
    for (const ManifestEntry& child : entry.children) {
        checkEntryFormatRules(child, limits, diagnostics);
    }
}

void checkFlagRules(FlagDomain domain,
                    std::uint8_t flags,
                    SourceRange range,
                    std::string entry_name,
                    int depth,
                    Diagnostics* diagnostics) {
    FlagBreakdown breakdown = decodeKnownFlags(domain, flags);
    if (breakdown.unknown != 0) {
        diagnostics->warning(flagIssueCategory(domain),
                             "format.unknown_flags",
                             flagDomainName(domain) + " flags contain unknown bits",
                             range,
                             "unknown bits: " + hexWord(breakdown.unknown),
                             std::move(entry_name),
                             depth);
    }
}

void checkNameRules(std::string_view name,
                    SourceRange range,
                    int depth,
                    Diagnostics* diagnostics) {
    switch (classifyArchiveName(name)) {
    case NameClass::kEmpty:
        diagnostics->error(IssueCategory::kPath,
                           "format.name_empty",
                           "entry name is empty",
                           range,
                           "",
                           std::string(name),
                           depth);
        break;
    case NameClass::kAbsolute:
        diagnostics->warning(IssueCategory::kPath,
                             "format.name_absolute",
                             "entry name is absolute",
                             range,
                             "",
                             std::string(name),
                             depth);
        break;
    case NameClass::kRelativeWithDots:
        diagnostics->warning(IssueCategory::kPath,
                             "format.name_dots",
                             "entry name contains dot path components",
                             range,
                             "",
                             std::string(name),
                             depth);
        break;
    case NameClass::kDevice:
        diagnostics->warning(IssueCategory::kPolicy,
                             "format.name_device",
                             "entry name uses a reserved device basename",
                             range,
                             "",
                             std::string(name),
                             depth);
        break;
    case NameClass::kControl:
        diagnostics->warning(IssueCategory::kPolicy,
                             "format.name_control",
                             "entry name contains control bytes",
                             range,
                             "",
                             std::string(name),
                             depth);
        break;
    case NameClass::kLong:
        diagnostics->warning(IssueCategory::kPath,
                             "format.name_long",
                             "entry name is longer than the format profile",
                             range,
                             "",
                             std::string(name),
                             depth);
        break;
    case NameClass::kSimple:
    case NameClass::kDirectory:
        break;
    }
}

void checkSizeRules(const ManifestEntry& entry,
                    const FormatLimits& limits,
                    Diagnostics* diagnostics) {
    if (entry.packed_size > limits.max_chunk_payload) {
        diagnostics->warning(IssueCategory::kChunk,
                             "format.packed_size",
                             "packed payload exceeds the configured chunk profile",
                             {static_cast<std::size_t>(entry.chunk.payload_offset),
                              static_cast<std::size_t>(entry.chunk.payload_size)},
                             "limit is " + humanSize(limits.max_chunk_payload),
                             entry.name,
                             entry.depth);
    }
    if (entry.chunk.readable && entry.chunk.payload_size != entry.packed_size) {
        diagnostics->error(IssueCategory::kChunk,
                           "format.payload_size",
                           "chunk payload size does not match the index",
                           {static_cast<std::size_t>(entry.chunk.payload_offset),
                            static_cast<std::size_t>(entry.chunk.payload_size)},
                           "",
                           entry.name,
                           entry.depth);
    }
    if (entry.depth > static_cast<int>(limits.max_nested_depth)) {
        diagnostics->error(IssueCategory::kNesting,
                           "format.nested_depth",
                           "entry exceeds the configured nested archive depth",
                           {static_cast<std::size_t>(entry.chunk.absolute_offset), 0},
                           "limit is " + std::to_string(limits.max_nested_depth),
                           entry.name,
                           entry.depth);
    }
}

std::string recommendedExtensionForName(std::string_view name) {
    std::string ext = lowerAscii(extensionOf(name));
    if (!ext.empty()) {
        return ext;
    }
    if (endsWith(lowerAscii(name), "readme") || endsWith(lowerAscii(name), "license")) {
        return ".txt";
    }
    return ".bin";
}

std::string formatByteSignature(ByteView view, std::size_t max_bytes) {
    const std::size_t count = std::min(view.size(), max_bytes);
    std::ostringstream out;
    out << "hex=" << hexRange(view, 0, count)
        << " ascii=\"" << asciiBytes(view, 0, count) << "\"";
    if (view.size() > count) {
        out << " ...";
    }
    return out.str();
}

bool signatureLooksLikeArchive(ByteView view) {
    return view.size() >= 4 && view.byteAt(0) == 'V' && view.byteAt(1) == 'E' &&
           view.byteAt(2) == 'C' && view.byteAt(3) == '!';
}

bool signatureLooksText(ByteView view) {
    if (view.empty()) {
        return true;
    }
    std::size_t printable = 0;
    const std::size_t sample = std::min<std::size_t>(view.size(), 256);
    for (std::size_t i = 0; i < sample; ++i) {
        const std::uint8_t byte = view.byteAt(i);
        if (isPrintableAscii(byte) || byte == '\n' || byte == '\r' || byte == '\t') {
            ++printable;
        }
    }
    return printable * 100 >= sample * 85;
}

bool extensionMatchesContentHint(std::string_view name, ByteView view) {
    const std::string ext = lowerAscii(extensionOf(name));
    if (signatureLooksLikeArchive(view)) {
        return ext == ".vec";
    }
    if (signatureLooksText(view)) {
        return ext == ".txt" || ext == ".md" || ext == ".json" ||
               ext == ".csv" || ext == ".log" || ext.empty();
    }
    return ext != ".txt" && ext != ".md" && ext != ".json" && ext != ".csv";
}

std::string contentHint(ByteView view) {
    if (signatureLooksLikeArchive(view)) {
        return "vector-archive";
    }
    if (signatureLooksText(view)) {
        return "text";
    }
    if (view.size() >= 8 && view.byteAt(0) == 0x89 && view.byteAt(1) == 'P' &&
        view.byteAt(2) == 'N' && view.byteAt(3) == 'G') {
        return "png";
    }
    if (view.size() >= 3 && view.byteAt(0) == 0xff && view.byteAt(1) == 0xd8 &&
        view.byteAt(2) == 0xff) {
        return "jpeg";
    }
    return "binary";
}

}  // namespace vector
#include "format_rules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "byte_io.h"
#include "path_utils.h"

namespace vector {
namespace {

ExtensionRule rule(std::string extension,
                   FileKind kind,
                   bool binary,
                   bool compressed,
                   bool paths,
                   std::string description) {
    ExtensionRule out;
    out.extension = std::move(extension);
    out.kind = kind;
    out.usually_binary = binary;
    out.commonly_compressed = compressed;
    out.may_contain_paths = paths;
    out.description = std::move(description);
    return out;
}

const std::vector<ExtensionRule> makeRules() {
    std::vector<ExtensionRule> rules;
    rules.reserve(220);
    rules.push_back(rule(".txt", FileKind::kText, false, false, false, "plain text"));
    rules.push_back(rule(".text", FileKind::kText, false, false, false, "plain text"));
    rules.push_back(rule(".md", FileKind::kText, false, false, false, "markdown document"));
    rules.push_back(rule(".markdown", FileKind::kText, false, false, false, "markdown document"));
    rules.push_back(rule(".rst", FileKind::kText, false, false, false, "reStructuredText document"));
    rules.push_back(rule(".adoc", FileKind::kText, false, false, false, "AsciiDoc document"));
    rules.push_back(rule(".csv", FileKind::kText, false, false, false, "comma separated values"));
    rules.push_back(rule(".tsv", FileKind::kText, false, false, false, "tab separated values"));
    rules.push_back(rule(".log", FileKind::kText, false, false, false, "log text"));
    rules.push_back(rule(".patch", FileKind::kText, false, false, true, "source patch"));
    rules.push_back(rule(".diff", FileKind::kText, false, false, true, "source diff"));
    rules.push_back(rule(".json", FileKind::kConfig, false, false, false, "JSON data"));
    rules.push_back(rule(".jsonl", FileKind::kConfig, false, false, false, "JSON lines data"));
    rules.push_back(rule(".yaml", FileKind::kConfig, false, false, false, "YAML data"));
    rules.push_back(rule(".yml", FileKind::kConfig, false, false, false, "YAML data"));
    rules.push_back(rule(".toml", FileKind::kConfig, false, false, false, "TOML configuration"));
    rules.push_back(rule(".ini", FileKind::kConfig, false, false, false, "INI configuration"));
    rules.push_back(rule(".cfg", FileKind::kConfig, false, false, false, "configuration file"));
    rules.push_back(rule(".conf", FileKind::kConfig, false, false, false, "configuration file"));
    rules.push_back(rule(".xml", FileKind::kConfig, false, false, false, "XML document"));
    rules.push_back(rule(".html", FileKind::kDocument, false, false, false, "HTML document"));
    rules.push_back(rule(".htm", FileKind::kDocument, false, false, false, "HTML document"));
    rules.push_back(rule(".css", FileKind::kSource, false, false, false, "CSS stylesheet"));
    rules.push_back(rule(".js", FileKind::kSource, false, false, false, "JavaScript source"));
    rules.push_back(rule(".mjs", FileKind::kSource, false, false, false, "JavaScript module"));
    rules.push_back(rule(".cjs", FileKind::kSource, false, false, false, "JavaScript module"));
    rules.push_back(rule(".ts", FileKind::kSource, false, false, false, "TypeScript source"));
    rules.push_back(rule(".tsx", FileKind::kSource, false, false, false, "TypeScript JSX source"));
    rules.push_back(rule(".jsx", FileKind::kSource, false, false, false, "JavaScript JSX source"));
    rules.push_back(rule(".c", FileKind::kSource, false, false, false, "C source"));
    rules.push_back(rule(".h", FileKind::kSource, false, false, false, "C or C++ header"));
    rules.push_back(rule(".cc", FileKind::kSource, false, false, false, "C++ source"));
    rules.push_back(rule(".cpp", FileKind::kSource, false, false, false, "C++ source"));
    rules.push_back(rule(".cxx", FileKind::kSource, false, false, false, "C++ source"));
    rules.push_back(rule(".hpp", FileKind::kSource, false, false, false, "C++ header"));
    rules.push_back(rule(".hh", FileKind::kSource, false, false, false, "C++ header"));
    rules.push_back(rule(".hxx", FileKind::kSource, false, false, false, "C++ header"));
    rules.push_back(rule(".ipp", FileKind::kSource, false, false, false, "C++ inline implementation"));
    rules.push_back(rule(".rs", FileKind::kSource, false, false, false, "Rust source"));
    rules.push_back(rule(".go", FileKind::kSource, false, false, false, "Go source"));
    rules.push_back(rule(".java", FileKind::kSource, false, false, false, "Java source"));
    rules.push_back(rule(".kt", FileKind::kSource, false, false, false, "Kotlin source"));
    rules.push_back(rule(".kts", FileKind::kSource, false, false, false, "Kotlin script"));
    rules.push_back(rule(".swift", FileKind::kSource, false, false, false, "Swift source"));
    rules.push_back(rule(".m", FileKind::kSource, false, false, false, "Objective-C source"));
    rules.push_back(rule(".mm", FileKind::kSource, false, false, false, "Objective-C++ source"));
    rules.push_back(rule(".py", FileKind::kSource, false, false, false, "Python source"));
    rules.push_back(rule(".pyw", FileKind::kSource, false, false, false, "Python source"));
    rules.push_back(rule(".rb", FileKind::kSource, false, false, false, "Ruby source"));
    rules.push_back(rule(".php", FileKind::kSource, false, false, false, "PHP source"));
    rules.push_back(rule(".pl", FileKind::kSource, false, false, false, "Perl source"));
    rules.push_back(rule(".pm", FileKind::kSource, false, false, false, "Perl module"));
    rules.push_back(rule(".lua", FileKind::kSource, false, false, false, "Lua source"));
    rules.push_back(rule(".sh", FileKind::kSource, false, false, false, "shell script"));
    rules.push_back(rule(".bash", FileKind::kSource, false, false, false, "Bash script"));
    rules.push_back(rule(".zsh", FileKind::kSource, false, false, false, "Zsh script"));
    rules.push_back(rule(".fish", FileKind::kSource, false, false, false, "Fish script"));
    rules.push_back(rule(".ps1", FileKind::kSource, false, false, false, "PowerShell script"));
    rules.push_back(rule(".bat", FileKind::kSource, false, false, false, "batch script"));
    rules.push_back(rule(".cmd", FileKind::kSource, false, false, false, "command script"));
    rules.push_back(rule(".sql", FileKind::kSource, false, false, false, "SQL script"));
    rules.push_back(rule(".r", FileKind::kSource, false, false, false, "R source"));
    rules.push_back(rule(".jl", FileKind::kSource, false, false, false, "Julia source"));
    rules.push_back(rule(".scala", FileKind::kSource, false, false, false, "Scala source"));
    rules.push_back(rule(".clj", FileKind::kSource, false, false, false, "Clojure source"));
    rules.push_back(rule(".cljs", FileKind::kSource, false, false, false, "ClojureScript source"));
    rules.push_back(rule(".erl", FileKind::kSource, false, false, false, "Erlang source"));
    rules.push_back(rule(".ex", FileKind::kSource, false, false, false, "Elixir source"));
    rules.push_back(rule(".exs", FileKind::kSource, false, false, false, "Elixir script"));
    rules.push_back(rule(".fs", FileKind::kSource, false, false, false, "F# source"));
    rules.push_back(rule(".fsx", FileKind::kSource, false, false, false, "F# script"));
    rules.push_back(rule(".vb", FileKind::kSource, false, false, false, "Visual Basic source"));
    rules.push_back(rule(".cs", FileKind::kSource, false, false, false, "C# source"));
    rules.push_back(rule(".dart", FileKind::kSource, false, false, false, "Dart source"));
    rules.push_back(rule(".zig", FileKind::kSource, false, false, false, "Zig source"));
    rules.push_back(rule(".nim", FileKind::kSource, false, false, false, "Nim source"));
    rules.push_back(rule(".v", FileKind::kSource, false, false, false, "Verilog source"));
    rules.push_back(rule(".sv", FileKind::kSource, false, false, false, "SystemVerilog source"));
    rules.push_back(rule(".vhd", FileKind::kSource, false, false, false, "VHDL source"));
    rules.push_back(rule(".vhdl", FileKind::kSource, false, false, false, "VHDL source"));
    rules.push_back(rule(".asm", FileKind::kSource, false, false, false, "assembly source"));
    rules.push_back(rule(".s", FileKind::kSource, false, false, false, "assembly source"));
    rules.push_back(rule(".cmake", FileKind::kSource, false, false, false, "CMake script"));
    rules.push_back(rule(".make", FileKind::kSource, false, false, false, "Make build script"));
    rules.push_back(rule(".mk", FileKind::kSource, false, false, false, "Make include"));
    rules.push_back(rule(".ninja", FileKind::kSource, false, false, false, "Ninja build file"));
    rules.push_back(rule(".pdf", FileKind::kDocument, true, true, false, "PDF document"));
    rules.push_back(rule(".doc", FileKind::kDocument, true, false, false, "Word document"));
    rules.push_back(rule(".docx", FileKind::kDocument, true, true, true, "Office document"));
    rules.push_back(rule(".xls", FileKind::kDocument, true, false, false, "Excel spreadsheet"));
    rules.push_back(rule(".xlsx", FileKind::kDocument, true, true, true, "Office spreadsheet"));
    rules.push_back(rule(".ppt", FileKind::kDocument, true, false, false, "PowerPoint deck"));
    rules.push_back(rule(".pptx", FileKind::kDocument, true, true, true, "Office presentation"));
    rules.push_back(rule(".odt", FileKind::kDocument, true, true, true, "OpenDocument text"));
    rules.push_back(rule(".ods", FileKind::kDocument, true, true, true, "OpenDocument spreadsheet"));
    rules.push_back(rule(".odp", FileKind::kDocument, true, true, true, "OpenDocument presentation"));
    rules.push_back(rule(".rtf", FileKind::kDocument, false, false, false, "rich text document"));
    rules.push_back(rule(".epub", FileKind::kDocument, true, true, true, "EPUB book"));
    rules.push_back(rule(".png", FileKind::kImage, true, true, false, "PNG image"));
    rules.push_back(rule(".jpg", FileKind::kImage, true, true, false, "JPEG image"));
    rules.push_back(rule(".jpeg", FileKind::kImage, true, true, false, "JPEG image"));
    rules.push_back(rule(".gif", FileKind::kImage, true, true, false, "GIF image"));
    rules.push_back(rule(".webp", FileKind::kImage, true, true, false, "WebP image"));
    rules.push_back(rule(".bmp", FileKind::kImage, true, false, false, "bitmap image"));
    rules.push_back(rule(".tif", FileKind::kImage, true, false, false, "TIFF image"));
    rules.push_back(rule(".tiff", FileKind::kImage, true, false, false, "TIFF image"));
    rules.push_back(rule(".ico", FileKind::kImage, true, false, false, "icon image"));
    rules.push_back(rule(".svg", FileKind::kImage, false, false, false, "SVG image"));
    rules.push_back(rule(".heic", FileKind::kImage, true, true, false, "HEIC image"));
    rules.push_back(rule(".avif", FileKind::kImage, true, true, false, "AVIF image"));
    rules.push_back(rule(".psd", FileKind::kImage, true, false, false, "Photoshop document"));
    rules.push_back(rule(".ai", FileKind::kImage, true, false, false, "Illustrator document"));
    rules.push_back(rule(".xcf", FileKind::kImage, true, false, false, "GIMP image"));
    rules.push_back(rule(".mp3", FileKind::kAudio, true, true, false, "MP3 audio"));
    rules.push_back(rule(".wav", FileKind::kAudio, true, false, false, "WAV audio"));
    rules.push_back(rule(".flac", FileKind::kAudio, true, true, false, "FLAC audio"));
    rules.push_back(rule(".ogg", FileKind::kAudio, true, true, false, "Ogg audio"));
    rules.push_back(rule(".opus", FileKind::kAudio, true, true, false, "Opus audio"));
    rules.push_back(rule(".m4a", FileKind::kAudio, true, true, false, "MPEG-4 audio"));
    rules.push_back(rule(".aac", FileKind::kAudio, true, true, false, "AAC audio"));
    rules.push_back(rule(".aiff", FileKind::kAudio, true, false, false, "AIFF audio"));
    rules.push_back(rule(".mid", FileKind::kAudio, true, false, false, "MIDI file"));
    rules.push_back(rule(".midi", FileKind::kAudio, true, false, false, "MIDI file"));
    rules.push_back(rule(".mp4", FileKind::kVideo, true, true, false, "MP4 video"));
    rules.push_back(rule(".m4v", FileKind::kVideo, true, true, false, "MPEG-4 video"));
    rules.push_back(rule(".mkv", FileKind::kVideo, true, true, false, "Matroska video"));
    rules.push_back(rule(".webm", FileKind::kVideo, true, true, false, "WebM video"));
    rules.push_back(rule(".mov", FileKind::kVideo, true, true, false, "QuickTime video"));
    rules.push_back(rule(".avi", FileKind::kVideo, true, false, false, "AVI video"));
    rules.push_back(rule(".wmv", FileKind::kVideo, true, true, false, "Windows Media video"));
    rules.push_back(rule(".flv", FileKind::kVideo, true, true, false, "Flash video"));
    rules.push_back(rule(".mpeg", FileKind::kVideo, true, true, false, "MPEG video"));
    rules.push_back(rule(".mpg", FileKind::kVideo, true, true, false, "MPEG video"));
    rules.push_back(rule(".zip", FileKind::kArchive, true, true, true, "ZIP archive"));
    rules.push_back(rule(".jar", FileKind::kArchive, true, true, true, "Java archive"));
    rules.push_back(rule(".war", FileKind::kArchive, true, true, true, "web application archive"));
    rules.push_back(rule(".ear", FileKind::kArchive, true, true, true, "enterprise archive"));
    rules.push_back(rule(".apk", FileKind::kArchive, true, true, true, "Android package"));
    rules.push_back(rule(".ipa", FileKind::kArchive, true, true, true, "iOS package"));
    rules.push_back(rule(".tar", FileKind::kArchive, true, false, true, "tar archive"));
    rules.push_back(rule(".tgz", FileKind::kArchive, true, true, true, "gzip tar archive"));
    rules.push_back(rule(".gz", FileKind::kArchive, true, true, false, "gzip stream"));
    rules.push_back(rule(".bz2", FileKind::kArchive, true, true, false, "bzip2 stream"));
    rules.push_back(rule(".xz", FileKind::kArchive, true, true, false, "xz stream"));
    rules.push_back(rule(".zst", FileKind::kArchive, true, true, false, "zstd stream"));
    rules.push_back(rule(".lz", FileKind::kArchive, true, true, false, "lzip stream"));
    rules.push_back(rule(".lz4", FileKind::kArchive, true, true, false, "lz4 stream"));
    rules.push_back(rule(".br", FileKind::kArchive, true, true, false, "brotli stream"));
    rules.push_back(rule(".7z", FileKind::kArchive, true, true, true, "7-Zip archive"));
    rules.push_back(rule(".rar", FileKind::kArchive, true, true, true, "RAR archive"));
    rules.push_back(rule(".cab", FileKind::kArchive, true, true, true, "cabinet archive"));
    rules.push_back(rule(".ar", FileKind::kArchive, true, false, true, "ar archive"));
    rules.push_back(rule(".cpio", FileKind::kArchive, true, false, true, "cpio archive"));
    rules.push_back(rule(".deb", FileKind::kArchive, true, true, true, "Debian package"));
    rules.push_back(rule(".rpm", FileKind::kArchive, true, true, true, "RPM package"));
    rules.push_back(rule(".dmg", FileKind::kArchive, true, true, true, "Apple disk image"));
    rules.push_back(rule(".iso", FileKind::kArchive, true, false, true, "ISO disk image"));
    rules.push_back(rule(".vec", FileKind::kArchive, true, false, true, "Vector archive"));
    rules.push_back(rule(".exe", FileKind::kExecutable, true, false, false, "Windows executable"));
    rules.push_back(rule(".dll", FileKind::kExecutable, true, false, false, "Windows library"));
    rules.push_back(rule(".so", FileKind::kExecutable, true, false, false, "shared object"));
    rules.push_back(rule(".dylib", FileKind::kExecutable, true, false, false, "dynamic library"));
    rules.push_back(rule(".bin", FileKind::kExecutable, true, false, false, "binary data"));
    rules.push_back(rule(".appimage", FileKind::kExecutable, true, true, true, "AppImage package"));
    rules.push_back(rule(".msi", FileKind::kExecutable, true, true, true, "Windows installer"));
    rules.push_back(rule(".wasm", FileKind::kExecutable, true, false, false, "WebAssembly module"));
    rules.push_back(rule(".class", FileKind::kExecutable, true, false, false, "Java class file"));
    rules.push_back(rule(".pyc", FileKind::kExecutable, true, false, false, "Python bytecode"));
    rules.push_back(rule(".pyo", FileKind::kExecutable, true, false, false, "Python bytecode"));
    rules.push_back(rule(".sqlite", FileKind::kDatabase, true, false, false, "SQLite database"));
    rules.push_back(rule(".sqlite3", FileKind::kDatabase, true, false, false, "SQLite database"));
    rules.push_back(rule(".db", FileKind::kDatabase, true, false, false, "database file"));
    rules.push_back(rule(".mdb", FileKind::kDatabase, true, false, false, "Access database"));
    rules.push_back(rule(".accdb", FileKind::kDatabase, true, false, false, "Access database"));
    rules.push_back(rule(".parquet", FileKind::kDatabase, true, true, false, "Parquet data"));
    rules.push_back(rule(".orc", FileKind::kDatabase, true, true, false, "ORC data"));
    rules.push_back(rule(".avro", FileKind::kDatabase, true, false, false, "Avro data"));
    rules.push_back(rule(".ttf", FileKind::kFont, true, false, false, "TrueType font"));
    rules.push_back(rule(".otf", FileKind::kFont, true, false, false, "OpenType font"));
    rules.push_back(rule(".woff", FileKind::kFont, true, true, false, "WOFF font"));
    rules.push_back(rule(".woff2", FileKind::kFont, true, true, false, "WOFF2 font"));
    rules.push_back(rule(".pem", FileKind::kCertificate, false, false, false, "PEM certificate or key"));
    rules.push_back(rule(".crt", FileKind::kCertificate, false, false, false, "certificate"));
    rules.push_back(rule(".cer", FileKind::kCertificate, false, false, false, "certificate"));
    rules.push_back(rule(".der", FileKind::kCertificate, true, false, false, "DER certificate"));
    rules.push_back(rule(".p12", FileKind::kCertificate, true, false, false, "PKCS#12 bundle"));
    rules.push_back(rule(".pfx", FileKind::kCertificate, true, false, false, "PKCS#12 bundle"));
    rules.push_back(rule(".key", FileKind::kCertificate, false, false, false, "key material"));
    rules.push_back(rule(".tmp", FileKind::kTemporary, true, false, false, "temporary file"));
    rules.push_back(rule(".temp", FileKind::kTemporary, true, false, false, "temporary file"));
    rules.push_back(rule(".bak", FileKind::kTemporary, true, false, false, "backup file"));
    rules.push_back(rule(".backup", FileKind::kTemporary, true, false, false, "backup file"));
    rules.push_back(rule(".old", FileKind::kTemporary, true, false, false, "old backup file"));
    rules.push_back(rule(".orig", FileKind::kTemporary, true, false, false, "original backup file"));
    rules.push_back(rule(".swp", FileKind::kTemporary, true, false, false, "editor swap file"));
    rules.push_back(rule(".swo", FileKind::kTemporary, true, false, false, "editor swap file"));
    return rules;
}

}  // namespace

const std::vector<ExtensionRule>& extensionRules() {
    static const std::vector<ExtensionRule> rules = makeRules();
    return rules;
}

const ExtensionRule* findExtensionRule(std::string_view extension) {
    const std::string normalized = normalizedExtension(extension);
    for (const ExtensionRule& rule_item : extensionRules()) {
        if (rule_item.extension == normalized) {
            return &rule_item;
        }
    }
    return nullptr;
}

EntryProfile profileEntryName(std::string_view name) {
    EntryProfile profile;
    profile.name = std::string(name);
    profile.extension = normalizedExtension(name);
    if (const ExtensionRule* rule_item = findExtensionRule(profile.extension)) {
        profile.kind = rule_item->kind;
        profile.usually_binary = rule_item->usually_binary;
        profile.commonly_compressed = rule_item->commonly_compressed;
        profile.may_contain_paths = rule_item->may_contain_paths;
        profile.extension_known = true;
        profile.description = rule_item->description;
    }
    profile.advisories = nameAdvisories(name);
    return profile;
}

EntryProfile profileManifestEntry(const ManifestEntry& entry) {
    return profileEntryName(entry.name);
}

std::string fileKindName(FileKind kind) {
    switch (kind) {
    case FileKind::kUnknown:
        return "unknown";
    case FileKind::kText:
        return "text";
    case FileKind::kSource:
        return "source";
    case FileKind::kConfig:
        return "config";
    case FileKind::kDocument:
        return "document";
    case FileKind::kImage:
        return "image";
    case FileKind::kAudio:
        return "audio";
    case FileKind::kVideo:
        return "video";
    case FileKind::kArchive:
        return "archive";
    case FileKind::kExecutable:
        return "executable";
    case FileKind::kDatabase:
        return "database";
    case FileKind::kFont:
        return "font";
    case FileKind::kCertificate:
        return "certificate";
    case FileKind::kTemporary:
        return "temporary";
    }
    return "unknown";
}

std::string fileKindDescription(FileKind kind) {
    switch (kind) {
    case FileKind::kUnknown:
        return "unrecognized file type";
    case FileKind::kText:
        return "human-readable text";
    case FileKind::kSource:
        return "program source or build instructions";
    case FileKind::kConfig:
        return "configuration or structured text";
    case FileKind::kDocument:
        return "document or office file";
    case FileKind::kImage:
        return "image asset";
    case FileKind::kAudio:
        return "audio media";
    case FileKind::kVideo:
        return "video media";
    case FileKind::kArchive:
        return "container or compressed archive";
    case FileKind::kExecutable:
        return "executable or loadable binary";
    case FileKind::kDatabase:
        return "database or tabular storage";
    case FileKind::kFont:
        return "font asset";
    case FileKind::kCertificate:
        return "certificate, key, or trust material";
    case FileKind::kTemporary:
        return "temporary or backup artifact";
    }
    return "unrecognized file type";
}

std::string advisoryName(EntryNameAdvisory advisory) {
    switch (advisory) {
    case EntryNameAdvisory::kNone:
        return "none";
    case EntryNameAdvisory::kHidden:
        return "hidden";
    case EntryNameAdvisory::kBackup:
        return "backup";
    case EntryNameAdvisory::kTemporary:
        return "temporary";
    case EntryNameAdvisory::kEditorSwap:
        return "editor-swap";
    case EntryNameAdvisory::kPackageMetadata:
        return "package-metadata";
    case EntryNameAdvisory::kSystemMetadata:
        return "system-metadata";
    case EntryNameAdvisory::kVeryLong:
        return "very-long";
    case EntryNameAdvisory::kNoExtension:
        return "no-extension";
    case EntryNameAdvisory::kMultipleExtensions:
        return "multiple-extensions";
    }
    return "unknown";
}

std::string advisoryDescription(EntryNameAdvisory advisory) {
    switch (advisory) {
    case EntryNameAdvisory::kNone:
        return "no naming advisory";
    case EntryNameAdvisory::kHidden:
        return "name is hidden on Unix-like systems";
    case EntryNameAdvisory::kBackup:
        return "name looks like a backup artifact";
    case EntryNameAdvisory::kTemporary:
        return "name looks temporary";
    case EntryNameAdvisory::kEditorSwap:
        return "name looks like an editor swap file";
    case EntryNameAdvisory::kPackageMetadata:
        return "name belongs to package metadata";
    case EntryNameAdvisory::kSystemMetadata:
        return "name belongs to operating system metadata";
    case EntryNameAdvisory::kVeryLong:
        return "name is unusually long";
    case EntryNameAdvisory::kNoExtension:
        return "name has no extension";
    case EntryNameAdvisory::kMultipleExtensions:
        return "name has multiple extensions";
    }
    return "unknown advisory";
}

std::string describeEntryProfile(const EntryProfile& profile) {
    std::string out = profile.name + ": " + fileKindName(profile.kind);
    if (!profile.extension.empty()) {
        out += " " + profile.extension;
    }
    if (!profile.description.empty()) {
        out += " (" + profile.description + ")";
    }
    if (!profile.advisories.empty()) {
        out += " advisories=";
        for (std::size_t i = 0; i < profile.advisories.size(); ++i) {
            if (i != 0) {
                out += ",";
            }
            out += advisoryName(profile.advisories[i]);
        }
    }
    return out;
}

std::vector<EntryNameAdvisory> nameAdvisories(std::string_view name) {
    std::vector<EntryNameAdvisory> advisories;
    const std::string base = basenameOf(name);
    if (!base.empty() && base[0] == '.') {
        advisories.push_back(EntryNameAdvisory::kHidden);
    }
    if (nameLooksBackup(name)) {
        advisories.push_back(EntryNameAdvisory::kBackup);
    }
    if (nameLooksTemporary(name)) {
        advisories.push_back(EntryNameAdvisory::kTemporary);
    }
    if (nameLooksEditorSwap(name)) {
        advisories.push_back(EntryNameAdvisory::kEditorSwap);
    }
    if (nameLooksPackageMetadata(name)) {
        advisories.push_back(EntryNameAdvisory::kPackageMetadata);
    }
    if (nameLooksSystemMetadata(name)) {
        advisories.push_back(EntryNameAdvisory::kSystemMetadata);
    }
    if (name.size() > 180) {
        advisories.push_back(EntryNameAdvisory::kVeryLong);
    }
    if (extensionOf(name).empty()) {
        advisories.push_back(EntryNameAdvisory::kNoExtension);
    }
    if (nameHasMultipleExtensions(name)) {
        advisories.push_back(EntryNameAdvisory::kMultipleExtensions);
    }
    return advisories;
}

bool extensionUsuallyBinary(std::string_view extension) {
    const ExtensionRule* rule_item = findExtensionRule(extension);
    return rule_item != nullptr && rule_item->usually_binary;
}

bool extensionCommonlyCompressed(std::string_view extension) {
    const ExtensionRule* rule_item = findExtensionRule(extension);
    return rule_item != nullptr && rule_item->commonly_compressed;
}

bool extensionMayContainPaths(std::string_view extension) {
    const ExtensionRule* rule_item = findExtensionRule(extension);
    return rule_item != nullptr && rule_item->may_contain_paths;
}

bool nameLooksTemporary(std::string_view name) {
    const std::string lower = lowerAscii(basenameOf(name));
    return startsWith(lower, "tmp") || startsWith(lower, "temp") ||
           endsWith(lower, ".tmp") || endsWith(lower, ".temp") ||
           endsWith(lower, "~");
}

bool nameLooksBackup(std::string_view name) {
    const std::string lower = lowerAscii(basenameOf(name));
    return endsWith(lower, ".bak") || endsWith(lower, ".backup") ||
           endsWith(lower, ".old") || endsWith(lower, ".orig") ||
           endsWith(lower, "~");
}

bool nameLooksEditorSwap(std::string_view name) {
    const std::string lower = lowerAscii(basenameOf(name));
    return endsWith(lower, ".swp") || endsWith(lower, ".swo") ||
           startsWith(lower, ".#") || startsWith(lower, "#");
}

bool nameLooksSystemMetadata(std::string_view name) {
    const std::string lower = lowerAscii(basenameOf(name));
    return lower == ".ds_store" || lower == "thumbs.db" ||
           lower == "desktop.ini" || lower == "__macosx";
}

bool nameLooksPackageMetadata(std::string_view name) {
    const std::string normalized = lowerAscii(normalizeSeparators(name));
    return startsWith(normalized, "meta-inf/") ||
           startsWith(normalized, "package/") ||
           startsWith(normalized, "debian/") ||
           startsWith(normalized, "rpm/") ||
           normalized == "package.json" ||
           normalized == "package-lock.json" ||
           normalized == "cargo.toml" ||
           normalized == "go.mod";
}

bool nameHasMultipleExtensions(std::string_view name) {
    const std::string base = basenameOf(name);
    if (base.empty() || base[0] == '.') {
        return false;
    }
    return std::count(base.begin(), base.end(), '.') >= 2;
}

std::string normalizedExtension(std::string_view name_or_extension) {
    std::string ext = std::string(name_or_extension);
    if (ext.find('/') != std::string::npos || ext.find('\\') != std::string::npos ||
        ext.find('.') != 0) {
        ext = extensionOf(ext);
    }
    ext = lowerAscii(ext);
    if (!ext.empty() && ext[0] != '.') {
        ext.insert(ext.begin(), '.');
    }
    return ext;
}

std::uint64_t estimateEntropyScore(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return 0;
    }
    std::array<std::size_t, 256> counts{};
    for (std::size_t i = 0; i < size; ++i) {
        ++counts[data[i]];
    }
    double entropy = 0.0;
    for (std::size_t count : counts) {
        if (count == 0) {
            continue;
        }
        const double p = static_cast<double>(count) / static_cast<double>(size);
        entropy -= p * std::log2(p);
    }
    return static_cast<std::uint64_t>(entropy * 1000.0);
}

std::string entropyLabel(std::uint64_t score) {
    if (score < 2000) {
        return "very-low";
    }
    if (score < 4000) {
        return "low";
    }
    if (score < 6500) {
        return "medium";
    }
    if (score < 7600) {
        return "high";
    }
    return "very-high";
}

bool bytesLookText(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        return false;
    }
    if (size == 0) {
        return true;
    }
    std::size_t printable = 0;
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t byte = data[i];
        if (byte == '\n' || byte == '\r' || byte == '\t' || isPrintableAscii(byte)) {
            ++printable;
        }
    }
    return printable * 100 >= size * 92;
}

bool bytesLookUtf8(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        return false;
    }
    std::size_t i = 0;
    while (i < size) {
        const std::uint8_t byte = data[i];
        if (byte <= 0x7f) {
            ++i;
            continue;
        }
        std::size_t needed = 0;
        if ((byte & 0xe0) == 0xc0) {
            needed = 1;
        } else if ((byte & 0xf0) == 0xe0) {
            needed = 2;
        } else if ((byte & 0xf8) == 0xf0) {
            needed = 3;
        } else {
            return false;
        }
        if (i + needed >= size) {
            return false;
        }
        for (std::size_t j = 1; j <= needed; ++j) {
            if ((data[i + j] & 0xc0) != 0x80) {
                return false;
            }
        }
        i += needed + 1;
    }
    return true;
}

}  // namespace vector
