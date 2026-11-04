/*
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 * Copyright (c) 2026, the RinOS developers
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/ByteBuffer.h>
#include <LibJS/Runtime/ArrayBuffer.h>
#include <LibWeb/Bindings/ExceptionOrUtils.h>
#include <LibWeb/CredentialManagement/AuthenticatorAssertionResponse.h>
#include <LibWeb/CredentialManagement/AuthenticatorAttestationResponse.h>
#include <LibWeb/CredentialManagement/CredentialsContainer.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/HTML/TemporaryExecutionContext.h>
#include <LibWeb/HTML/Window.h>
#include <LibWeb/WebIDL/AbstractOperations.h>
#include <LibWeb/WebIDL/DOMException.h>
#include <LibWeb/WebIDL/Promise.h>

#if defined(AK_OS_RINOS)
#    include <rin/contract_abi.h>
#    include <rin/web/webcontent_protocol.h>
#endif

namespace Web::CredentialManagement {

GC_DEFINE_ALLOCATOR(CredentialsContainer);

namespace {

#if defined(AK_OS_RINOS)

static void secure_clear(void* data, size_t size)
{
    volatile u8* bytes = static_cast<volatile u8*>(data);
    while (size-- != 0)
        *bytes++ = 0;
}

static void secure_clear(ByteBuffer& buffer)
{
    if (!buffer.is_empty())
        secure_clear(buffer.data(), buffer.size());
}

static bool copy_string_bytes(String const& value, u8* destination,
    u32 capacity, u32& size, bool required)
{
    auto bytes = value.bytes();
    if ((required && bytes.is_empty()) || bytes.size() > capacity)
        return false;
    if (!bytes.is_empty())
        __builtin_memcpy(destination, bytes.data(), bytes.size());
    size = static_cast<u32>(bytes.size());
    return true;
}

static bool copy_buffer_source(GC::Root<WebIDL::BufferSource> const& source,
    u8* destination, u32 capacity, u32& size, bool required)
{
    if (!source)
        return false;
    auto copy = WebIDL::get_buffer_source_copy(*source);
    if (copy.is_error())
        return false;
    auto bytes = copy.release_value();
    if ((required && bytes.is_empty()) || bytes.size() > capacity) {
        secure_clear(bytes);
        return false;
    }
    if (!bytes.is_empty())
        __builtin_memcpy(destination, bytes.data(), bytes.size());
    size = static_cast<u32>(bytes.size());
    secure_clear(bytes);
    return true;
}

static Optional<String> default_rp_id(DOM::Document const& document)
{
    auto origin = document.origin().serialize().to_byte_string();
    auto view = origin.view();
    constexpr StringView https_prefix = "https://"_sv;
    if (!view.starts_with(https_prefix) || view.length() <= https_prefix.length())
        return {};
    size_t host_end = https_prefix.length();
    while (host_end < view.length() && view[host_end] != ':' &&
        view[host_end] != '/')
        ++host_end;
    if (host_end == https_prefix.length())
        return {};
    auto host = view.substring_view(https_prefix.length(),
        host_end - https_prefix.length());
    if (host.contains('[') || host.contains(']'))
        return {};
    auto result = String::from_utf8(host);
    if (result.is_error())
        return {};
    return result.release_value();
}

static bool copy_rp_id(String const& rp_id, RinWebContentWebAuthnEventV1& event)
{
    u32 size = 0;
    if (!copy_string_bytes(rp_id, reinterpret_cast<u8*>(event.rp_id),
            sizeof(event.rp_id) - 1u, size, true) ||
        size > RIN_PASS_WEBAUTHN_RP_ID_MAX)
        return false;
    event.rp_id[size] = '\0';
    event.request.rp_id_size = size;
    __builtin_memcpy(event.request.rp_id, event.rp_id, size);
    return true;
}

static bool map_timeout(Optional<WebIDL::UnsignedLong> timeout, u32& output)
{
    constexpr u32 default_timeout = 60000u;
    constexpr u32 maximum_timeout = RIN_WEBCONTENT_WEBAUTHN_TIMEOUT_MAX_MS;
    output = timeout.has_value() ? timeout.value() : default_timeout;
    if (output == 0u)
        output = 1u;
    if (output > maximum_timeout)
        output = maximum_timeout;
    return true;
}

static bool map_user_verification(Bindings::UserVerificationRequirement value,
    u32& output)
{
    auto name = Bindings::idl_enum_to_string(value);
    if (name == "required"_sv)
        output = RIN_PASS_WEBAUTHN_USER_VERIFICATION_REQUIRED;
    else if (name == "preferred"_sv)
        output = RIN_PASS_WEBAUTHN_USER_VERIFICATION_PREFERRED;
    else if (name == "discouraged"_sv)
        output = RIN_PASS_WEBAUTHN_USER_VERIFICATION_DISCOURAGED;
    else
        return false;
    return true;
}

static bool map_descriptor(PublicKeyCredentialDescriptor const& descriptor,
    RinPassWebAuthnRequestV1& request, u32 index)
{
    if (descriptor.type != "public-key"_string)
        return false;
    u32 credential_size = 0;
    if (!copy_buffer_source(descriptor.id,
            request.credential_ids + index * RIN_PASS_WEBAUTHN_MAX_CREDENTIAL_ID,
            RIN_PASS_WEBAUTHN_MAX_CREDENTIAL_ID, credential_size, true))
        return false;
    request.credential_sizes[index] = static_cast<u16>(credential_size);
    if (!descriptor.transports.has_value())
        return true;
    u16 transport_mask = 0;
    for (auto transport : descriptor.transports.value()) {
        auto name = Bindings::idl_enum_to_string(transport);
        if (name == "usb"_sv)
            transport_mask |= RIN_PASS_WEBAUTHN_TRANSPORT_USB;
        else if (name == "internal"_sv)
            transport_mask |= RIN_PASS_WEBAUTHN_TRANSPORT_INTERNAL;
    }
    if (transport_mask == 0u)
        return false;
    request.credential_transports[index] = transport_mask;
    return true;
}

static bool map_common_options(DOM::Document const& document,
    GC::Ptr<DOM::AbortSignal> signal, RinWebContentWebAuthnEventV1& event,
    bool create)
{
    if (!document.is_fully_active() || document.origin().is_opaque() ||
        (signal && signal->aborted()))
        return false;

    event.struct_size = sizeof(event);
    event.version = RIN_WEBCONTENT_EXTENSION_ABI_VERSION;
    event.event = RIN_WEBCONTENT_WEBAUTHN_EVENT_BEGIN;
    event.operation = create ? RIN_PASS_OPERATION_WEBAUTHN_CREATE
                             : RIN_PASS_OPERATION_WEBAUTHN_GET;
    event.flags = RIN_WEBCONTENT_WEBAUTHN_FLAG_PERMISSIONS_POLICY_ALLOWED |
        RIN_WEBCONTENT_WEBAUTHN_FLAG_ANCESTORS_VALIDATED;
    event.timeout_ms = 60000u;
    auto document_url = document.url().to_byte_string();
    if (document_url.is_empty() || document_url.length() >= sizeof(event.document_url))
        return false;
    __builtin_memcpy(event.document_url, document_url.characters(),
        document_url.length());
    return true;
}

static bool make_creation_event(DOM::Document const& document,
    CredentialCreationOptions const& options,
    PublicKeyCredentialCreationOptions const& public_key,
    RinWebContentWebAuthnEventV1& event)
{
    if (!map_common_options(document, options.signal, event, true) ||
        public_key.exclude_credentials.size() > RIN_PASS_WEBAUTHN_MAX_CREDENTIALS ||
        Bindings::idl_enum_to_string(public_key.attestation) != "none"_sv)
        return false;

    Optional<String> rp_id;
    if (public_key.rp.id.has_value())
        rp_id = public_key.rp.id.value();
    else
        rp_id = default_rp_id(document);
    if (!rp_id.has_value() || !copy_rp_id(rp_id.value(), event) ||
        !copy_buffer_source(public_key.challenge, event.challenge,
            sizeof(event.challenge), event.challenge_size, true) ||
        !copy_string_bytes(public_key.rp.name, event.request.rp_name,
            sizeof(event.request.rp_name), event.request.rp_name_size, true) ||
        !copy_buffer_source(public_key.user.id, event.request.user_id,
            sizeof(event.request.user_id), event.request.user_id_size, true) ||
        !copy_string_bytes(public_key.user.name, event.request.user_name,
            sizeof(event.request.user_name), event.request.user_name_size, true) ||
        !copy_string_bytes(public_key.user.display_name,
            event.request.user_display_name,
            sizeof(event.request.user_display_name),
            event.request.user_display_name_size, true))
        return false;

    event.request.struct_size = sizeof(event.request);
    event.request.version = RIN_PASS_ABI_VERSION;
    event.request.operation = RIN_PASS_OPERATION_WEBAUTHN_CREATE;
    event.request.resident_key = RIN_PASS_WEBAUTHN_RESIDENT_KEY_DISCOURAGED;
    event.request.user_verification = RIN_PASS_WEBAUTHN_USER_VERIFICATION_PREFERRED;
    event.request.attestation = RIN_PASS_WEBAUTHN_ATTESTATION_NONE;
    if (public_key.pub_key_cred_params.is_empty())
        return false;
    bool supports_es256 = false;
    for (auto const& parameter : public_key.pub_key_cred_params) {
        if (parameter.type != "public-key"_string)
            continue;
        if (parameter.alg == -7)
            supports_es256 = true;
    }
    if (!supports_es256)
        return false;
    event.request.algorithm_count = 1u;
    event.request.algorithms[0] = -7; // COSE ES256, the only creation algorithm in the Rin Pass ABI.

    if (public_key.timeout.has_value())
        map_timeout(public_key.timeout, event.timeout_ms);
    if (public_key.authenticator_selection.has_value()) {
        auto const& selection = public_key.authenticator_selection.value();
        if (!map_user_verification(selection.user_verification,
                event.request.user_verification))
            return false;
        if (selection.require_resident_key ||
            Bindings::idl_enum_to_string(selection.resident_key) == "required"_sv)
            event.request.resident_key = RIN_PASS_WEBAUTHN_RESIDENT_KEY_REQUIRED;
        else if (Bindings::idl_enum_to_string(selection.resident_key) == "preferred"_sv)
            event.request.resident_key = RIN_PASS_WEBAUTHN_RESIDENT_KEY_PREFERRED;
        auto attachment = selection.authenticator_attachment;
        if (attachment.has_value()) {
            auto name = Bindings::idl_enum_to_string(attachment.value());
            if (name == "platform"_sv)
                event.request.authenticator = RIN_PASS_WEBAUTHN_AUTHENTICATOR_PLATFORM;
            else if (name == "cross-platform"_sv)
                event.request.authenticator = RIN_PASS_WEBAUTHN_AUTHENTICATOR_SECURITY_KEY;
            else
                return false;
        }
    }
    event.request.credential_count =
        static_cast<u32>(public_key.exclude_credentials.size());
    for (u32 index = 0; index < event.request.credential_count; ++index) {
        if (!map_descriptor(public_key.exclude_credentials[index],
                event.request, index))
            return false;
    }
    return true;
}

static bool make_request_event(DOM::Document const& document,
    CredentialRequestOptions const& options,
    PublicKeyCredentialRequestOptions const& public_key,
    RinWebContentWebAuthnEventV1& event)
{
    if (!map_common_options(document, options.signal, event, false) ||
        public_key.allow_credentials.size() > RIN_PASS_WEBAUTHN_MAX_CREDENTIALS)
        return false;
    Optional<String> rp_id;
    if (public_key.rp_id.has_value())
        rp_id = public_key.rp_id.value();
    else
        rp_id = default_rp_id(document);
    if (!rp_id.has_value() || !copy_rp_id(rp_id.value(), event) ||
        !copy_buffer_source(public_key.challenge, event.challenge,
            sizeof(event.challenge), event.challenge_size, true) ||
        !map_user_verification(public_key.user_verification,
            event.request.user_verification))
        return false;

    event.request.struct_size = sizeof(event.request);
    event.request.version = RIN_PASS_ABI_VERSION;
    event.request.operation = RIN_PASS_OPERATION_WEBAUTHN_GET;
    event.request.resident_key = RIN_PASS_WEBAUTHN_RESIDENT_KEY_NONE;
    event.request.attestation = RIN_PASS_WEBAUTHN_ATTESTATION_NONE;
    if (public_key.timeout.has_value())
        map_timeout(public_key.timeout, event.timeout_ms);
    event.request.credential_count =
        static_cast<u32>(public_key.allow_credentials.size());
    for (u32 index = 0; index < event.request.credential_count; ++index) {
        if (!map_descriptor(public_key.allow_credentials[index],
                event.request, index))
            return false;
    }
    return true;
}

static bool read_cbor_head(ReadonlyBytes bytes, size_t& offset,
    u8& major_type, u64& value)
{
    if (offset >= bytes.size())
        return false;
    u8 first = bytes[offset++];
    major_type = first >> 5;
    u8 additional = first & 0x1f;
    if (additional < 24u) {
        value = additional;
        return true;
    }
    size_t width = additional == 24u ? 1u : additional == 25u ? 2u :
        additional == 26u ? 4u : additional == 27u ? 8u : 0u;
    if (width == 0u || width > bytes.size() - offset)
        return false;
    value = 0;
    while (width-- != 0u)
        value = (value << 8) | bytes[offset++];
    return true;
}

static bool read_cbor_integer(ReadonlyBytes bytes, size_t& offset, i64& value)
{
    u8 major_type;
    u64 encoded;
    if (!read_cbor_head(bytes, offset, major_type, encoded) || encoded > INT64_MAX)
        return false;
    if (major_type == 0u)
        value = static_cast<i64>(encoded);
    else if (major_type == 1u)
        value = -1 - static_cast<i64>(encoded);
    else
        return false;
    return true;
}

static bool skip_cbor_value(ReadonlyBytes bytes, size_t& offset, u32 depth)
{
    if (depth > 8u)
        return false;
    u8 major_type;
    u64 value;
    if (!read_cbor_head(bytes, offset, major_type, value))
        return false;
    if (major_type == 2u || major_type == 3u) {
        if (value > bytes.size() - offset)
            return false;
        offset += static_cast<size_t>(value);
        return true;
    }
    if (major_type == 4u) {
        for (u64 index = 0; index < value; ++index) {
            if (!skip_cbor_value(bytes, offset, depth + 1u))
                return false;
        }
        return true;
    }
    if (major_type == 5u) {
        for (u64 index = 0; index < value; ++index) {
            if (!skip_cbor_value(bytes, offset, depth + 1u) ||
                !skip_cbor_value(bytes, offset, depth + 1u))
                return false;
        }
        return true;
    }
    if (major_type == 6u)
        return skip_cbor_value(bytes, offset, depth + 1u);
    return major_type == 0u || major_type == 1u || major_type == 7u;
}

static bool extract_es256_public_key(ReadonlyBytes authenticator_data,
    ReadonlyBytes credential_id, u8 output[91])
{
    static constexpr u8 spki_prefix[] = {
        0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
        0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01,
        0x07, 0x03, 0x42, 0x00, 0x04
    };
    if (authenticator_data.size() < 55u ||
        (authenticator_data[32] & 0x40u) == 0u)
        return false;
    size_t credential_size_offset = 53u;
    size_t declared_credential_size =
        (static_cast<size_t>(authenticator_data[credential_size_offset]) << 8) |
        authenticator_data[credential_size_offset + 1u];
    if (declared_credential_size != credential_id.size() ||
        declared_credential_size > authenticator_data.size() - 55u)
        return false;
    for (size_t index = 0; index < credential_id.size(); ++index) {
        if (authenticator_data[55u + index] != credential_id[index])
            return false;
    }
    ReadonlyBytes cose = authenticator_data.slice(55u + credential_id.size());
    size_t offset = 0;
    u8 major_type;
    u64 entry_count;
    if (!read_cbor_head(cose, offset, major_type, entry_count) ||
        major_type != 5u || entry_count > 16u)
        return false;
    bool has_kty = false;
    bool has_alg = false;
    bool has_curve = false;
    bool has_x = false;
    bool has_y = false;
    u8 x[32] {};
    u8 y[32] {};
    for (u64 index = 0; index < entry_count; ++index) {
        i64 key;
        if (!read_cbor_integer(cose, offset, key))
            return false;
        if (key == 1 || key == 3 || key == -1) {
            i64 value;
            if (!read_cbor_integer(cose, offset, value))
                return false;
            if (key == 1) has_kty = value == 2;
            if (key == 3) has_alg = value == -7;
            if (key == -1) has_curve = value == 1;
        } else if (key == -2 || key == -3) {
            u8 value_major;
            u64 value_size;
            if (!read_cbor_head(cose, offset, value_major, value_size) ||
                value_major != 2u || value_size != 32u ||
                value_size > cose.size() - offset)
                return false;
            auto* destination = key == -2 ? x : y;
            __builtin_memcpy(destination, cose.data() + offset, 32u);
            offset += 32u;
            if (key == -2) has_x = true;
            else has_y = true;
        } else if (!skip_cbor_value(cose, offset, 0u)) {
            secure_clear(x, sizeof(x));
            secure_clear(y, sizeof(y));
            return false;
        }
    }
    if (offset != cose.size() || !has_kty || !has_alg || !has_curve ||
        !has_x || !has_y) {
        secure_clear(x, sizeof(x));
        secure_clear(y, sizeof(y));
        return false;
    }
    __builtin_memcpy(output, spki_prefix, sizeof(spki_prefix));
    __builtin_memcpy(output + sizeof(spki_prefix), x, sizeof(x));
    __builtin_memcpy(output + sizeof(spki_prefix) + sizeof(x), y, sizeof(y));
    secure_clear(x, sizeof(x));
    secure_clear(y, sizeof(y));
    return true;
}

static bool append_cbor_length(u8* output, size_t capacity, size_t& offset,
    u8 major_type, u64 length)
{
    auto append = [&](u8 value) {
        if (offset >= capacity)
            return false;
        output[offset++] = value;
        return true;
    };
    if (length < 24u)
        return append(static_cast<u8>((major_type << 5) | length));
    if (length <= 0xffu)
        return append(static_cast<u8>((major_type << 5) | 24u)) &&
            append(static_cast<u8>(length));
    if (length <= 0xffffu)
        return append(static_cast<u8>((major_type << 5) | 25u)) &&
            append(static_cast<u8>(length >> 8)) &&
            append(static_cast<u8>(length));
    if (length <= 0xffffffffu)
        return append(static_cast<u8>((major_type << 5) | 26u)) &&
            append(static_cast<u8>(length >> 24)) &&
            append(static_cast<u8>(length >> 16)) &&
            append(static_cast<u8>(length >> 8)) &&
            append(static_cast<u8>(length));
    return false;
}

static bool append_cbor_text(u8* output, size_t capacity, size_t& offset,
    StringView text)
{
    if (!append_cbor_length(output, capacity, offset, 3u, text.length()) ||
        text.length() > capacity - offset)
        return false;
    __builtin_memcpy(output + offset, text.characters_without_null_termination(),
        text.length());
    offset += text.length();
    return true;
}

static bool make_attestation_object(ReadonlyBytes authenticator_data,
    u8* output, size_t capacity, size_t& output_size)
{
    size_t offset = 0;
    if (!append_cbor_length(output, capacity, offset, 5u, 3u) ||
        !append_cbor_text(output, capacity, offset, "fmt"_sv) ||
        !append_cbor_text(output, capacity, offset, "none"_sv) ||
        !append_cbor_text(output, capacity, offset, "authData"_sv) ||
        !append_cbor_length(output, capacity, offset, 2u,
            authenticator_data.size()) ||
        authenticator_data.size() > capacity - offset)
        return false;
    __builtin_memcpy(output + offset, authenticator_data.data(),
        authenticator_data.size());
    offset += authenticator_data.size();
    if (!append_cbor_text(output, capacity, offset, "attStmt"_sv) ||
        !append_cbor_length(output, capacity, offset, 5u, 0u))
        return false;
    output_size = offset;
    return true;
}

static Optional<GC::Ref<JS::ArrayBuffer>> make_array_buffer(JS::Realm& realm,
    ReadonlyBytes bytes)
{
    auto copy = ByteBuffer::copy(bytes.data(), bytes.size());
    if (copy.is_error())
        return {};
    return JS::ArrayBuffer::create(realm, copy.release_value());
}

static void reject_web_authn(JS::Realm& realm, WebIDL::Promise& promise,
    i32 result)
{
    if (result == RIN_RESULT_INTERRUPTED) {
        WebIDL::reject_promise(realm, promise,
            WebIDL::AbortError::create(realm, "WebAuthn request was cancelled"_utf16));
        return;
    }
    WebIDL::reject_promise(realm, promise,
        WebIDL::NotAllowedError::create(realm,
            "The authenticator did not complete the WebAuthn request"_utf16));
}

static void complete_web_authn(JS::Realm& realm, GC::Ref<WebIDL::Promise> promise,
    RinWebContentWebAuthnCompleteV1 const& completion, bool create)
{
    HTML::TemporaryExecutionContext execution_context { realm };
    if (completion.result != RIN_RESULT_OK) {
        reject_web_authn(realm, promise, completion.result);
        return;
    }
    if (!rin_webcontent_client_webauthn_complete_valid(&completion) ||
        completion.result_stream_size < sizeof(RinPassWebAuthnResultHeaderV1) ||
        completion.client_data_json_size == 0u) {
        reject_web_authn(realm, promise, RIN_RESULT_CORRUPT_DATA);
        return;
    }

    RinPassWebAuthnResultHeaderV1 header {};
    __builtin_memcpy(&header, completion.result_stream, sizeof(header));
    u16 expected_kind = create ? RIN_PASS_RESULT_KIND_WEBAUTHN_CREATE
                               : RIN_PASS_RESULT_KIND_WEBAUTHN_GET;
    u64 expected_size = sizeof(header) +
        static_cast<u64>(header.credential_id_size) +
        header.authenticator_data_size + header.signature_size + header.user_id_size;
    if (header.kind != expected_kind || expected_size != completion.result_stream_size ||
        header.credential_id_size == 0u ||
        header.credential_id_size > RIN_PASS_WEBAUTHN_MAX_CREDENTIAL_ID ||
        header.authenticator_data_size < 37u ||
        header.authenticator_data_size > RIN_WEBCONTENT_WEBAUTHN_AUTHENTICATOR_DATA_MAX) {
        reject_web_authn(realm, promise, RIN_RESULT_CORRUPT_DATA);
        return;
    }

    size_t offset = sizeof(header);
    ReadonlyBytes credential_id { completion.result_stream + offset,
        header.credential_id_size };
    offset += header.credential_id_size;
    ReadonlyBytes authenticator_data { completion.result_stream + offset,
        header.authenticator_data_size };
    offset += header.authenticator_data_size;
    ReadonlyBytes signature { completion.result_stream + offset,
        header.signature_size };
    offset += header.signature_size;
    ReadonlyBytes user_handle { completion.result_stream + offset,
        header.user_id_size };
    ReadonlyBytes client_data { completion.client_data_json,
        completion.client_data_json_size };

    auto encoded_id = AK::encode_base64url(credential_id, AK::OmitPadding::Yes);
    if (encoded_id.is_error()) {
        reject_web_authn(realm, promise, RIN_RESULT_NO_MEMORY);
        return;
    }
    auto id = String::from_utf8(encoded_id.value().view());
    auto raw_id = make_array_buffer(realm, credential_id);
    auto client_data_buffer = make_array_buffer(realm, client_data);
    auto authenticator_data_buffer = make_array_buffer(realm, authenticator_data);
    if (id.is_error() || !raw_id.has_value() || !client_data_buffer.has_value() ||
        !authenticator_data_buffer.has_value()) {
        reject_web_authn(realm, promise, RIN_RESULT_NO_MEMORY);
        return;
    }

    Optional<GC::Ref<AuthenticatorResponse>> response = [&]() -> Optional<GC::Ref<AuthenticatorResponse>> {
        if (!create) {
            auto signature_buffer = make_array_buffer(realm, signature);
            if (!signature_buffer.has_value())
                return {};
            GC::Ptr<JS::ArrayBuffer> user_handle_buffer;
            if (!user_handle.is_empty()) {
                auto buffer = make_array_buffer(realm, user_handle);
                if (!buffer.has_value())
                    return {};
                user_handle_buffer = buffer.value();
            }
            return GC::Ref<AuthenticatorResponse> { AuthenticatorAssertionResponse::create(realm,
                client_data_buffer.value(), authenticator_data_buffer.value(),
                signature_buffer.value(), user_handle_buffer) };
        }

        u8 attestation_bytes[RIN_WEBCONTENT_WEBAUTHN_AUTHENTICATOR_DATA_MAX + 64u] {};
        size_t attestation_size = 0;
        if (!make_attestation_object(authenticator_data, attestation_bytes,
                sizeof(attestation_bytes), attestation_size)) {
            secure_clear(attestation_bytes, sizeof(attestation_bytes));
            return {};
        }
        auto attestation_buffer = make_array_buffer(realm,
            ReadonlyBytes { attestation_bytes, attestation_size });
        secure_clear(attestation_bytes, sizeof(attestation_bytes));
        if (!attestation_buffer.has_value())
            return {};

        GC::Ptr<JS::ArrayBuffer> public_key_buffer;
        u8 public_key_bytes[91] {};
        if (extract_es256_public_key(authenticator_data, credential_id,
                public_key_bytes)) {
            auto public_key = make_array_buffer(realm,
                ReadonlyBytes { public_key_bytes, sizeof(public_key_bytes) });
            if (public_key.has_value())
                public_key_buffer = public_key.value();
        }
        secure_clear(public_key_bytes, sizeof(public_key_bytes));
        Vector<Bindings::AuthenticatorTransport> transports;
        transports.append(header.transport == RIN_PASS_WEBAUTHN_RESULT_TRANSPORT_INTERNAL
                ? Bindings::AuthenticatorTransport::Internal
                : Bindings::AuthenticatorTransport::Usb);
        return GC::Ref<AuthenticatorResponse> { AuthenticatorAttestationResponse::create(realm,
            client_data_buffer.value(), attestation_buffer.value(),
            authenticator_data_buffer.value(), public_key_buffer,
            -7, move(transports)) };
    }();

    if (!response.has_value()) {
        reject_web_authn(realm, promise, RIN_RESULT_NO_MEMORY);
        return;
    }
    Optional<String> attachment;
    attachment = header.transport == RIN_PASS_WEBAUTHN_RESULT_TRANSPORT_INTERNAL
        ? "platform"_string : "cross-platform"_string;
    auto credential = PublicKeyCredential::create(realm, id.release_value(),
        raw_id.value(), response.value(), move(attachment));
    WebIDL::resolve_promise(realm, promise, JS::Value(credential.ptr()));
}

static GC::Ref<WebIDL::Promise> request_web_authn(JS::Realm& realm,
    DOM::Document& document, GC::Ptr<DOM::AbortSignal> signal,
    bool create, RinWebContentWebAuthnEventV1& event)
{
    auto promise = WebIDL::create_promise(realm);
    if (signal && signal->aborted()) {
        WebIDL::reject_promise(realm, promise,
            WebIDL::AbortError::create(realm, "WebAuthn request was cancelled"_utf16));
        return promise;
    }
    auto* window = as_if<HTML::Window>(realm.global_object());
    if (window == nullptr || !window->is_secure_context() ||
        window->browsing_context() == nullptr ||
        !window->browsing_context()->is_top_level() ||
        !document.is_allowed_to_use_feature(create
                ? DOM::PolicyControlledFeature::PublicKeyCredentialsCreate
                : DOM::PolicyControlledFeature::PublicKeyCredentialsGet)) {
        WebIDL::reject_promise(realm, promise,
            WebIDL::NotAllowedError::create(realm,
                "WebAuthn is not allowed for this document"_utf16));
        return promise;
    }
    auto weak_document = GC::Weak { document };
    auto identity = window->page().client().page_did_request_webauthn(
        document, signal, event,
        GC::create_function(realm.heap(), [&realm, promise, create,
            weak_document](RinWebContentWebAuthnCompleteV1 const& completion) {
            if (!weak_document || !weak_document->is_fully_active()) {
                HTML::TemporaryExecutionContext execution_context { realm };
                reject_web_authn(realm, promise, RIN_RESULT_INTERRUPTED);
                return;
            }
            complete_web_authn(realm, promise, completion, create);
        }));
    secure_clear(&event, sizeof(event));
    if (identity.navigation_generation == 0u || identity.request_id == 0u)
        WebIDL::reject_promise(realm, promise,
            WebIDL::NotAllowedError::create(realm,
                "The authenticated Browser WebAuthn channel is unavailable"_utf16));
    return promise;
}

#endif

}

GC::Ref<CredentialsContainer> CredentialsContainer::create(JS::Realm& realm)
{
    return realm.create<CredentialsContainer>(realm);
}

CredentialsContainer::~CredentialsContainer() { }

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-get
GC::Ref<WebIDL::Promise> CredentialsContainer::get(CredentialRequestOptions const& options)
{
    auto& realm = *vm().current_realm();
    if (options.public_key.has_value()) {
#if defined(AK_OS_RINOS)
        if (options.signal && options.signal->aborted())
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::AbortError::create(realm, "WebAuthn request was cancelled"_utf16));
        auto mediation = Bindings::idl_enum_to_string(options.mediation);
        if (mediation != "optional"_sv && mediation != "required"_sv)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "The requested WebAuthn mediation mode is not available"_utf16));
        if (options.password.has_value() || options.federated.has_value())
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "Password and federated credentials are not supported"_utf16));
        auto* window = as_if<HTML::Window>(realm.global_object());
        if (window == nullptr)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotAllowedError::create(realm, "WebAuthn requires a Window"_utf16));
        RinWebContentWebAuthnEventV1 event {};
        if (!make_request_event(window->associated_document(), options,
                options.public_key.value(), event)) {
            secure_clear(&event, sizeof(event));
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "The requested WebAuthn options are not supported by Rin Pass"_utf16));
        }
        return request_web_authn(realm, window->associated_document(),
            options.signal, false, event);
#else
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotSupportedError::create(realm, "WebAuthn is unavailable on this platform"_utf16));
#endif
    }
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotSupportedError::create(realm, "Password and federated credentials are unavailable"_utf16));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-store
GC::Ref<WebIDL::Promise> CredentialsContainer::store(Credential const&)
{
    auto& realm = *vm().current_realm();
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotSupportedError::create(realm, "Credential storage is unavailable"_utf16));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-create
GC::Ref<WebIDL::Promise> CredentialsContainer::create(CredentialCreationOptions const& options)
{
    auto& realm = *vm().current_realm();
    if (options.public_key.has_value()) {
#if defined(AK_OS_RINOS)
        if (options.signal && options.signal->aborted())
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::AbortError::create(realm, "WebAuthn request was cancelled"_utf16));
        auto mediation = Bindings::idl_enum_to_string(options.mediation);
        if (mediation != "optional"_sv && mediation != "required"_sv)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "The requested WebAuthn mediation mode is not available"_utf16));
        if (options.password.has_value() || options.federated.has_value())
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "Password and federated credentials are not supported"_utf16));
        auto* window = as_if<HTML::Window>(realm.global_object());
        if (window == nullptr)
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotAllowedError::create(realm, "WebAuthn requires a Window"_utf16));
        RinWebContentWebAuthnEventV1 event {};
        if (!make_creation_event(window->associated_document(), options,
                options.public_key.value(), event)) {
            secure_clear(&event, sizeof(event));
            return WebIDL::create_rejected_promise_from_exception(realm,
                WebIDL::NotSupportedError::create(realm,
                    "The requested WebAuthn options are not supported by Rin Pass"_utf16));
        }
        return request_web_authn(realm, window->associated_document(),
            options.signal, true, event);
#else
        return WebIDL::create_rejected_promise_from_exception(realm,
            WebIDL::NotSupportedError::create(realm, "WebAuthn is unavailable on this platform"_utf16));
#endif
    }
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotSupportedError::create(realm, "Password and federated credentials are unavailable"_utf16));
}

// https://www.w3.org/TR/credential-management-1/#dom-credentialscontainer-preventsilentaccess
GC::Ref<WebIDL::Promise> CredentialsContainer::prevent_silent_access()
{
    auto& realm = *vm().current_realm();
    return WebIDL::create_rejected_promise_from_exception(realm,
        WebIDL::NotSupportedError::create(realm, "Origin credential mediation state cannot be persisted"_utf16));
}

CredentialsContainer::CredentialsContainer(JS::Realm& realm)
    : PlatformObject(realm)
{
}

void CredentialsContainer::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(CredentialsContainer);
    Base::initialize(realm);
}

}
