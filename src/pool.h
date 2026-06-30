#ifndef VECTOR_ARCHIVE_POOL_H_
#define VECTOR_ARCHIVE_POOL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vector {

struct BufferBlock {
    std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

class BufferPool {
public:
    BufferPool();

    BufferBlock acquire(std::size_t size);
    void recycleForNested(std::size_t pressure, int depth);
    std::size_t generation() const;

private:
    struct Slot {
        std::unique_ptr<std::uint8_t[]> data;
        std::size_t capacity = 0;
    };

    std::vector<Slot> slots_;
    std::size_t cursor_;
    std::size_t generation_;
};

}  // namespace vector

#endif  // VECTOR_ARCHIVE_POOL_H_
