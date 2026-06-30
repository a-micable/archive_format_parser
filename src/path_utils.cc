#include "path_utils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "byte_io.h"

namespace vector {
namespace {

std::string trimTrailing(std::string value, char ch) {
    while (!value.empty() && value.back() == ch) {
        value.pop_back();
    }
    return value;
}

std::uint64_t stableNameHash(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char ch : value) {
        hash ^= ch;
        hash *= 1099511628211ull;
    }
    return hash;
}

PathComponentKind classifyComponent(std::string_view component,
                                    const PathPolicy& policy) {
    if (component.empty()) {
        return PathComponentKind::kEmpty;
    }
    if (component == ".") {
        return PathComponentKind::kCurrentDirectory;
    }
    if (component == "..") {
        return PathComponentKind::kParentDirectory;
    }
    if (hasDrivePrefix(component)) {
        return PathComponentKind::kDrive;
    }
    if (!policy.allow_reserved_devices && isReservedDeviceName(component)) {
        return PathComponentKind::kReservedDevice;
    }
    if (!policy.allow_control_bytes && containsControlByte(component)) {
        return PathComponentKind::kControl;
    }
    if (!policy.allow_trailing_spaces && hasTrailingSpace(component)) {
        return PathComponentKind::kTrailingSpace;
    }
    if (!policy.allow_trailing_dots && hasTrailingDot(component)) {
        return PathComponentKind::kTrailingDot;
    }
    if (!policy.allow_wildcards && hasWildcard(component)) {
        return PathComponentKind::kWildcard;
    }
    return PathComponentKind::kNormal;
}

std::string replacementForComponent(std::string component,
                                    PathComponentKind kind,
                                    const PathPolicy& policy) {
    switch (kind) {
    case PathComponentKind::kNormal:
        return component;
    case PathComponentKind::kEmpty:
    case PathComponentKind::kCurrentDirectory:
    case PathComponentKind::kParentDirectory:
    case PathComponentKind::kRoot:
    case PathComponentKind::kDrive:
        return "";
    case PathComponentKind::kReservedDevice:
        return "_" + component;
    case PathComponentKind::kTrailingSpace:
        if (!policy.allow_trailing_spaces) {
            return trimTrailing(component, ' ');
        }
        return component;
    case PathComponentKind::kTrailingDot:
        if (!policy.allow_trailing_dots) {
            return trimTrailing(component, '.');
        }
        return component;
    case PathComponentKind::kControl:
    case PathComponentKind::kWildcard:
        return replaceUnsafeCharacters(component, '_');
    }
    return component;
}

bool kindAllowed(PathComponentKind kind, const PathPolicy& policy) {
    switch (kind) {
    case PathComponentKind::kNormal:
        return true;
    case PathComponentKind::kEmpty:
        return policy.allow_empty;
    case PathComponentKind::kCurrentDirectory:
        return policy.allow_current;
    case PathComponentKind::kParentDirectory:
        return policy.allow_parent;
    case PathComponentKind::kRoot:
        return policy.allow_absolute;
    case PathComponentKind::kDrive:
        return policy.allow_drive_names;
    case PathComponentKind::kReservedDevice:
        return policy.allow_reserved_devices;
    case PathComponentKind::kTrailingSpace:
        return policy.allow_trailing_spaces;
    case PathComponentKind::kTrailingDot:
        return policy.allow_trailing_dots;
    case PathComponentKind::kControl:
        return policy.allow_control_bytes;
    case PathComponentKind::kWildcard:
        return policy.allow_wildcards;
    }
    return false;
}

void addPathIssue(PathAnalysis* analysis,
                  PathComponentKind kind,
                  std::string_view component,
                  std::size_t offset) {
    analysis->diagnostics.warning(
        pathIssueCategory(kind),
        "path." + pathComponentKindName(kind),
        "entry name contains " + pathComponentKindName(kind),
        {offset, component.size()},
        "component: " + std::string(component),
        analysis->original);
}

}  // namespace

CollisionResolver::CollisionResolver(PathPolicy policy)
    : policy_(std::move(policy)) {}

