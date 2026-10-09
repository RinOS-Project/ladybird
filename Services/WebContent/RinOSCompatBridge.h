/* SPDX-License-Identifier: MIT */
/* Private Ladybird/WebContent entry points for the Browser TLS key owner. */
#pragma once

#include <AK/ByteBuffer.h>
#include <AK/ReadonlyBytes.h>
#include <LibURL/URL.h>

namespace RinWebContentBridge {

bool request_tls_client_certificate(
    AK::u64 request_id, URL::URL const& url,
    AK::ReadonlyBytes signature_algorithms,
    AK::ReadonlyBytes signature_algorithms_cert,
    AK::ReadonlyBytes certificate_authorities,
    AK::u64& connection_generation, AK::u16& signature_scheme,
    AK::ByteBuffer& certificate_list, AK::ByteBuffer& signer_capability);

bool sign_tls_client_certificate(
    AK::u64 request_id, AK::u64 connection_generation,
    AK::ReadonlyBytes signer_capability, AK::u16 signature_scheme,
    AK::ReadonlyBytes message, AK::ByteBuffer& signature);

}
