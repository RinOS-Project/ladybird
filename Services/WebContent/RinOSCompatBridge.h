/* SPDX-License-Identifier: MIT */
/* Private Ladybird/WebContent entry points for the Browser TLS key owner. */
#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Span.h>
#include <AK/Types.h>
#include <LibURL/URL.h>

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

}
