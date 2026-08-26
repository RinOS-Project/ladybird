/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCore/AnonymousBuffer.h>
#include <LibTest/TestCase.h>

#include <errno.h>

TEST_CASE(rinos_anonymous_buffer_roundtrips_by_shared_memory_name)
{
    constexpr size_t buffer_size = 4097;
    auto owner = TRY_OR_FAIL(Core::AnonymousBuffer::create_with_size(buffer_size));
    for (size_t index = 0; index < owner.size(); ++index)
        owner.data<u8>()[index] = static_cast<u8>(index % 251);

    auto peer = TRY_OR_FAIL(Core::AnonymousBuffer::create_from_shm_name(owner.shm_name(), owner.size()));
    EXPECT_EQ(peer.size(), owner.size());
    EXPECT_EQ(__builtin_memcmp(owner.data<u8>(), peer.data<u8>(), owner.size()), 0);

    peer.data<u8>()[buffer_size - 1] = 0xa5;
    EXPECT_EQ(owner.data<u8>()[buffer_size - 1], 0xa5);

    auto mismatched = Core::AnonymousBuffer::create_from_shm_name(owner.shm_name(), owner.size() + 1);
    EXPECT(mismatched.is_error());
    EXPECT_EQ(mismatched.error().code(), EINVAL);
}
