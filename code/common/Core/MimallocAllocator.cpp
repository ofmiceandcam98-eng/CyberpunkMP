#include "MimallocAllocator.h"

#include <mimalloc.h>
#include <spdlog/spdlog.h>

namespace
{
/**
 * Make the allocator say something when it notices the heap is wrong.
 *
 * Two crashes on 27 August landed inside this DLL with the allocator holding values that
 * cannot be pointers - 0x11 and 0x13 in one, a null in another - a few seconds after a new
 * character finished the creator. That is corruption: some earlier write damaged a block,
 * and the crash lands later in whatever unlucky code next walks a free list. The faulting
 * address says nothing about who wrote the bad byte, which is why three fixes reasoned
 * from it were wrong.
 *
 * mimalloc already detects this class of damage - double frees, freeing a pointer it does
 * not own, corrupted free lists - it just reports to stderr, which nothing here collects.
 * Routing its two callbacks into the log means the next occurrence names itself instead of
 * being inferred from a disassembly.
 *
 * Cheap enough to leave in permanently: these fire only when mimalloc has something to
 * say, and if it never speaks again that is itself worth knowing.
 */
void OnMimallocError(const int aErrorCode, void*)
{
    // Codes are plain errno values. EFAULT and EINVAL are the two that mean corruption
    // rather than simply running out of something.
    const char* meaning = "unknown";
    switch (aErrorCode)
    {
    case 14: meaning = "EFAULT - corrupted free list or an invalid pointer was freed"; break;
    case 22: meaning = "EINVAL - a pointer was freed that mimalloc does not own"; break;
    case 12: meaning = "ENOMEM - out of memory"; break;
    case 75: meaning = "EOVERFLOW - allocation size overflow"; break;
    case 11: meaning = "EAGAIN - double free detected"; break;
    default: break;
    }

    spdlog::error("[Heap] mimalloc reported error {} ({})", aErrorCode, meaning);
}

void OnMimallocOutput(const char* acpMessage, void*)
{
    if (acpMessage && *acpMessage)
        spdlog::warn("[Heap] {}", acpMessage);
}

struct MimallocReporting
{
    MimallocReporting()
    {
        mi_register_error(&OnMimallocError, nullptr);
        mi_register_output(&OnMimallocOutput, nullptr);
    }
};

// Registered before main so it is already in place for the earliest allocation.
const MimallocReporting gReporting;
} // namespace

void* MimallocAllocator::Allocate(const size_t aSize) noexcept
{
    return mi_malloc(aSize);
}

void MimallocAllocator::Free(void* apData) noexcept
{
    mi_free(apData);
}

size_t MimallocAllocator::Size(void* apData) noexcept
{
    if (apData == nullptr) return 0;

    return mi_malloc_size(apData);
}

void* MimallocAllocator::AlignedAllocate(size_t aSize, size_t aAlignment) noexcept
{
    return mi_malloc_aligned(aSize, aAlignment);
}

void MimallocAllocator::AlignedFree(void* apData) noexcept
{
    mi_free(apData);
}

