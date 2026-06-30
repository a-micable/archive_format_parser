#include <cstddef>
#include <cstdint>
#include <vector>

#include "archive.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 4) {
        return 0;
    }

    std::vector<std::uint8_t> leaf(data, data + size);
    auto nested = vector::buildArchiveForTest({{"mutated.bin", leaf}},
                                              false, false, true);
    auto root = vector::buildArchiveForTest({{"nested.vec", nested},
                                             {"pressure.bin", std::vector<std::uint8_t>(71, 0x33)}},
                                            false, false, true);
    vector::unpackNestedOnly(root.data(), root.size());
    return 0;
}
