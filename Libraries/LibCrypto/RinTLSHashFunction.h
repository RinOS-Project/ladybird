/*
 * Copyright (c) 2025, RinOS Contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#ifdef AK_OS_RINOS

#include <AK/ByteString.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Types.h>
#include <LibCrypto/Hash/HashFunction.h>
#include <string.h>

namespace Crypto::Hash {

// Generic hash function template for rintls-backed and self-contained hash implementations.
// CtxT must be a trivially copyable C struct. Derived must implement:
//   static void rin_init(CtxT*)
//   static void rin_update(CtxT*, u8 const*, size_t)
//   static void rin_final(CtxT*, u8*)  — finalizes WITHOUT resetting
template<typename Derived, size_t BlockS, size_t DigestS, typename CtxT, typename DigestT = Digest<DigestS>>
class RinTLSHashFunction : public HashFunction<BlockS, DigestS, DigestT> {
    AK_MAKE_NONCOPYABLE(RinTLSHashFunction);

public:
    using HashFunction<BlockS, DigestS, DigestT>::update;

    static NonnullOwnPtr<Derived> create()
    {
        return make<Derived>();
    }

    RinTLSHashFunction()
    {
        Derived::rin_init(&m_ctx);
    }

    virtual ~RinTLSHashFunction() override = default;

    virtual ByteString class_name() const override = 0;

    void update(u8 const* input, size_t length) override
    {
        Derived::rin_update(&m_ctx, input, length);
    }

    DigestT digest() override
    {
        DigestT d;
        CtxT copy;
        memcpy(&copy, &m_ctx, sizeof(CtxT));
        Derived::rin_final(&copy, d.data);
        Derived::rin_init(&m_ctx);
        return d;
    }

    DigestT peek() override
    {
        DigestT d;
        CtxT copy;
        memcpy(&copy, &m_ctx, sizeof(CtxT));
        Derived::rin_final(&copy, d.data);
        return d;
    }

    void reset() override
    {
        Derived::rin_init(&m_ctx);
    }

    virtual NonnullOwnPtr<Derived> copy() const
    {
        auto other = create();
        memcpy(&other->m_ctx, &m_ctx, sizeof(CtxT));
        return other;
    }

    static DigestT hash(u8 const* data, size_t length)
    {
        auto hasher = create();
        hasher->update(data, length);
        return hasher->digest();
    }

    static DigestT hash(ByteBuffer const& buffer) { return hash(buffer.data(), buffer.size()); }
    static DigestT hash(StringView buffer) { return hash(reinterpret_cast<u8 const*>(buffer.characters_without_null_termination()), buffer.length()); }

protected:
    CtxT m_ctx;
};

}

#endif // AK_OS_RINOS
