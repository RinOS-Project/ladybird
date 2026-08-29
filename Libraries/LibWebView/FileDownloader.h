/*
 * Copyright (c) 2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/Function.h>
#include <AK/HashMap.h>
#if !defined(AK_OS_RINOS)
#include <AK/LexicalPath.h>
#endif
#include <AK/NonnullRefPtr.h>
#include <LibRequests/Forward.h>
#include <LibURL/Forward.h>
#include <LibWebView/Forward.h>

namespace WebView {

class WEBVIEW_API FileDownloader {
public:
    FileDownloader();
    ~FileDownloader();

#if defined(AK_OS_RINOS)
    /* These values cross only the Browser/bridge trust boundary. They are
     * deliberately not server text and there is no successful-completion
     * value: File Manager alone can attest durable publication. */
    enum class DownloadFailure : u8 {
        Cancelled,
        UnsafeURL,
        UnsafeFilename,
        SizeRejected,
        FileManagerUnavailable,
        DurabilityFailed,
        TLSFailed,
        NetworkFailed,
        HttpFailed,
        ResponseInvalid,
        MemoryFailed,
        TransferFailed,
    };
    using DownloadFailureCallback = Function<void(u64, DownloadFailure)>;
    enum class DownloadEvent : u8 {
        Started,
        Completed,
    };
    using DownloadEventCallback = Function<void(u64, DownloadEvent,
                                                ByteString, ByteString, u64)>;

    /* RinOS never accepts a caller-selected pathname for a web download.
     * File Manager owns the interactive destination selection and returns a
     * short-lived write-only File Portal descriptor only after approval.
     * `suggested_filename` is untrusted presentation data from WebContent;
     * it is checked again by the Browser/File Manager portal contract. */
    void download_file(URL::URL const&, ByteString suggested_filename = {},
                       DownloadFailureCallback = {},
                       DownloadEventCallback = {}, u64 transfer_id = 0);
    /* Stop a live request by its opaque transfer identity. The transfer
     * object owns the portal abort and emits a single Cancelled event. */
    bool cancel_download(u64 transfer_id);
#else
    void download_file(URL::URL const&, LexicalPath);
#endif

private:
    HashMap<u64, NonnullRefPtr<Requests::Request>> m_requests;
#if defined(AK_OS_RINOS)
    HashMap<u64, Function<void()>> m_cancel_callbacks;
#endif
};

}
