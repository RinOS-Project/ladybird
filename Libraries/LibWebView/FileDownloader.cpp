/*
 * Copyright (c) 2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#if !defined(AK_OS_RINOS)
#    include <LibCore/File.h>
#else
#    include <AK/ByteBuffer.h>
#    include <AK/RefCounted.h>
#    include <AK/StdLibExtras.h>
#    include <AK/WeakPtr.h>
#    include "../../../../src/apps/browser/rin_browser_download_portal_client.h"
#    include "../../../../src/apps/browser/rin_browser_download_filename.h"
#    include <LibCore/EventLoop.h>
#    include <LibThreading/ConditionVariable.h>
#    include <LibThreading/Mutex.h>
#    include <LibThreading/Thread.h>
#endif
#include <LibHTTP/HeaderList.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#include <LibWeb/Loader/UserAgent.h>
#include <LibWebView/Application.h>
#include <LibWebView/FileDownloader.h>

namespace WebView {

FileDownloader::FileDownloader() = default;
FileDownloader::~FileDownloader() = default;

#if !defined(AK_OS_RINOS)
static ErrorOr<void> save_file(LexicalPath const& destination, ReadonlyBytes data)
{
    auto file = TRY(Core::File::open(destination.string(), Core::File::OpenMode::Write));
    TRY(file->write_until_depleted(data));
    return {};
}
#endif

#if defined(AK_OS_RINOS)
static StringView rinos_download_portal_error(RinBrowserDownloadPolicyResult result)
{
    switch (result) {
    case RIN_BROWSER_DOWNLOAD_POLICY_CANCELLED:
        return "Download cancelled"sv;
    case RIN_BROWSER_DOWNLOAD_POLICY_UNSAFE_URL:
        return "Download requires an authenticated HTTPS URL"sv;
    case RIN_BROWSER_DOWNLOAD_POLICY_UNSAFE_FILENAME:
        return "Download name is not safe to save"sv;
    case RIN_BROWSER_DOWNLOAD_POLICY_SIZE_REJECTED:
        return "Download size is not supported"sv;
    case RIN_BROWSER_DOWNLOAD_POLICY_BACKEND_UNAVAILABLE:
        return "File Manager save service is unavailable"sv;
    case RIN_BROWSER_DOWNLOAD_POLICY_DURABILITY_FAILED:
        return "Download could not be saved durably"sv;
    default:
        return "Secure download transfer failed"sv;
    }
}

static FileDownloader::DownloadFailure rinos_download_failure(RinBrowserDownloadPolicyResult result)
{
    switch (result) {
    case RIN_BROWSER_DOWNLOAD_POLICY_CANCELLED:
        return FileDownloader::DownloadFailure::Cancelled;
    case RIN_BROWSER_DOWNLOAD_POLICY_UNSAFE_URL:
        return FileDownloader::DownloadFailure::UnsafeURL;
    case RIN_BROWSER_DOWNLOAD_POLICY_UNSAFE_FILENAME:
        return FileDownloader::DownloadFailure::UnsafeFilename;
    case RIN_BROWSER_DOWNLOAD_POLICY_SIZE_REJECTED:
        return FileDownloader::DownloadFailure::SizeRejected;
    case RIN_BROWSER_DOWNLOAD_POLICY_BACKEND_UNAVAILABLE:
        return FileDownloader::DownloadFailure::FileManagerUnavailable;
    case RIN_BROWSER_DOWNLOAD_POLICY_DURABILITY_FAILED:
        return FileDownloader::DownloadFailure::DurabilityFailed;
    default:
        return FileDownloader::DownloadFailure::TransferFailed;
    }
}

static bool rinos_download_tls_failure(Requests::NetworkError error)
{
    return error == Requests::NetworkError::SSLHandshakeFailed ||
           error == Requests::NetworkError::SSLVerificationFailed;
}

class RinDownloadFailureReporter final : public RefCounted<RinDownloadFailureReporter> {
public:
    explicit RinDownloadFailureReporter(FileDownloader::DownloadFailureCallback callback)
        : m_callback(move(callback))
    {
    }

    void report(u64 transfer_id, FileDownloader::DownloadFailure failure)
    {
        if (m_callback)
            m_callback(transfer_id, failure);
    }

private:
    FileDownloader::DownloadFailureCallback m_callback;
};

class RinDownloadEventReporter final : public RefCounted<RinDownloadEventReporter> {
public:
    explicit RinDownloadEventReporter(FileDownloader::DownloadEventCallback callback)
        : m_callback(move(callback))
    {
    }

    void report(u64 transfer_id, FileDownloader::DownloadEvent event,
                ByteString url, ByteString filename, u64 size)
    {
        if (m_callback)
            m_callback(transfer_id, event, move(url), move(filename), size);
    }

private:
    FileDownloader::DownloadEventCallback m_callback;
};

class RinPortalStreamingTransfer final : public RefCounted<RinPortalStreamingTransfer> {
public:
    enum class EnqueueResult {
        Accepted,
        NotReady,
        MemoryFailure,
    };

    RinPortalStreamingTransfer(WeakPtr<Requests::Request> request,
        NonnullRefPtr<Core::WeakEventLoopReference> browser_event_loop,
        NonnullRefPtr<RinDownloadFailureReporter> failure_reporter,
        NonnullRefPtr<RinDownloadEventReporter> event_reporter,
        u64 transfer_id)
        : m_request(move(request))
        , m_browser_event_loop(move(browser_event_loop))
        , m_failure_reporter(move(failure_reporter))
        , m_event_reporter(move(event_reporter))
        , m_transfer_id(transfer_id)
        , m_changed(m_mutex)
    {
    }

    bool begin_receiving()
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_receiving_started || m_failed)
            return false;
        m_receiving_started = true;
        return true;
    }

    EnqueueResult enqueue(ReadonlyBytes bytes)
    {
        auto copy = ByteBuffer::copy(bytes);
        if (copy.is_error())
            return EnqueueResult::MemoryFailure;

        Threading::MutexLocker locker(m_mutex);
        if (!m_receiving_started || m_failed || m_response_finished || m_chunk_ready)
            return EnqueueResult::NotReady;
        m_chunk = copy.release_value();
        m_chunk_ready = true;
        m_changed.signal();
        return EnqueueResult::Accepted;
    }

    void response_finished(u64 total_size, Optional<Requests::NetworkError> network_error)
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_failed || m_response_finished)
            return;
        m_response_total_size = total_size;
        m_network_error = move(network_error);
        m_response_finished = true;
        m_changed.broadcast();
    }

    void run(ByteString url, ByteString filename, u64 content_length)
    {
        RinBrowserDownloadPortalSessionV1 session {};
        rin_browser_download_portal_session_init(&session);
        auto url_view = url.view();
        auto filename_view = filename.view();
        auto result = rin_browser_download_portal_session_begin(
            &session,
            url_view.characters_without_null_termination(), url_view.length(),
            filename_view.characters_without_null_termination(), filename_view.length(),
            content_length);
        if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
            fail(rinos_download_failure(result), rinos_download_portal_error(result));
            return;
        }

        for (;;) {
            Optional<ByteBuffer> chunk;
            Optional<Requests::NetworkError> network_error;
            u64 response_total_size = 0;
            {
                Threading::MutexLocker locker(m_mutex);
                m_changed.wait_while([&] {
                    return !m_chunk_ready && !m_response_finished && !m_failed;
                });
                if (m_failed) {
                    (void)rin_browser_download_portal_session_abort(&session);
                    return;
                }
                if (m_chunk_ready) {
                    chunk = move(m_chunk);
                    m_chunk_ready = false;
                } else {
                    VERIFY(m_response_finished);
                    network_error = move(m_network_error);
                    response_total_size = m_response_total_size;
                }
            }

            if (chunk.has_value()) {
                auto bytes = chunk->bytes();
                size_t offset = 0;
                while (offset < bytes.size()) {
                    auto remaining = bytes.size() - offset;
                    auto write_size = AK::min(remaining,
                        static_cast<size_t>(RIN_BROWSER_DOWNLOAD_MAX_CHUNK_BYTES));
                    result = rin_browser_download_portal_session_write(
                        &session, bytes.data() + offset, write_size);
                    if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
                        (void)rin_browser_download_portal_session_abort(&session);
                        fail(rinos_download_failure(result), rinos_download_portal_error(result));
                        return;
                    }
                    offset += write_size;
                }
                auto event_loop = m_browser_event_loop->take();
                if (!event_loop.is_alive()) {
                    (void)rin_browser_download_portal_session_abort(&session);
                    return;
                }
                event_loop->deferred_invoke([request = m_request] {
                    if (auto strong_request = request.strong_ref())
                        (void)strong_request->resume_receiving();
                });
                continue;
            }

            if (network_error.has_value()) {
                (void)rin_browser_download_portal_session_abort(&session);
                if (rinos_download_tls_failure(*network_error)) {
                    fail(FileDownloader::DownloadFailure::TLSFailed,
                        "Secure connection failed before download"sv);
                } else {
                    fail(FileDownloader::DownloadFailure::NetworkFailed,
                        "Network download failed"sv);
                }
                return;
            }
            if ((content_length ==
                     RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                 (response_total_size == 0u ||
                  response_total_size != session.written_length)) ||
                (content_length !=
                     RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                 response_total_size != content_length)) {
                (void)rin_browser_download_portal_session_abort(&session);
                fail(FileDownloader::DownloadFailure::ResponseInvalid,
                    "Download response length changed during transfer"sv);
                return;
            }
            RinBrowserDownloadPortalClientReceiptV1 receipt {};
            result = rin_browser_download_portal_session_finish(&session, &receipt);
            if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
                fail(rinos_download_failure(result), rinos_download_portal_error(result));
            } else {
                auto event_loop = m_browser_event_loop->take();
                if (event_loop.is_alive()) {
                    event_loop->deferred_invoke([
                        event_reporter = m_event_reporter,
                        transfer_id = m_transfer_id,
                        url = move(url), filename = move(filename),
                        size = receipt.content_length]() mutable {
                        event_reporter->report(
                            transfer_id, FileDownloader::DownloadEvent::Completed,
                            move(url), move(filename), size);
                    });
                }
            }
            return;
        }
    }

    void fail(FileDownloader::DownloadFailure failure, StringView message)
    {
        {
            Threading::MutexLocker locker(m_mutex);
            if (m_failed)
                return;
            m_failed = true;
            m_changed.broadcast();
        }
        auto event_loop = m_browser_event_loop->take();
        if (!event_loop.is_alive())
            return;
        auto transfer_id = m_transfer_id;
        event_loop->deferred_invoke([request = m_request,
                                     failure_reporter = m_failure_reporter,
                                     failure, message, transfer_id] {
            if (auto strong_request = request.strong_ref())
                (void)strong_request->stop();
            failure_reporter->report(transfer_id, failure);
            Application::the().display_error_dialog(message);
        });
    }

    void cancel()
    {
        fail(FileDownloader::DownloadFailure::Cancelled,
             "Download cancelled"sv);
    }

private:
    WeakPtr<Requests::Request> m_request;
    NonnullRefPtr<Core::WeakEventLoopReference> m_browser_event_loop;
    NonnullRefPtr<RinDownloadFailureReporter> m_failure_reporter;
    NonnullRefPtr<RinDownloadEventReporter> m_event_reporter;
    u64 m_transfer_id { 0 };
    Threading::Mutex m_mutex;
    Threading::ConditionVariable m_changed;
    ByteBuffer m_chunk;
    Optional<Requests::NetworkError> m_network_error;
    u64 m_response_total_size { 0 };
    bool m_receiving_started { false };
    bool m_chunk_ready { false };
    bool m_response_finished { false };
    bool m_failed { false };
};
#endif

#if defined(AK_OS_RINOS)
void FileDownloader::download_file(URL::URL const& url, ByteString suggested_filename,
                                    DownloadFailureCallback on_failure,
                                    DownloadEventCallback on_event,
                                    u64 requested_transfer_id)
#else
void FileDownloader::download_file(URL::URL const& url, LexicalPath destination)
#endif
{
    static u64 next_request_id = 1;

    // FIXME: What other request headers should be set? Perhaps we want to use exactly the same request headers used to
    //        originally fetch the image in WebContent.
    auto request_headers = HTTP::HeaderList::create();
    request_headers->set({ "User-Agent"sv, Web::default_user_agent });

#if defined(AK_OS_RINOS)
    auto failure_reporter = adopt_ref(*new RinDownloadFailureReporter(move(on_failure)));
    auto event_reporter = adopt_ref(*new RinDownloadEventReporter(move(on_event)));
#endif
    auto request = Application::request_server_client().start_request("GET"sv, url, *request_headers);
    if (!request) {
#if defined(AK_OS_RINOS)
        failure_reporter->report(0u, DownloadFailure::TransferFailed);
#endif
        Application::the().display_error_dialog("Unable to start request to download file"sv);
        return;
    }

    auto request_id = next_request_id++;
    if (request_id == 0)
        request_id = next_request_id++;
    auto transfer_id = requested_transfer_id != 0 ? requested_transfer_id : request_id;

#if defined(AK_OS_RINOS)
    auto browser_event_loop = Core::EventLoop::current_weak();
    WeakPtr<Requests::Request> request_weak = request;
    auto stream = adopt_ref(*new RinPortalStreamingTransfer(
        request_weak, move(browser_event_loop), failure_reporter,
        event_reporter, transfer_id));
    m_cancel_callbacks.set(transfer_id, [stream] { stream->cancel(); });
    request->set_unbuffered_request_callbacks(
        [url, suggested_filename = move(suggested_filename), stream,
            event_reporter, request_id](NonnullRefPtr<HTTP::HeaderList> response_headers,
                            Optional<u32> response_code,
                            Optional<String> const&) mutable {
            if (response_code.has_value() && *response_code >= 400) {
                stream->fail(DownloadFailure::HttpFailed,
                    "Download server returned an error response"sv);
                return;
            }
            if (!response_code.has_value() || *response_code < 200 || *response_code >= 300) {
                stream->fail(DownloadFailure::ResponseInvalid,
                    "Download did not receive a successful HTTP response"sv);
                return;
            }

            auto content_length = response_headers->extract_length();
            u64 declared_length = RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH;
            if (content_length.has<u64>()) {
                declared_length = content_length.get<u64>();
                if (declared_length == 0u ||
                    declared_length > RIN_BROWSER_DOWNLOAD_MAX_BYTES) {
                    stream->fail(DownloadFailure::ResponseInvalid,
                        "Download response Content-Length is not supported"sv);
                    return;
                }
            } else if (!content_length.has<Empty>() ||
                       response_headers->get("Content-Length"sv).has_value()) {
                stream->fail(DownloadFailure::ResponseInvalid,
                    "Download response has a malformed Content-Length"sv);
                return;
            }

            char server_filename[RIN_BROWSER_DOWNLOAD_MAX_FILENAME_BYTES + 1u];
            size_t server_filename_size = 0u;
            if (auto content_disposition = response_headers->get("Content-Disposition"sv); content_disposition.has_value()
                && rin_browser_download_filename_from_content_disposition(
                    content_disposition->characters(), content_disposition->length(),
                    server_filename, sizeof(server_filename), &server_filename_size)) {
                suggested_filename = ByteString { server_filename, server_filename_size };
            }
            if (suggested_filename.is_empty())
                suggested_filename = url.basename();
            if (suggested_filename.is_empty())
                suggested_filename = "download"sv;
            if (!stream->begin_receiving()) {
                stream->fail(DownloadFailure::TransferFailed,
                    "Secure download transfer could not start"sv);
                return;
            }

            event_reporter->report(
                request_id, FileDownloader::DownloadEvent::Started,
                url.serialize().to_byte_string(), suggested_filename,
                declared_length);

            auto worker = Threading::Thread::try_create("rin-download"sv,
                [stream,
                    serialized_url = url.serialize().to_byte_string(),
                    filename = move(suggested_filename),
                    length = declared_length]() mutable -> intptr_t {
                    stream->run(move(serialized_url), move(filename), length);
                    return 0;
                });
            if (worker.is_error()) {
                stream->fail(DownloadFailure::TransferFailed,
                    "Unable to start secure download transfer"sv);
                return;
            }
            auto transfer_thread = worker.release_value();
            transfer_thread->start();
            transfer_thread->detach();
        },
        [stream, request_weak](ReadonlyBytes bytes) {
            auto strong_request = request_weak.strong_ref();
            if (!strong_request || !strong_request->pause_receiving()) {
                stream->fail(DownloadFailure::TransferFailed,
                    "Secure download stream lost its backpressure control"sv);
                return;
            }
            switch (stream->enqueue(bytes)) {
            case RinPortalStreamingTransfer::EnqueueResult::Accepted:
                return;
            case RinPortalStreamingTransfer::EnqueueResult::MemoryFailure:
                stream->fail(DownloadFailure::MemoryFailed,
                    "Unable to reserve download data for File Manager"sv);
                return;
            case RinPortalStreamingTransfer::EnqueueResult::NotReady:
                stream->fail(DownloadFailure::ResponseInvalid,
                    "Download body arrived before validated response headers"sv);
                return;
            }
            VERIFY_NOT_REACHED();
        },
        [this, request_id, transfer_id, stream](u64 total_size, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> network_error) {
            Core::deferred_invoke([this, request_id, transfer_id] {
                m_requests.remove(request_id);
                m_cancel_callbacks.remove(transfer_id);
            });
            stream->response_finished(total_size, move(network_error));
        });
#else
    request->set_buffered_request_finished_callback(
        [this, request_id, destination = move(destination)](u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> const& network_error, HTTP::HeaderList const&, Optional<u32> response_code, Optional<String> const& reason_phrase, ReadonlyBytes payload) {
            Core::deferred_invoke([this, request_id]() { m_requests.remove(request_id); });

            if (network_error.has_value()) {
                auto error = MUST(String::formatted("Unable to download file: {}", Requests::network_error_to_string(*network_error)));
                Application::the().display_error_dialog(error);
                return;
            }
            if (response_code.has_value() && *response_code >= 400) {
                auto error = reason_phrase.has_value()
                    ? MUST(String::formatted("Received error response code {} while downloading file: {}", *response_code, reason_phrase))
                    : MUST(String::formatted("Received error response code {} while downloading file", *response_code));
                Application::the().display_error_dialog(error);
                return;
            }
            if (auto result = save_file(destination, payload); result.is_error()) {
                auto error = MUST(String::formatted("Unable to save downloaded file file: {}", result.error()));
                Application::the().display_error_dialog(error);
            }
        });
#endif

    m_requests.set(request_id, request.release_nonnull());
}

bool FileDownloader::cancel_download(u64 transfer_id)
{
    if (transfer_id == 0)
        return false;
    auto callback = m_cancel_callbacks.take(transfer_id);
    if (!callback.has_value())
        return false;
    (*callback)();
    return true;
}

}