CollisionResult CollisionResolver::assign(std::string_view requested) {
    CollisionResult result;
    result.requested = std::string(requested);
    PathAnalysis analysis = analyzePath(requested, policy_);
    std::string normalized = analysis.normalized.empty() ? "unnamed" : analysis.normalized;
    std::string candidate = normalized;
    std::string key = collisionKey(candidate);
    std::size_t attempt = 0;
    while (keys_.find(key) != keys_.end()) {
        result.collided = true;
        ++attempt;
        candidate = makeCandidate(normalized, attempt);
        key = collisionKey(candidate);
    }
    keys_.insert(key);
    assignments_[result.requested] = candidate;
    result.assigned = candidate;
    result.attempt = attempt;
    return result;
}

bool CollisionResolver::contains(std::string_view normalized) const {
    return keys_.find(collisionKey(normalized)) != keys_.end();
}

std::size_t CollisionResolver::size() const {
    return assignments_.size();
}

void CollisionResolver::clear() {
    assignments_.clear();
    keys_.clear();
}

const std::map<std::string, std::string>& CollisionResolver::assignments() const {
    return assignments_;
}

std::string CollisionResolver::collisionKey(std::string_view value) const {
    if (policy_.lowercase_for_collision) {
        return canonicalCollisionKey(value);
    }
    return std::string(value);
}

std::string CollisionResolver::makeCandidate(std::string_view value,
                                             std::size_t attempt) const {
    switch (policy_.collision_strategy) {
    case CollisionStrategy::kKeepFirst:
        return std::string(value);
    case CollisionStrategy::kAppendNumber:
        return appendNumberSuffix(value, attempt);
    case CollisionStrategy::kAppendHash:
        return appendHashSuffix(std::string(value) + "#" + std::to_string(attempt));
    }
    return appendNumberSuffix(value, attempt);
}

PathPolicy defaultArchivePathPolicy() {
    PathPolicy policy;
    return policy;
}

PathPolicy permissivePathPolicy() {
    PathPolicy policy;
    policy.allow_empty = true;
    policy.allow_absolute = true;
    policy.allow_parent = true;
    policy.allow_current = true;
    policy.allow_drive_names = true;
    policy.allow_control_bytes = true;
    policy.allow_reserved_devices = true;
    policy.allow_trailing_spaces = true;
    policy.allow_trailing_dots = true;
    policy.allow_wildcards = true;
    policy.max_components = 4096;
    policy.max_component_length = 4096;
    policy.max_path_length = 16384;
    return policy;
}

PathAnalysis analyzePath(std::string_view name, const PathPolicy& policy) {
    PathAnalysis analysis;
    analysis.original = std::string(name);
    analysis.absolute = isAbsoluteArchivePath(name);
    if (name.empty() && !policy.allow_empty) {
        analysis.valid = false;
        analysis.diagnostics.error(IssueCategory::kPath,
                                   "path.empty",
                                   "entry name is empty",
                                   {0, 0});
    }
    if (name.size() > policy.max_path_length) {
        analysis.valid = false;
        analysis.diagnostics.warning(
            IssueCategory::kPath,
            "path.too_long",
            "entry name is longer than policy limit",
            {policy.max_path_length, name.size() - policy.max_path_length},
            "limit is " + std::to_string(policy.max_path_length));
    }
    if (analysis.absolute && !policy.allow_absolute) {
        analysis.valid = false;
        analysis.diagnostics.warning(IssueCategory::kPath,
                                     "path.absolute",
                                     "entry name is absolute",
                                     {0, name.empty() ? 0 : 1},
                                     "",
                                     analysis.original);
    }

    const std::string separated = normalizeSeparators(name);
    std::vector<std::string> raw_components = splitPathComponents(separated);
    std::vector<std::string> clean_components;
    std::size_t offset = 0;
    for (const std::string& component : raw_components) {
        PathComponent item;
        item.text = component;
        item.byte_offset = offset;
        item.kind = classifyComponent(component, policy);
        analysis.components.push_back(item);
        if (!kindAllowed(item.kind, policy)) {
            analysis.valid = false;
            addPathIssue(&analysis, item.kind, component, offset);
        }
        if (component.size() > policy.max_component_length) {
            analysis.valid = false;
            analysis.diagnostics.warning(
                IssueCategory::kPath,
                "path.component_too_long",
                "path component is longer than policy limit",
                {offset + policy.max_component_length,
                 component.size() - policy.max_component_length},
                "limit is " + std::to_string(policy.max_component_length),
                component);
        }
        std::string replacement = replacementForComponent(component, item.kind, policy);
        if (!replacement.empty()) {
            clean_components.push_back(replacement);
        }
        offset += component.size() + 1;
    }
    if (clean_components.size() > policy.max_components) {
        analysis.valid = false;
        analysis.diagnostics.warning(
            IssueCategory::kPath,
            "path.too_many_components",
            "entry name has too many components",
            {0, name.size()},
            "limit is " + std::to_string(policy.max_components));
        clean_components.resize(policy.max_components);
    }
    analysis.normalized = joinPathComponents(clean_components);
    if (analysis.normalized.empty()) {
        analysis.normalized = "unnamed";
    }
    analysis.changed = analysis.normalized != name;
    return analysis;
}

