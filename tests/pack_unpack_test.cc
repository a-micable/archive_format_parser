#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "archive.h"

namespace {

std::filesystem::path tempRoot() {
    auto root = std::filesystem::temp_directory_path() /
                ("vector-archive-test-" + std::to_string(::getpid()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

void filesystemRoundTrip(bool compress) {
    const auto root = tempRoot();
    const auto input_dir = root / "input";
    const auto output_dir = root / "output";
    writeText(input_dir / "alpha.txt", "alpha alpha alpha alpha\n");
    writeText(input_dir / "beta.bin", std::string(300, 'B'));

    std::string error;
    const auto archive = root / "bundle.vec";
    assert(vector::pack({input_dir / "alpha.txt", input_dir / "beta.bin"},
                        archive, compress, &error));
    assert(vector::unpack(archive, output_dir, &error));
    assert(readText(output_dir / input_dir.relative_path() / "alpha.txt") ==
           "alpha alpha alpha alpha\n");
    assert(readText(output_dir / input_dir.relative_path() / "beta.bin") ==
           std::string(300, 'B'));

    std::filesystem::remove_all(root);
}

void inMemoryArchiveVariants() {
    std::vector<vector::FilePayload> files = {
        {"plain.txt", {'p', 'l', 'a', 'i', 'n'}},
        {"runs.bin", std::vector<std::uint8_t>(180, 0x41)},
    };

    auto compressed_ext = vector::buildArchiveForTest(files, true, true, true);
    auto invalid_ext = vector::buildArchiveForTest(files, false, true, false);
    assert(vector::unpack(compressed_ext.data(), compressed_ext.size()));
    assert(vector::unpack(invalid_ext.data(), invalid_ext.size()));
}

void nestedArchiveVariant() {
    auto leaf = vector::buildArchiveForTest({{"leaf.txt", {'l', 'e', 'a', 'f'}}},
                                            false, true, true);
    auto middle = vector::buildArchiveForTest({{"leaf.vec", leaf},
                                               {"pad.bin", std::vector<std::uint8_t>(71, 'x')}},
                                              true, false, true);
    auto root = vector::buildArchiveForTest({{"middle.vec", middle},
                                             {"root.txt", {'r', 'o', 'o', 't'}}},
                                            false, false, true);
    assert(vector::unpack(root.data(), root.size()));
    assert(vector::unpackNestedOnly(root.data(), root.size()));
}

}  // namespace

int main() {
    filesystemRoundTrip(false);
    filesystemRoundTrip(true);
    inMemoryArchiveVariants();
    nestedArchiveVariant();
    return 0;
}
