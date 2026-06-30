#include <cstddef>
#include <cstdint>

#include "archive.h"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    vector::unpack(data, size);
    return 0;
}
