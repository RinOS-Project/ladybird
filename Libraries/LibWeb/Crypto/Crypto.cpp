/*
 * Copyright (c) 2021, Idan Horowitz <idan.horowitz@serenityos.org>
 * Copyright (c) 2022, stelar7 <dudedbz@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Random.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibWeb/Bindings/CryptoPrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Crypto/Crypto.h>
#include <LibWeb/Crypto/SubtleCrypto.h>
#include <LibWeb/WebIDL/Buffers.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/QuotaExceededError.h>

#if defined(AK_OS_RINOS)
extern "C" int rin_webcrypto_get_random_uuid(void*);
extern "C" int rin_webcrypto_get_random_values(void*, size_t);
#endif

namespace Web::Crypto {

GC_DEFINE_ALLOCATOR(Crypto);

GC::Ref<Crypto> Crypto::create(JS::Realm& realm)
{
    return realm.create<Crypto>(realm);
}

Crypto::Crypto(JS::Realm& realm)
    : PlatformObject(realm)
{
}

Crypto::~Crypto() = default;

void Crypto::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(Crypto);
    Base::initialize(realm);
    m_subtle = SubtleCrypto::create(realm);
}

void Crypto::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_subtle);
}

GC::Ref<SubtleCrypto> Crypto::subtle() const
{
    return *m_subtle;
}

// https://w3c.github.io/webcrypto/#dfn-Crypto-method-getRandomValues
WebIDL::ExceptionOr<GC::Root<WebIDL::ArrayBufferView>> Crypto::get_random_values(GC::Root<WebIDL::ArrayBufferView> array) const
{
    // 1. If array is not an Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, BigInt64Array, or BigUint64Array, then throw a TypeMismatchError and terminate the algorithm.
    if (!array->is_typed_array_base())
        return WebIDL::TypeMismatchError::create(realm(), "array must be one of Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, BigInt64Array, or BigUint64Array"_utf16);

    auto const& typed_array = *array->bufferable_object().get<GC::Ref<JS::TypedArrayBase>>();
    if (!typed_array.element_name().is_one_of("Int8Array"sv, "Uint8Array"sv, "Uint8ClampedArray"sv, "Int16Array"sv, "Uint16Array"sv, "Int32Array"sv, "Uint32Array"sv, "BigInt64Array"sv, "BigUint64Array"sv))
        return WebIDL::TypeMismatchError::create(realm(), "array must be one of Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array, Uint32Array, BigInt64Array, or BigUint64Array"_utf16);

    auto typed_array_record = JS::make_typed_array_with_buffer_witness_record(typed_array, JS::ArrayBuffer::Order::SeqCst);

    // IMPLEMENTATION DEFINED: If the viewed array buffer is out-of-bounds, throw a InvalidStateError and terminate the algorithm.
    if (JS::is_typed_array_out_of_bounds(typed_array_record))
        return WebIDL::InvalidStateError::create(realm(), Utf16String::formatted(JS::ErrorType::BufferOutOfBounds.format(), "TypedArray"sv));

    auto byte_length = JS::typed_array_byte_length(typed_array_record);

    // 2. If the byteLength of array is greater than 65536, throw a QuotaExceededError and terminate the algorithm.
    if (byte_length > 65536)
        return WebIDL::QuotaExceededError::create(realm(), "array's byteLength may not be greater than 65536"_utf16);

    // 3. Overwrite all elements of array with cryptographically strong random values of the appropriate type.
#if defined(AK_OS_RINOS)
    auto random_bytes = TRY_OR_THROW_OOM(realm().vm(), ByteBuffer::create_uninitialized(byte_length));
    if (rin_webcrypto_get_random_values(random_bytes.data(), random_bytes.size()) != 0)
        return WebIDL::OperationError::create(realm(), "Secure randomness is unavailable"_utf16);
    array->viewed_array_buffer()->buffer().overwrite(array->byte_offset(), random_bytes.data(), random_bytes.size());
#else
    fill_with_random(array->viewed_array_buffer()->buffer().bytes().slice(array->byte_offset(), array->byte_length()));
#endif

    // 4. Return array.
    return array;
}

// https://w3c.github.io/webcrypto/#dfn-Crypto-method-randomUUID
WebIDL::ExceptionOr<String> Crypto::random_uuid() const
{
#if defined(AK_OS_RINOS)
    u8 bytes[16];
    if (rin_webcrypto_get_random_uuid(bytes) != 0)
        return WebIDL::OperationError::create(realm(), "Secure randomness is unavailable"_utf16);
    return TRY_OR_THROW_OOM(realm().vm(), String::formatted(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5],
        bytes[6], bytes[7],
        bytes[8], bytes[9],
        bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]));
#else
    return generate_random_uuid();
#endif
}

// https://w3c.github.io/webcrypto/#dfn-generate-a-random-uuid
String generate_random_uuid()
{
    return AK::generate_random_uuid();
}

}
