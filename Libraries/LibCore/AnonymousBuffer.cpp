/*
 * Copyright (c) 2021, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Atomic.h>
#include <AK/ByteString.h>
#include <AK/NumericLimits.h>
#include <AK/Try.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/System.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>

namespace Core {

#if defined(AK_OS_RINOS)
extern "C" {
int rin_shm_get(char const* name, u32 size, u32 flags);
void* rin_shm_at(int handle, void* addr_hint, u32 prot);
int rin_shm_dt(int handle, void* addr);
}

static constexpr u32 RIN_SHM_FLAG_CREAT = 0x00000001u;
static constexpr u32 RIN_SHM_FLAG_EXCL = 0x00000002u;
static constexpr u32 RIN_SHM_PROT_READ = 0x00000001u;
static constexpr u32 RIN_SHM_PROT_WRITE = 0x00000002u;

static ByteString next_rinos_anonymous_buffer_name()
{
    static Atomic<u32> s_next_buffer_id = 1;
    auto id = s_next_buffer_id.fetch_add(1, AK::MemoryOrder::memory_order_relaxed);
    return ByteString::formatted("/ladybird-anon-{}-{}", System::getpid(), id);
}
#endif

ErrorOr<AnonymousBuffer> AnonymousBuffer::create_with_size(size_t size)
{
#if defined(AK_OS_RINOS)
    auto impl = TRY(AnonymousBufferImpl::create(size));
    return AnonymousBuffer(move(impl));
#else
    auto fd = TRY(Core::System::anon_create(size, O_CLOEXEC));
    return create_from_anon_fd(fd, size);
#endif
}

ErrorOr<NonnullRefPtr<AnonymousBufferImpl>> AnonymousBufferImpl::create(size_t size)
{
#if defined(AK_OS_RINOS)
    if (size > NumericLimits<u32>::max())
        return Error::from_errno(EINVAL);

    auto shm_name = next_rinos_anonymous_buffer_name();
    int handle = rin_shm_get(shm_name.characters(),
        static_cast<u32>(size),
        RIN_SHM_FLAG_CREAT | RIN_SHM_FLAG_EXCL);
    if (handle < 0)
        return Error::from_errno(errno == 0 ? EIO : errno);

    void* data = rin_shm_at(handle, nullptr, RIN_SHM_PROT_READ | RIN_SHM_PROT_WRITE);
    if (!data) {
        auto saved_errno = errno;
        (void)rin_shm_dt(handle, nullptr);
        return Error::from_errno(saved_errno == 0 ? EIO : saved_errno);
    }

    auto impl = TRY(AK::adopt_nonnull_ref_or_enomem(new (nothrow) AnonymousBufferImpl(handle, size, data, BackingKind::RinSharedMemory)));
    impl->m_shm_name = move(shm_name);
    return impl;
#else
    auto fd = TRY(Core::System::anon_create(size, O_CLOEXEC));
    return AnonymousBufferImpl::create(fd, size);
#endif
}

ErrorOr<NonnullRefPtr<AnonymousBufferImpl>> AnonymousBufferImpl::create(int fd, size_t size)
{
    auto* data = mmap(nullptr, round_up_to_power_of_two(size, PAGE_SIZE), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED)
        return Error::from_errno(errno);
    return AK::adopt_nonnull_ref_or_enomem(new (nothrow) AnonymousBufferImpl(fd, size, data));
}

AnonymousBufferImpl::~AnonymousBufferImpl()
{
#if defined(AK_OS_RINOS)
    if (m_backing_kind == BackingKind::RinSharedMemory) {
        if (m_fd != -1) {
            auto rc = rin_shm_dt(m_fd, m_data);
            VERIFY(rc == 0);
        }
        return;
    }
#endif

    if (m_fd != -1) {
        auto rc = close(m_fd);
        VERIFY(rc == 0);
    }
    auto rc = munmap(m_data, round_up_to_power_of_two(m_size, PAGE_SIZE));
    VERIFY(rc == 0);
}

ErrorOr<AnonymousBuffer> AnonymousBuffer::create_from_anon_fd(int fd, size_t size)
{
    auto impl = TRY(AnonymousBufferImpl::create(fd, size));
    return AnonymousBuffer(move(impl));
}

#if defined(AK_OS_RINOS)
ErrorOr<AnonymousBuffer> AnonymousBuffer::create_from_shm_name(StringView name, size_t size)
{
    if (name.is_empty() || size == 0 || size > NumericLimits<u32>::max())
        return Error::from_errno(EINVAL);

    ByteString name_str { name };
    int handle = rin_shm_get(name_str.characters(), static_cast<u32>(size), 0);
    if (handle < 0)
        return Error::from_errno(errno == 0 ? ENOENT : errno);

    void* data = rin_shm_at(handle, nullptr, RIN_SHM_PROT_READ | RIN_SHM_PROT_WRITE);
    if (!data) {
        auto saved_errno = errno;
        (void)rin_shm_dt(handle, nullptr);
        return Error::from_errno(saved_errno == 0 ? EIO : saved_errno);
    }

    auto impl = TRY(AK::adopt_nonnull_ref_or_enomem(new (nothrow) AnonymousBufferImpl(handle, size, data, AnonymousBufferImpl::BackingKind::RinSharedMemory)));
    impl->m_shm_name = move(name_str);
    return AnonymousBuffer(move(impl));
}
#endif

AnonymousBufferImpl::AnonymousBufferImpl(int fd, size_t size, void* data, BackingKind backing_kind)
    : m_fd(fd)
    , m_size(size)
    , m_data(data)
    , m_backing_kind(backing_kind)
{
}

}
