/*
 * Copyright (c) 2024, Andrew Kaster <andrew@ladybird.org>
 * Copyright (c) 2025, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Enumerate.h>
#include <LibCore/File.h>
#include <RequestServer/CURL.h>

namespace RequestServer {

ByteString build_curl_resolve_list(DNS::LookupResult const& dns_result, StringView host, u16 port)
{
    StringBuilder resolve_opt_builder;
    resolve_opt_builder.appendff("{}:{}:", host, port);

    for (auto const& [i, addr] : enumerate(dns_result.cached_addresses())) {
        auto formatted_address = addr.visit(
            [&](IPv4Address const& ipv4) { return ipv4.to_byte_string(); },
            [&](IPv6Address const& ipv6) { return MUST(ipv6.to_string()).to_byte_string(); });

        if (i > 0)
            resolve_opt_builder.append(',');
        resolve_opt_builder.append(formatted_address);
    }

    dbgln_if(REQUESTSERVER_DEBUG, "RequestServer: Resolve list: {}", resolve_opt_builder.string_view());
    return resolve_opt_builder.to_byte_string();
}

bool set_curl_certificate_paths(CURL* handle, Vector<ByteString> const& paths,
                                Optional<ByteBuffer>& storage)
{
    if (!handle || paths.is_empty())
        return paths.is_empty();

    if (paths.size() == 1)
        return curl_easy_setopt(handle, CURLOPT_CAINFO,
                   paths.first().characters()) == CURLE_OK;

#if LIBCURL_VERSION_NUM >= 0x074D00
    constexpr size_t max_ca_bundle_size = 4 * 1024 * 1024;
    ByteBuffer combined;
    for (auto const& path : paths) {
        auto file_or_error = Core::File::open(path, Core::File::OpenMode::Read);
        if (file_or_error.is_error()) {
            dbgln("RequestServer: unable to open CA bundle '{}': {}", path, file_or_error.error());
            return false;
        }
        auto file = file_or_error.release_value();
        auto size_or_error = file->size();
        if (size_or_error.is_error()) {
            dbgln("RequestServer: unable to size CA bundle '{}': {}", path, size_or_error.error());
            return false;
        }
        auto size = size_or_error.release_value();
        if (size == 0 || size > max_ca_bundle_size ||
            combined.size() > max_ca_bundle_size - size) {
            dbgln("RequestServer: CA bundle '{}' exceeds the bounded size", path);
            return false;
        }
        auto bytes = ByteBuffer::create_uninitialized(size);
        if (bytes.is_error() || file->read_until_filled(bytes.value().bytes()).is_error()) {
            dbgln("RequestServer: unable to read CA bundle '{}'", path);
            return false;
        }
        if (combined.try_append(bytes.value().bytes()).is_error() ||
            combined.try_append(static_cast<u8>('\n')).is_error())
            return false;
    }

    curl_blob blob { combined.data(), combined.size(), CURL_BLOB_COPY };
    if (curl_easy_setopt(handle, CURLOPT_CAINFO_BLOB, &blob) != CURLE_OK)
        return false;
    storage = move(combined);
    return true;
#else
    dbgln("RequestServer: this curl does not support multiple CA bundles");
    return false;
#endif
}

Requests::NetworkError curl_code_to_network_error(int code)
{
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
        return Requests::NetworkError::UnableToResolveHost;
    case CURLE_COULDNT_RESOLVE_PROXY:
        return Requests::NetworkError::UnableToResolveProxy;
    case CURLE_COULDNT_CONNECT:
        return Requests::NetworkError::UnableToConnect;
    case CURLE_OPERATION_TIMEDOUT:
        return Requests::NetworkError::TimeoutReached;
    case CURLE_TOO_MANY_REDIRECTS:
        return Requests::NetworkError::TooManyRedirects;
    case CURLE_SSL_CONNECT_ERROR:
        return Requests::NetworkError::SSLHandshakeFailed;
    case CURLE_PEER_FAILED_VERIFICATION:
        return Requests::NetworkError::SSLVerificationFailed;
    case CURLE_URL_MALFORMAT:
        return Requests::NetworkError::MalformedUrl;
    case CURLE_PARTIAL_FILE:
        return Requests::NetworkError::IncompleteContent;
    case CURLE_BAD_CONTENT_ENCODING:
        return Requests::NetworkError::InvalidContentEncoding;
    default:
        return Requests::NetworkError::Unknown;
    }
}

}
