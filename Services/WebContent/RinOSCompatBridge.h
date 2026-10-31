/* SPDX-License-Identifier: MIT */
/* Private Ladybird/WebContent entry points for the Browser TLS key owner. */
#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Span.h>
#include <AK/Types.h>
#include <LibURL/URL.h>
#include <vector>
#include <rin/web/webcontent_protocol.h>

namespace RinWebContentBridge {

bool request_tls_client_certificate(
    u64 request_id, URL::URL const& url,
    AK::ReadonlyBytes signature_algorithms,
    AK::ReadonlyBytes signature_algorithms_cert,
    AK::ReadonlyBytes certificate_authorities,
    u64& connection_generation, u16& signature_scheme,
    AK::ByteBuffer& certificate_list, AK::ByteBuffer& signer_capability);

bool sign_tls_client_certificate(
    u64 request_id, u64 connection_generation,
    AK::ReadonlyBytes signer_capability, u16 signature_scheme,
    AK::ReadonlyBytes message, AK::ByteBuffer& signature);

bool exchange_clipboard_owner(
    u32 page_id, const void* request_payload, size_t request_payload_size,
    RinWebContentClipboardOwnerResponseV1& response,
    std::vector<u8>& response_data);

}
