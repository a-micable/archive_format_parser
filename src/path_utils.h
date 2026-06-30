#ifndef VECTOR_ARCHIVE_PATH_UTILS_H_
#define VECTOR_ARCHIVE_PATH_UTILS_H_

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.h"

namespace vector {

enum class PathComponentKind {
    kNormal,
    kEmpty,
    kCurrentDirectory,
    kParentDirectory,
    kRoot,
    kDrive,
    kReservedDevice,
    kTrailingSpace,
    kTrailingDot,
    kControl,
    kWildcard,
};

struct PathComponent {
    std::string text;
    PathComponentKind kind = PathComponentKind::kNormal;
    std::size_t byte_offset = 0;
};

enum class CollisionStrategy {
    kKeepFirst,
    kAppendNumber,
    kAppendHash,
};

struct PathPolicy {
    bool allow_empty = false;
    bool allow_absolute = false;
    bool allow_parent = false;
    bool allow_current = true;
    bool allow_drive_names = false;
    bool allow_control_bytes = false;
    bool allow_reserved_devices = false;
    bool allow_trailing_spaces = false;
    bool allow_trailing_dots = false;
    bool allow_wildcards = true;
    bool lowercase_for_collision = true;
    std::size_t max_components = 64;
    std::size_t max_component_length = 255;
    std::size_t max_path_length = 4096;
    CollisionStrategy collision_strategy = CollisionStrategy::kAppendNumber;
};

struct PathAnalysis {
    std::string original;
    std::string normalized;
    std::vector<PathComponent> components;
    bool absolute = false;
    bool changed = false;
    bool valid = true;
    Diagnostics diagnostics;
};

struct CollisionResult {
    std::string requested;
    std::string assigned;
    bool collided = false;
    std::size_t attempt = 0;
};

class CollisionResolver {
public:
    explicit CollisionResolver(PathPolicy policy = {});

    CollisionResult assign(std::string_view requested);
    bool contains(std::string_view normalized) const;
    std::size_t size() const;
    void clear();
    const std::map<std::string, std::string>& assignments() const;

private:
    std::string collisionKey(std::string_view value) const;
    std::string makeCandidate(std::string_view value, std::size_t attempt) const;

    PathPolicy policy_;
    std::map<std::string, std::string> assignments_;
    std::set<std::string> keys_;
};

PathPolicy defaultArchivePathPolicy();
PathPolicy permissivePathPolicy();
PathAnalysis analyzePath(std::string_view name,
                         const PathPolicy& policy = defaultArchivePathPolicy());
std::string sanitizeArchiveName(std::string_view name,
                                const PathPolicy& policy = defaultArchivePathPolicy());
std::filesystem::path safeExtractionPath(const std::filesystem::path& output_dir,
                                         std::string_view name,
                                         const PathPolicy& policy = defaultArchivePathPolicy());
std::vector<std::string> splitPathComponents(std::string_view name);
std::string joinPathComponents(const std::vector<std::string>& components);
std::string normalizeSeparators(std::string_view name);
std::string collapseDots(std::string_view name);
std::string removeUnsafeCharacters(std::string_view name);
std::string replaceUnsafeCharacters(std::string_view name, char replacement);
std::string basenameOf(std::string_view name);
std::string dirnameOf(std::string_view name);
std::string extensionOf(std::string_view name);
std::string stemOf(std::string_view name);
std::string canonicalCollisionKey(std::string_view name);
std::string appendNumberSuffix(std::string_view name, std::size_t attempt);
std::string appendHashSuffix(std::string_view name);

bool isAbsoluteArchivePath(std::string_view name);
bool hasParentReference(std::string_view name);
bool hasCurrentReference(std::string_view name);
bool hasDrivePrefix(std::string_view name);
bool hasTrailingSpace(std::string_view component);
bool hasTrailingDot(std::string_view component);
bool hasWildcard(std::string_view component);
bool isReservedDeviceName(std::string_view component);
bool isSafeArchiveName(std::string_view name,
                       const PathPolicy& policy = defaultArchivePathPolicy());

std::string pathComponentKindName(PathComponentKind kind);
IssueCategory pathIssueCategory(PathComponentKind kind);

}  // namespace vector

#endif  // VECTOR_ARCHIVE_PATH_UTILS_H_