std::string sanitizeArchiveName(std::string_view name, const PathPolicy& policy) {
    return analyzePath(name, policy).normalized;
}

std::filesystem::path safeExtractionPath(const std::filesystem::path& output_dir,
                                         std::string_view name,
                                         const PathPolicy& policy) {
    return output_dir / std::filesystem::path(sanitizeArchiveName(name, policy));
}

std::vector<std::string> splitPathComponents(std::string_view name) {
    std::vector<std::string> components;
    std::string current;
    for (char ch : name) {
        if (ch == '/' || ch == '\\') {
            components.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    components.push_back(current);
    return components;
}

std::string joinPathComponents(const std::vector<std::string>& components) {
    std::string out;
    for (const std::string& component : components) {
        if (component.empty()) {
            continue;
        }
        if (!out.empty()) {
            out.push_back('/');
        }
        out += component;
    }
    return out;
}

std::string normalizeSeparators(std::string_view name) {
    std::string out(name);
    for (char& ch : out) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    return out;
}

std::string collapseDots(std::string_view name) {
    std::vector<std::string> out;
    for (const std::string& component : splitPathComponents(normalizeSeparators(name))) {
        if (component.empty() || component == ".") {
            continue;
        }
        if (component == "..") {
            if (!out.empty()) {
                out.pop_back();
            }
            continue;
        }
        out.push_back(component);
    }
    return joinPathComponents(out);
}

std::string removeUnsafeCharacters(std::string_view name) {
    std::string out;
    for (unsigned char ch : name) {
        if (ch >= 0x20 && ch != 0x7f && ch != '<' && ch != '>' && ch != '"' &&
            ch != '|' && ch != '?' && ch != '*') {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::string replaceUnsafeCharacters(std::string_view name, char replacement) {
    std::string out;
    for (unsigned char ch : name) {
        if (ch < 0x20 || ch == 0x7f || ch == '<' || ch == '>' || ch == '"' ||
            ch == '|' || ch == '?' || ch == '*') {
            out.push_back(replacement);
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

std::string basenameOf(std::string_view name) {
    std::string normalized = normalizeSeparators(name);
    const std::size_t slash = normalized.find_last_of('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    return normalized.substr(slash + 1);
}

std::string dirnameOf(std::string_view name) {
    std::string normalized = normalizeSeparators(name);
    const std::size_t slash = normalized.find_last_of('/');
    if (slash == std::string::npos) {
        return "";
    }
    return normalized.substr(0, slash);
}

std::string extensionOf(std::string_view name) {
    const std::string base = basenameOf(name);
    const std::size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return "";
    }
    return base.substr(dot);
}

std::string stemOf(std::string_view name) {
    const std::string base = basenameOf(name);
    const std::size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return base;
    }
    return base.substr(0, dot);
}

std::string canonicalCollisionKey(std::string_view name) {
    return lowerAscii(normalizeSeparators(trimCopy(name)));
}

std::string appendNumberSuffix(std::string_view name, std::size_t attempt) {
    const std::string dir = dirnameOf(name);
    const std::string ext = extensionOf(name);
    const std::string stem = stemOf(name);
    std::string base = stem + " (" + std::to_string(attempt + 1) + ")" + ext;
    if (dir.empty()) {
        return base;
    }
    return dir + "/" + base;
}

std::string appendHashSuffix(std::string_view name) {
    const std::string dir = dirnameOf(name);
    const std::string ext = extensionOf(name);
    const std::string stem = stemOf(name);
    const std::string hash = hexWord(stableNameHash(name), 8).substr(2, 8);
    std::string base = stem + "-" + hash + ext;
    if (dir.empty()) {
        return base;
    }
    return dir + "/" + base;
}

bool isAbsoluteArchivePath(std::string_view name) {
    return startsWith(name, "/") || startsWith(name, "\\") || hasDrivePrefix(name);
}

bool hasParentReference(std::string_view name) {
    for (const std::string& component : splitPathComponents(name)) {
        if (component == "..") {
            return true;
        }
    }
    return false;
}

bool hasCurrentReference(std::string_view name) {
    for (const std::string& component : splitPathComponents(name)) {
        if (component == ".") {
            return true;
        }
    }
    return false;
}

bool hasDrivePrefix(std::string_view name) {
    return name.size() >= 2 && isAsciiAlpha(name[0]) && name[1] == ':';
}

bool hasTrailingSpace(std::string_view component) {
    return !component.empty() && component.back() == ' ';
}

bool hasTrailingDot(std::string_view component) {
    return !component.empty() && component.back() == '.';
}

bool hasWildcard(std::string_view component) {
    return component.find('*') != std::string_view::npos ||
           component.find('?') != std::string_view::npos;
}

bool isReservedDeviceName(std::string_view component) {
    std::string base = upperAscii(stemOf(component));
    static const char* kReserved[] = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3",
        "COM4", "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1",
        "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
        "LPT9",
    };
    for (const char* reserved : kReserved) {
        if (base == reserved) {
            return true;
        }
    }
    return false;
}

bool isSafeArchiveName(std::string_view name, const PathPolicy& policy) {
    return analyzePath(name, policy).valid;
}

std::string pathComponentKindName(PathComponentKind kind) {
    switch (kind) {
    case PathComponentKind::kNormal:
        return "normal component";
    case PathComponentKind::kEmpty:
        return "empty component";
    case PathComponentKind::kCurrentDirectory:
        return "current directory component";
    case PathComponentKind::kParentDirectory:
        return "parent directory component";
    case PathComponentKind::kRoot:
        return "root component";
    case PathComponentKind::kDrive:
        return "drive prefix";
    case PathComponentKind::kReservedDevice:
        return "reserved device name";
    case PathComponentKind::kTrailingSpace:
        return "trailing space";
    case PathComponentKind::kTrailingDot:
        return "trailing dot";
    case PathComponentKind::kControl:
        return "control byte";
    case PathComponentKind::kWildcard:
        return "wildcard character";
    }
    return "unknown path component";
}

IssueCategory pathIssueCategory(PathComponentKind kind) {
    switch (kind) {
    case PathComponentKind::kNormal:
        return IssueCategory::kPath;
    case PathComponentKind::kEmpty:
    case PathComponentKind::kCurrentDirectory:
    case PathComponentKind::kParentDirectory:
    case PathComponentKind::kRoot:
    case PathComponentKind::kDrive:
        return IssueCategory::kPath;
    case PathComponentKind::kReservedDevice:
    case PathComponentKind::kTrailingSpace:
    case PathComponentKind::kTrailingDot:
    case PathComponentKind::kControl:
    case PathComponentKind::kWildcard:
        return IssueCategory::kPolicy;
    }
    return IssueCategory::kPath;
}

}  // namespace vector
