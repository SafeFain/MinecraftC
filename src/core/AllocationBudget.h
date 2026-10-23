#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>

// Allocation hooks for third-party parsers/decoders. Owners outlive every
// allocation; a rejected realloc preserves the original block.
class AllocationBudget {
public:
    explicit AllocationBudget(std::size_t maximum) : limit(maximum) {}
    AllocationBudget(const AllocationBudget&) = delete;
    AllocationBudget& operator=(const AllocationBudget&) = delete;
    std::size_t limit, live = 0;
    bool rejected = false;

    static void* allocate(std::size_t size, AllocationBudget* owner) {
        if (size > std::numeric_limits<std::size_t>::max() - sizeof(Header) ||
            (owner && size + sizeof(Header) > owner->limit - owner->live)) {
            if (owner) owner->rejected = true;
            return nullptr;
        }
        auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + size));
        if (!header) return nullptr;
        header->size = size; header->owner = owner;
        if (owner) owner->live += size + sizeof(Header);
        return header + 1;
    }
    static void release(void* pointer) {
        if (!pointer) return;
        auto* header = static_cast<Header*>(pointer) - 1;
        if (header->owner) header->owner->live -= header->size + sizeof(Header);
        std::free(header);
    }
    static void* resize(void* pointer, std::size_t size, AllocationBudget* fallback) {
        if (!pointer) return allocate(size, fallback);
        auto* old = static_cast<Header*>(pointer) - 1;
        // Reserve both blocks: realloc can temporarily require both allocations.
        void* replacement = allocate(size, old->owner);
        if (!replacement) return nullptr;
        std::memcpy(replacement, pointer, std::min(size, old->size));
        release(pointer);
        return replacement;
    }
    inline static thread_local AllocationBudget* decoder = nullptr;
private:
    struct alignas(std::max_align_t) Header {
        std::size_t size;
        AllocationBudget* owner;
    };
};

class DecoderAllocationScope {
public:
    explicit DecoderAllocationScope(AllocationBudget& budget)
        : previous(AllocationBudget::decoder) { AllocationBudget::decoder = &budget; }
    ~DecoderAllocationScope() { AllocationBudget::decoder = previous; }
    DecoderAllocationScope(const DecoderAllocationScope&) = delete;
    DecoderAllocationScope& operator=(const DecoderAllocationScope&) = delete;
private:
    AllocationBudget* previous;
};
