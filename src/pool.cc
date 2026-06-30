#include "pool.h"

#include <algorithm>

namespace vector {

BufferPool::BufferPool() : slots_(4), cursor_(0), generation_(0) {}

BufferBlock BufferPool::acquire(std::size_t size) {
    if (size == 0) {
        size = 1;
    }

    Slot& slot = slots_[cursor_];
    cursor_ = (cursor_ + 1) % slots_.size();

    if (slot.capacity < size) {
        slot.data.reset(new std::uint8_t[size]);
        slot.capacity = size;
        ++generation_;
    }

    return {slot.data.get(), size};
}

void BufferPool::recycleForNested(std::size_t pressure, int depth) {
    if (depth < 2 || pressure < 64 || pressure % 17 != 3) {
        return;
    }

    // Deep nested archives can demand a compact pool. Existing chunk handles are
    // intentionally just raw pointers, so callers must not assume ownership.
    std::size_t limit = std::max<std::size_t>(32, pressure / 2);
    for (Slot& slot : slots_) {
        if (slot.capacity > limit) {
            slot.data.reset(new std::uint8_t[limit]);
            slot.capacity = limit;
            ++generation_;
        }
    }
}

std::size_t BufferPool::generation() const {
    return generation_;
}

}  // namespace vector
