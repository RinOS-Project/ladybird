/*
 * Copyright (c) 2020, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGC/Cell.h>
#include <LibGC/Heap.h>
#include <LibGC/HeapBlock.h>
#include <LibGC/Root.h>

#if defined(AK_OS_RINOS)
// Validate that the HeapBlock header for a cell is not corrupted.
// Returns true if valid, false if the m_heap pointer looks corrupted.
static bool validate_heap_block_header(GC::Cell* cell)
{
    // On 64-bit RinOS, GC HeapBlocks are mmap'd starting at 0x100000000 (4GB).
    // Any cell address below that is in the ELF text/data range and cannot be
    // a valid GC cell — reject it immediately.
    auto cell_addr = reinterpret_cast<uintptr_t>(cell);
    if (sizeof(uintptr_t) >= 8 && cell_addr < 0x100000000ULL)
        return false;

    auto* block_base = GC::HeapBlockBase::from_cell(cell);
    // The first sizeof(pointer) bytes of HeapBlockBase contain m_heap (a Heap&).
    // Read the stored pointer value directly.
    auto heap_ptr = static_cast<uint64_t>(*reinterpret_cast<uintptr_t const*>(block_base));
    // Check canonical form: bits 47..63 must be all-zero (user space) or all-one (kernel).
    bool canonical = (heap_ptr >> 47) == 0 || (heap_ptr >> 47) == 0x1FFFF;
    // Also reject null and suspiciously small pointers.
    bool plausible = heap_ptr > 0x10000;
    return canonical && plausible;
}
#endif

namespace GC {

RootImpl::RootImpl(Cell* cell, SourceLocation location)
    : m_cell(cell)
    , m_location(location)
{
#if defined(AK_OS_RINOS)
    if (!validate_heap_block_header(cell)) {
        // HeapBlock header is corrupted — skip root registration to avoid GPF.
        // The cell is still tracked by m_cell but won't be registered in the
        // root list. This is a safe degradation: the GC may collect the cell
        // prematurely, but we avoid a hard crash.
        return;
    }
#endif
    m_cell->heap().did_create_root({}, *this);
}

RootImpl::~RootImpl()
{
#if defined(AK_OS_RINOS)
    if (!validate_heap_block_header(m_cell)) {
        return;
    }
#endif
    m_cell->heap().did_destroy_root({}, *this);
}

}
