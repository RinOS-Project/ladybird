/*
 * Copyright (c) 2021-2023, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2025, Ryszard Goc <ryszardgoc@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Assertions.h>
#include <AK/Platform.h>
#include <AK/Random.h>
#include <AK/Vector.h>
#include <LibGC/BlockAllocator.h>
#include <LibGC/HeapBlock.h>
#include <sys/mman.h>

#if defined(AK_OS_MACOS)
#    include <mach/mach.h>
#    include <mach/mach_vm.h>
#endif

#ifdef HAS_ADDRESS_SANITIZER
#    include <sanitizer/asan_interface.h>
#    include <sanitizer/lsan_interface.h>
#endif

#if defined(AK_OS_WINDOWS)
#    include <AK/Windows.h>
#    include <memoryapi.h>
#endif

namespace GC {

BlockAllocator::~BlockAllocator()
{
    for (auto* block : m_blocks) {
        ASAN_UNPOISON_MEMORY_REGION(block, HeapBlock::BLOCK_SIZE);
#if defined(AK_OS_MACOS)
        kern_return_t kr = mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(block), HeapBlock::BLOCK_SIZE);
        VERIFY(kr == KERN_SUCCESS);
#elif defined(AK_OS_WINDOWS)
        if (!VirtualFree(block, 0, MEM_RELEASE)) {
            warnln("{}", Error::from_windows_error());
            VERIFY_NOT_REACHED();
        }
#elif defined(AK_OS_RINOS)
        munmap(block, HeapBlock::BLOCK_SIZE);
#else
        free(block);
#endif
    }
}

void* BlockAllocator::allocate_block([[maybe_unused]] char const* name)
{
    if (!m_blocks.is_empty()) {
        // To reduce predictability, take a random block from the cache.
        size_t random_index = get_random_uniform(m_blocks.size());
        auto* block = m_blocks.unstable_take(random_index);
        ASAN_UNPOISON_MEMORY_REGION(block, HeapBlock::BLOCK_SIZE);
        LSAN_REGISTER_ROOT_REGION(block, HeapBlock::BLOCK_SIZE);
#if defined(MADV_FREE_REUSE) && defined(MADV_FREE_REUSABLE)
        if (madvise(block, HeapBlock::BLOCK_SIZE, MADV_FREE_REUSE) < 0) {
            perror("madvise(MADV_FREE_REUSE)");
            VERIFY_NOT_REACHED();
        }
#endif
        return block;
    }

#if defined(AK_OS_MACOS)
    mach_vm_address_t address = 0;
    kern_return_t kr = mach_vm_map(
        mach_task_self(),
        &address,
        HeapBlock::BLOCK_SIZE,
        HeapBlock::BLOCK_SIZE - 1,
        VM_FLAGS_ANYWHERE,
        MEMORY_OBJECT_NULL,
        0,
        false,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_PROT_READ | VM_PROT_WRITE,
        VM_INHERIT_DEFAULT);
    VERIFY(kr == KERN_SUCCESS);
    auto* block = reinterpret_cast<void*>(address);
#elif defined(AK_OS_WINDOWS)
    auto* block = VirtualAlloc(NULL, HeapBlock::BLOCK_SIZE, MEM_COMMIT, PAGE_READWRITE);
    VERIFY(block);
#elif defined(AK_OS_RINOS)
    // On RinOS, use mmap to isolate GC heap blocks from the general kernel heap.
    // posix_memalign uses platform_kmalloc which shares the flat heap with all
    // other allocations — adjacent buffer overflows can corrupt HeapBlock headers.
    // mmap provides page-granularity isolation, preventing heap corruption.
    constexpr size_t block_size = HeapBlock::BLOCK_SIZE; // 16 KiB
    // Allocate 2x to guarantee we can find a block_size-aligned region within.
    void* raw = mmap(nullptr, block_size * 2, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    VERIFY(raw != MAP_FAILED);
    auto raw_addr = reinterpret_cast<uintptr_t>(raw);
    auto aligned_addr = (raw_addr + block_size - 1) & ~(block_size - 1);
    // Release the leading portion before the aligned address.
    if (aligned_addr > raw_addr)
        munmap(raw, aligned_addr - raw_addr);
    // Release the trailing portion after the aligned block.
    auto tail_start = aligned_addr + block_size;
    auto tail_end = raw_addr + block_size * 2;
    if (tail_end > tail_start)
        munmap(reinterpret_cast<void*>(tail_start), tail_end - tail_start);
    auto* block = reinterpret_cast<void*>(aligned_addr);
#else
    void* block = nullptr;
    auto rc = posix_memalign(&block, HeapBlock::BLOCK_SIZE, HeapBlock::BLOCK_SIZE);
    VERIFY(rc == 0);
#endif
    LSAN_REGISTER_ROOT_REGION(block, HeapBlock::BLOCK_SIZE);
    return block;
}

void BlockAllocator::deallocate_block(void* block)
{
    VERIFY(block);

#if defined(AK_OS_WINDOWS)
    DWORD ret = DiscardVirtualMemory(block, HeapBlock::BLOCK_SIZE);
    if (ret != ERROR_SUCCESS) {
        warnln("{}", Error::from_windows_error(ret));
        VERIFY_NOT_REACHED();
    }
#elif defined(MADV_FREE_REUSE) && defined(MADV_FREE_REUSABLE)
    if (madvise(block, HeapBlock::BLOCK_SIZE, MADV_FREE_REUSABLE) < 0) {
        perror("madvise(MADV_FREE_REUSABLE)");
        VERIFY_NOT_REACHED();
    }
#elif defined(MADV_FREE)
    if (madvise(block, HeapBlock::BLOCK_SIZE, MADV_FREE) < 0) {
        perror("madvise(MADV_FREE)");
        VERIFY_NOT_REACHED();
    }
#elif defined(MADV_DONTNEED)
    if (madvise(block, HeapBlock::BLOCK_SIZE, MADV_DONTNEED) < 0) {
        perror("madvise(MADV_DONTNEED)");
        VERIFY_NOT_REACHED();
    }
#endif

    ASAN_POISON_MEMORY_REGION(block, HeapBlock::BLOCK_SIZE);
    LSAN_UNREGISTER_ROOT_REGION(block, HeapBlock::BLOCK_SIZE);
    // On RinOS, zero the header area to prevent stale m_heap references.
#if defined(AK_OS_RINOS)
    __builtin_memset(block, 0, 64);
#endif
    m_blocks.append(block);
}

}
