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
#    include "../../../../public-base/libs/rinruntime/include/rinruntime/download_policy.h"
#    include <LibCore/EventLoop.h>
#    include <LibThreading/ConditionVariable.h>
#    include <LibThreading/Mutex.h>
#    include <LibThreading/Thread.h>
#    include "../../../../public-base/libs/rinruntime/include/rinruntime/download_resume.hpp"
extern "C" int rin_current_process_instance_cookie(u64* cookieOut)
    __attribute__((weak));
#endif
#include <LibHTTP/HeaderList.h>
#include <LibRequests/Request.h>
#include <LibRequests/RequestClient.h>
#include <LibURL/Parser.h>
#include <LibURL/URL.h>
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
static u64 next_rinos_download_identity()
{
    static u64 process_cookie = [] {
        u64 cookie = 0u;
        if (rin_current_process_instance_cookie == nullptr ||
            rin_current_process_instance_cookie(&cookie) != 0 || cookie == 0u)
            return u64 { 0u };
        return cookie;
    }();
    static u64 sequence = 0u;
    if (process_cookie == 0u || process_cookie > (UINT64_MAX >> 16u) ||
        sequence >= UINT16_MAX)
        return 0u;
    ++sequence;
    return (process_cookie << 16u) | sequence;
}

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

static bool rinos_download_strong_etag(StringView value)
{
    if (value.length() < 2u || value.length() > 127u || value[0] != '"' ||
        value[value.length() - 1u] != '"')
        return false;
    for (size_t index = 1u; index + 1u < value.length(); ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte < 0x21u || byte > 0x7eu || byte == '"')
            return false;
    }
    return true;
}

static constexpr u8 s_max_download_redirects = 8u;

static bool rinos_download_https_url(URL::URL const& url)
{
    auto serialized = url.serialize().to_byte_string();
    return !url.includes_credentials() &&
           rin_browser_download_url_valid(
               serialized.characters(),
               serialized.length());
}

static bool rinos_download_redirect_status(u32 status)
{
    return status == 301u || status == 302u || status == 303u ||
           status == 307u || status == 308u;
}

static Optional<URL::URL> rinos_download_redirect_target(
    URL::URL const& source, HTTP::HeaderList const& response_headers,
    bool range_resume, u8 redirect_hops)
{
    if (redirect_hops >= s_max_download_redirects)
        return {};
    Optional<ByteString> location;
    bool duplicate_location = false;
    response_headers.for_each_header_value("Location"sv,
        [&](StringView value) {
            if (location.has_value()) {
                duplicate_location = true;
                return IterationDecision::Break;
            }
            location = ByteString { value };
            return IterationDecision::Continue;
        });
    if (!location.has_value() || duplicate_location || location->is_empty())
        return {};
    auto target = source.complete_url(location->view());
    if (!target.has_value() || !rinos_download_https_url(*target) ||
        (source.scheme() == "https"sv && target->scheme() != "https"sv) ||
        (range_resume &&
         !source.origin().is_same_origin(target->origin())))
        return {};
    target->set_fragment({});
    return target;
}

static ByteString rinos_download_referrer_for_url(
    URL::URL const& target, ByteString const& referrer)
{
    if (referrer.is_empty())
        return {};
    auto parsed = URL::Parser::basic_parse(referrer);
    if (!parsed.has_value() || !rinos_download_https_url(*parsed) ||
        !target.origin().is_same_origin(parsed->origin()))
        return {};
    return ByteString { parsed->origin().serialize().to_byte_string() };
}

static ByteString rinos_download_fallback_filename(
    URL::URL const& url, ByteString suggested_filename)
{
    if (!suggested_filename.is_empty() &&
        rin_browser_download_filename_valid(
            suggested_filename.characters(),
            suggested_filename.length()))
        return suggested_filename;
    auto filename = url.basename();
    if (filename.is_empty() ||
        !rin_browser_download_filename_valid(
            filename.characters(), filename.length()))
        return "download"sv;
    return filename;
}

class RinDownloadFailureReporter final : public RefCounted<RinDownloadFailureReporter> {
public:
    explicit RinDownloadFailureReporter(FileDownloader::DownloadFailureCallback callback)
        : m_callback(move(callback))
    {
    }

    void report(u64 transfer_id, FileDownloader::DownloadFailure failure)
    {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
#endif
        if (m_callback)
            m_callback(transfer_id, failure);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        } catch (...) {
            /* The Browser bridge is an owner callback.  A callback failure
             * must not unwind through the event loop or the transfer worker;
             * the transfer has already been closed before this report. */
        }
#endif
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
                ByteString url, ByteString referrer, ByteString filename, u64 size,
                u64 generation, ByteString validator, u64 committed_bytes)
    {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        try {
#endif
        if (m_callback)
            m_callback(transfer_id, event, move(url), move(referrer),
                       move(filename), size, generation, move(validator),
                       committed_bytes);
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
        } catch (...) {
            /* Event delivery is advisory metadata.  Never let a Browser
             * owner callback tear down the worker or event-loop boundary. */
        }
#endif
    }

private:
    FileDownloader::DownloadEventCallback m_callback;
};

class RinPortalStreamingTransfer final
    : public RefCounted<RinPortalStreamingTransfer> {
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

    bool begin_receiving(bool can_pause)
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_receiving_started || m_failed)
            return false;
        m_receiving_started = true;
        m_can_pause = can_pause;
        return true;
    }

    bool pause()
    {
        {
            Threading::MutexLocker locker(m_mutex);
            if (!m_can_pause || m_failed || m_suspended || m_pause_requested ||
                m_response_finished)
                return false;
            m_pause_requested = true;
            m_changed.broadcast();
        }
        auto request = m_request.strong_ref();
        if (!request) {
            Threading::MutexLocker locker(m_mutex);
            m_pause_requested = false;
            m_changed.broadcast();
            return false;
        }
        /* The data callback also pauses reads while a chunk is waiting for
         * the portal writer.  In that case pause_receiving() reports that
         * reads are already paused; the durable pause request is still valid
         * and the worker will commit the queued chunk before suspending. */
        (void)request->pause_receiving();
        return true;
    }

    bool resume_receiving_if_active(Requests::Request& request)
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_failed || m_suspended || m_pause_requested)
            return false;
        return request.resume_receiving();
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

    bool set_redirect_target(URL::URL target)
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_failed || m_response_finished || m_redirect_target.has_value())
            return false;
        m_redirect_target = move(target);
        return true;
    }

    Optional<URL::URL> take_redirect_target()
    {
        Threading::MutexLocker locker(m_mutex);
        if (!m_redirect_target.has_value())
            return {};
        auto target = move(m_redirect_target);
        m_redirect_target = {};
        return target;
    }

    bool redirect_pending()
    {
        Threading::MutexLocker locker(m_mutex);
        return m_redirect_target.has_value();
    }

    void response_finished(u64 total_size, Optional<Requests::NetworkError> network_error)
    {
        Threading::MutexLocker locker(m_mutex);
        if (m_failed || m_response_finished || m_suspended)
            return;
        m_response_total_size = total_size;
        m_network_error = move(network_error);
        m_response_finished = true;
        m_changed.broadcast();
    }

    void run(NonnullRefPtr<RinPortalStreamingTransfer> stream,
             ByteString session_url, ByteString event_url,
             ByteString referrer, ByteString filename,
             u64 content_length,
             u64 generation, ByteString validator, u64 resume_offset)
    {
        RinBrowserDownloadPortalSessionV1 session {};
        rin_browser_download_portal_session_init(&session);
        auto url_view = session_url.view();
        auto filename_view = filename.view();
        auto result = resume_offset == 0u
            ? rin_browser_download_portal_session_begin_with_request_id(
                  &session, m_transfer_id,
                  url_view.characters_without_null_termination(), url_view.length(),
                  filename_view.characters_without_null_termination(), filename_view.length(),
                  content_length)
            : rin_browser_download_portal_session_resume(
                  &session, m_transfer_id,
                  url_view.characters_without_null_termination(), url_view.length(),
                  filename_view.characters_without_null_termination(), filename_view.length(),
                  content_length, resume_offset);
        if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
            fail(rinos_download_failure(result), rinos_download_portal_error(result));
            return;
        }

        auto suspend_for_pause = [&]() {
            {
                Threading::MutexLocker locker(m_mutex);
                if (!m_pause_requested)
                    return false;
            }
            if (generation == 0u ||
                content_length == RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH ||
                validator.is_empty() || session.written_length == 0u ||
                session.written_length >= content_length) {
                (void)rin_browser_download_portal_session_abort(&session);
                fail(FileDownloader::DownloadFailure::TransferFailed,
                    "Download cannot be paused without a durable partial range"sv);
                return true;
            }
            result = rin_browser_download_portal_session_suspend(&session);
            if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
                fail(FileDownloader::DownloadFailure::DurabilityFailed,
                    "Paused download staging could not be preserved"sv);
                return true;
            }
            {
                Threading::MutexLocker locker(m_mutex);
                m_suspended = true;
                m_pause_requested = false;
                m_changed.broadcast();
            }
            auto event_loop = m_browser_event_loop->take();
            if (!event_loop.is_alive())
                return true;
            event_loop->deferred_invoke([
                request = m_request, event_reporter = m_event_reporter,
                transfer_id = m_transfer_id, event_url, referrer, filename,
                content_length, generation, validator,
                committed = session.written_length] {
                if (auto strong_request = request.strong_ref())
                    (void)strong_request->stop();
                event_reporter->report(
                    transfer_id, FileDownloader::DownloadEvent::Paused,
                    event_url, referrer, filename, content_length, generation,
                    validator, committed);
            });
            return true;
        };

        for (;;) {
            Optional<ByteBuffer> chunk;
            Optional<Requests::NetworkError> network_error;
            u64 response_total_size = 0;
            bool pause_requested = false;
            {
                Threading::MutexLocker locker(m_mutex);
                m_changed.wait_while([&] {
                    return !m_chunk_ready && !m_response_finished &&
                           !m_pause_requested && !m_failed;
                });
                if (m_failed) {
                    (void)rin_browser_download_portal_session_abort(&session);
                    return;
                }
                if (m_chunk_ready) {
                    chunk = move(m_chunk);
                    m_chunk_ready = false;
                } else if (m_pause_requested) {
                    pause_requested = true;
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
                    if (content_length !=
                            RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                        generation != 0u && !validator.is_empty() &&
                        session.written_length < content_length) {
                        auto event_loop = m_browser_event_loop->take();
                        if (!event_loop.is_alive()) {
                            (void)rin_browser_download_portal_session_abort(&session);
                            return;
                        }
                        event_loop->deferred_invoke([
                            event_reporter = m_event_reporter,
                            transfer_id = m_transfer_id,
                            event_url, referrer, filename, content_length, generation,
                            validator, committed = session.written_length]() mutable {
                            event_reporter->report(
                                transfer_id, FileDownloader::DownloadEvent::Progress,
                                move(event_url), move(referrer), move(filename),
                                content_length, generation, move(validator),
                                committed);
                        });
                    }
                }
                if (suspend_for_pause())
                    return;
                auto event_loop = m_browser_event_loop->take();
                if (!event_loop.is_alive()) {
                    (void)rin_browser_download_portal_session_abort(&session);
                    return;
                }
                event_loop->deferred_invoke([request = m_request, stream] {
                    if (auto strong_request = request.strong_ref())
                        (void)stream->resume_receiving_if_active(*strong_request);
                });
                continue;
            }

            if (pause_requested && suspend_for_pause())
                return;

            if (network_error.has_value()) {
                const bool resumable =
                    generation != 0u &&
                    content_length != RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                    !validator.is_empty() && session.written_length != 0u &&
                    session.written_length < content_length &&
                    response_total_size ==
                        session.written_length - resume_offset;
                if (resumable) {
                    result = rin_browser_download_portal_session_suspend(&session);
                    if (result != RIN_BROWSER_DOWNLOAD_POLICY_OK) {
                        fail(FileDownloader::DownloadFailure::DurabilityFailed,
                            "Interrupted download staging could not be preserved"sv);
                        return;
                    }
                } else {
                    (void)rin_browser_download_portal_session_abort(&session);
                }
                if (rinos_download_tls_failure(*network_error)) {
                    fail(FileDownloader::DownloadFailure::TLSFailed,
                        "Secure connection failed before download"sv);
                } else {
                    fail(FileDownloader::DownloadFailure::NetworkFailed,
                        "Network download failed"sv);
                }
                return;
            }
            const u64 expected_network_size = content_length ==
                    RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH
                ? content_length : content_length - resume_offset;
            if ((content_length ==
                     RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                 (response_total_size == 0u ||
                  response_total_size != session.written_length)) ||
                (content_length !=
                     RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                 (resume_offset >= content_length ||
                  response_total_size != expected_network_size ||
                  session.written_length != content_length))) {
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
                        url = move(event_url), referrer = move(referrer),
                        filename = move(filename),
                        size = receipt.content_length, generation,
                        validator = move(validator)]() mutable {
                        event_reporter->report(
                            transfer_id, FileDownloader::DownloadEvent::Completed,
                            move(url), move(referrer), move(filename), size,
                            generation, move(validator), size);
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
    Optional<URL::URL> m_redirect_target;
    u64 m_response_total_size { 0 };
    bool m_receiving_started { false };
    bool m_chunk_ready { false };
    bool m_response_finished { false };
    bool m_failed { false };
    bool m_can_pause { false };
    bool m_pause_requested { false };
    bool m_suspended { false };
};
#endif

#if defined(AK_OS_RINOS)
void FileDownloader::download_file(URL::URL const& url, ByteString suggested_filename,
                                    DownloadFailureCallback on_failure,
                                    DownloadEventCallback on_event,
                                    u64 requested_transfer_id,
                                    u64 resume_generation,
                                    ByteString resume_validator,
                                    u64 resume_total_bytes,
                                    u64 resume_committed_bytes,
                                    ByteString referrer, u8 redirect_hops,
                                    ByteString resume_stage_url)
#else
void FileDownloader::download_file(URL::URL const& url, LexicalPath destination)
#endif
{
    static u64 next_request_id = 1;

    auto request_id = next_request_id++;
    if (request_id == 0)
        request_id = next_request_id++;
#if defined(AK_OS_RINOS)
    auto generated_transfer_id = requested_transfer_id == 0
        ? next_rinos_download_identity() : 0u;
    auto transfer_id = requested_transfer_id != 0
        ? requested_transfer_id
        : (generated_transfer_id != 0u ? generated_transfer_id : request_id);
    auto transfer_generation = generated_transfer_id != 0u ||
            requested_transfer_id != 0u
        ? transfer_id : 0u;
    RinRuntime::DownloadRangeRequest range_request;
    const bool range_resume = resume_committed_bytes != 0u;
    if (range_resume) {
        if (resume_stage_url.is_empty())
            resume_stage_url = url.serialize().to_byte_string();
        auto parsed_stage_url = URL::Parser::basic_parse(resume_stage_url.view());
        if (!rinos_download_https_url(url) ||
            !parsed_stage_url.has_value() ||
            !rinos_download_https_url(*parsed_stage_url) ||
            !parsed_stage_url->origin().is_same_origin(url.origin())) {
            if (on_failure)
                on_failure(transfer_id, DownloadFailure::ResponseInvalid);
            return;
        }
        range_request.requestId = transfer_id;
        range_request.generation = resume_generation;
        range_request.offset = resume_committed_bytes;
        range_request.totalBytes = resume_total_bytes;
        range_request.validator = resume_validator.characters();
        if (transfer_id == 0u || !range_request.valid()) {
            if (on_failure)
                on_failure(transfer_id, DownloadFailure::ResponseInvalid);
            return;
        }
        transfer_generation = resume_generation;
    } else if (resume_generation != 0u || !resume_validator.is_empty() ||
               resume_total_bytes != 0u || !resume_stage_url.is_empty()) {
        if (on_failure)
            on_failure(transfer_id, DownloadFailure::ResponseInvalid);
        return;
    }
#endif

#if defined(AK_OS_RINOS)
    auto failure_reporter = adopt_ref(*new RinDownloadFailureReporter(move(on_failure)));
    auto event_reporter = adopt_ref(*new RinDownloadEventReporter(move(on_event)));
    if (!rinos_download_https_url(url)) {
        failure_reporter->report(transfer_id, DownloadFailure::UnsafeURL);
        return;
    }
    referrer = rinos_download_referrer_for_url(url, referrer);
#endif

    auto request_headers = HTTP::HeaderList::create();
    request_headers->set({ "User-Agent"sv, Web::default_user_agent });
#if defined(AK_OS_RINOS)
    if (!referrer.is_empty())
        request_headers->set({ "Referer"sv, referrer });
    if (range_resume) {
        std::string range_header;
        if (!range_request.makeRangeHeader(range_header)) {
            failure_reporter->report(
                transfer_id, DownloadFailure::ResponseInvalid);
            return;
        }
        request_headers->set({ "Range"sv,
            ByteString { range_header.data(), range_header.size() } });
        request_headers->set({ "Accept-Encoding"sv, "identity"sv });
        request_headers->set({ "If-Range"sv, resume_validator });
    }
#endif

    auto request = Application::request_server_client().start_request(
        "GET"sv, url, *request_headers, {},
#if defined(AK_OS_RINOS)
        range_resume ? HTTP::CacheMode::NoStore : HTTP::CacheMode::Default,
#else
        HTTP::CacheMode::Default,
#endif
        HTTP::Cookie::IncludeCredentials::Yes);
    if (!request) {
#if defined(AK_OS_RINOS)
        auto failure_url = url.serialize().to_byte_string();
        auto failure_filename = rinos_download_fallback_filename(
            url, move(suggested_filename));
        /* Allocate the Browser row before reporting a start failure.  The
         * failure callback can then attach to this bounded metadata record
         * and the user can retry the same opaque transfer identity. */
        if (!failure_url.is_empty() &&
            failure_url.length() < RIN_BROWSER_DOWNLOAD_MAX_URL_BYTES) {
            event_reporter->report(
                transfer_id, DownloadEvent::Started, move(failure_url),
                referrer, move(failure_filename),
                range_resume ? resume_total_bytes
                             : RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH,
                transfer_generation,
                range_resume ? resume_validator : ByteString {},
                range_resume ? resume_committed_bytes : 0u);
        }
        failure_reporter->report(transfer_id, range_resume
            ? DownloadFailure::NetworkFailed : DownloadFailure::TransferFailed);
#endif
        Application::the().display_error_dialog("Unable to start request to download file"sv);
        return;
    }

#if defined(AK_OS_RINOS)
    auto browser_event_loop = Core::EventLoop::current_weak();
    WeakPtr<Requests::Request> request_weak = request;
    auto stream = adopt_ref(*new RinPortalStreamingTransfer(
        request_weak, move(browser_event_loop), failure_reporter,
        event_reporter, transfer_id));
    m_cancel_callbacks.set(transfer_id, [stream] { stream->cancel(); });
    m_pause_callbacks.set(transfer_id, [stream] { return stream->pause(); });
    auto redirect_suggested_filename = suggested_filename;
    auto redirect_validator = resume_validator;
    auto redirect_stage_url = resume_stage_url;
    request->set_unbuffered_request_callbacks(
        [url, suggested_filename = move(suggested_filename), stream,
            referrer, request_weak, event_reporter, transfer_id, range_resume,
            range_request, redirect_hops, resume_stage_url,
            resume_generation = transfer_generation,
            resume_validator = move(resume_validator),
            resume_total_bytes, resume_committed_bytes](NonnullRefPtr<HTTP::HeaderList> response_headers,
                            Optional<u32> response_code,
                            Optional<String> const&) mutable {
            auto report_started = [&] {
                auto event_url = url.serialize().to_byte_string();
                auto event_filename = rinos_download_fallback_filename(
                    url, suggested_filename);
                if (event_url.is_empty() ||
                    event_url.length() >= RIN_BROWSER_DOWNLOAD_MAX_URL_BYTES)
                    return;
                event_reporter->report(
                    transfer_id, DownloadEvent::Started, move(event_url),
                    referrer, move(event_filename),
                    range_resume ? resume_total_bytes
                                 : RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH,
                    resume_generation,
                    range_resume ? resume_validator : ByteString {},
                    range_resume ? resume_committed_bytes : 0u);
            };
            auto reject_response = [&](DownloadFailure failure,
                                       StringView message) {
                report_started();
                stream->fail(failure, message);
            };

            if (response_code.has_value() &&
                rinos_download_redirect_status(*response_code)) {
                auto target = rinos_download_redirect_target(
                    url, *response_headers, range_resume, redirect_hops);
                if (target.has_value() &&
                    stream->set_redirect_target(target.release_value())) {
                    Core::deferred_invoke([request_weak] {
                        if (auto strong_request = request_weak.strong_ref())
                            (void)strong_request->stop();
                    });
                    return;
                }
                reject_response(DownloadFailure::ResponseInvalid,
                    "Download redirect was rejected by the bounded redirect policy"sv);
                return;
            }
            if (response_code.has_value() && *response_code >= 400) {
                reject_response(range_resume ? DownloadFailure::ResponseInvalid
                                             : DownloadFailure::HttpFailed,
                    range_resume
                        ? "Download server rejected the requested range"sv
                        : "Download server returned an error response"sv);
                return;
            }
            if (!response_code.has_value() ||
                (range_resume ? *response_code != 206u
                              : (*response_code < 200 || *response_code >= 300))) {
                reject_response(DownloadFailure::ResponseInvalid,
                    range_resume
                        ? "Download resume did not receive a valid partial response"sv
                        : "Download did not receive a successful HTTP response"sv);
                return;
            }

            auto content_length = response_headers->extract_length();
            u64 declared_length = RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH;
            if (content_length.has<u64>()) {
                declared_length = content_length.get<u64>();
                if (declared_length == 0u ||
                    declared_length > RIN_BROWSER_DOWNLOAD_MAX_BYTES) {
                    reject_response(DownloadFailure::ResponseInvalid,
                        "Download response Content-Length is not supported"sv);
                    return;
                }
            } else if (!content_length.has<Empty>() ||
                       response_headers->get("Content-Length"sv).has_value()) {
                reject_response(DownloadFailure::ResponseInvalid,
                    "Download response has a malformed Content-Length"sv);
                return;
            }

            if (range_resume &&
                (!content_length.has<u64>() ||
                 content_length.get<u64>() !=
                     resume_total_bytes - resume_committed_bytes)) {
                reject_response(DownloadFailure::ResponseInvalid,
                    "Download range length did not match the saved offset"sv);
                return;
            }

            if (range_resume) {
                auto encoding = response_headers->get("Content-Encoding"sv);
                if (encoding.has_value() &&
                    !encoding->equals_ignoring_ascii_case("identity"sv)) {
                    reject_response(DownloadFailure::ResponseInvalid,
                        "Compressed download ranges are not supported"sv);
                    return;
                }
            }

            ByteString validator;
            if (auto etag = response_headers->get("ETag"sv);
                etag.has_value() && rinos_download_strong_etag(*etag)) {
                validator = ByteString { etag->characters(), etag->length() };
            }
            if (range_resume && validator != resume_validator) {
                reject_response(DownloadFailure::ResponseInvalid,
                    "Download validator changed during resume"sv);
                return;
            }

            if (range_resume) {
                auto content_range = response_headers->get("Content-Range"sv);
                const std::string content_range_value = content_range.has_value()
                    ? std::string(content_range->characters(), content_range->length())
                    : std::string();
                const std::string content_length_value =
                    std::to_string(content_length.get<u64>());
                RinRuntime::DownloadRangeResponse response;
                if (!RinRuntime::makeDownloadRangeResponse(
                        range_request, static_cast<std::uint16_t>(*response_code),
                        content_range_value, content_length_value,
                        resume_generation,
                        std::string(validator.characters(), validator.length()),
                        response)) {
                    reject_response(DownloadFailure::ResponseInvalid,
                        "Download Content-Range did not match the saved receipt"sv);
                    return;
                }
            }

            char server_filename[RIN_BROWSER_DOWNLOAD_MAX_FILENAME_BYTES + 1u];
            size_t server_filename_size = 0u;
            if (auto content_disposition = response_headers->get("Content-Disposition"sv); content_disposition.has_value()
                && rin_browser_download_filename_from_content_disposition(
                    content_disposition->characters(), content_disposition->length(),
                    server_filename, sizeof(server_filename), &server_filename_size)) {
                suggested_filename = ByteString { server_filename, server_filename_size };
            }
            suggested_filename = rinos_download_fallback_filename(
                url, move(suggested_filename));
            const u64 full_length = range_resume ? resume_total_bytes
                                                  : declared_length;
            const bool can_pause = resume_generation != 0u &&
                full_length != RIN_BROWSER_DOWNLOAD_UNKNOWN_CONTENT_LENGTH &&
                full_length <= RIN_BROWSER_DOWNLOAD_MAX_BYTES &&
                !validator.is_empty();
            if (!stream->begin_receiving(can_pause)) {
                reject_response(DownloadFailure::TransferFailed,
                    "Secure download transfer could not start"sv);
                return;
            }

            event_reporter->report(
                transfer_id, FileDownloader::DownloadEvent::Started,
                url.serialize().to_byte_string(), referrer, suggested_filename,
                range_resume ? resume_total_bytes : declared_length,
                resume_generation, validator,
                range_resume ? resume_committed_bytes : 0u);

            auto worker = Threading::Thread::try_create("rin-download"sv,
                [stream,
                    session_url = range_resume
                        ? resume_stage_url : url.serialize().to_byte_string(),
                    event_url = url.serialize().to_byte_string(),
                    serialized_referrer = referrer,
                    filename = move(suggested_filename),
                    length = range_resume ? resume_total_bytes : declared_length,
                    generation = resume_generation, validator = move(validator),
                    offset = range_resume ? resume_committed_bytes : 0u]() mutable -> intptr_t {
                    stream->run(stream, move(session_url), move(event_url),
                                move(serialized_referrer), move(filename),
                                length, generation, move(validator), offset);
                    return 0;
                });
            if (worker.is_error()) {
                stream->fail(range_resume ? DownloadFailure::NetworkFailed
                                          : DownloadFailure::TransferFailed,
                    "Unable to start secure download transfer"sv);
                return;
            }
            auto transfer_thread = worker.release_value();
            transfer_thread->start();
            transfer_thread->detach();
        },
        [stream, request_weak](ReadonlyBytes bytes) {
            if (stream->redirect_pending())
                return;
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
        [this, request_id, transfer_id, stream, url, referrer,
            redirect_suggested_filename = move(redirect_suggested_filename),
            redirect_validator = move(redirect_validator),
            redirect_stage_url = move(redirect_stage_url), range_resume,
            transfer_generation, resume_total_bytes, resume_committed_bytes,
            redirect_hops, failure_reporter, event_reporter](
            u64 total_size, Requests::RequestTimingInfo const&,
            Optional<Requests::NetworkError> network_error) mutable {
            auto redirect_target = stream->take_redirect_target();
            if (redirect_target.has_value()) {
                auto target = redirect_target.release_value();
                auto redirected_referrer =
                    url.origin().is_same_origin(target.origin())
                    ? referrer : ByteString {};
                Core::deferred_invoke([this, request_id, transfer_id,
                    target = move(target),
                    redirected_referrer = move(redirected_referrer),
                    filename = move(redirect_suggested_filename),
                    validator = move(redirect_validator),
                    stage_url = move(redirect_stage_url), range_resume,
                    transfer_generation, resume_total_bytes,
                    resume_committed_bytes, redirect_hops,
                    failure_reporter, event_reporter]() mutable {
                    m_requests.remove(request_id);
                    m_cancel_callbacks.remove(transfer_id);
                    m_pause_callbacks.remove(transfer_id);

                    DownloadFailureCallback next_failure =
                        [failure_reporter](u64 id, DownloadFailure failure) {
                            failure_reporter->report(id, failure);
                        };
                    DownloadEventCallback next_event =
                        [event_reporter](u64 id, DownloadEvent event,
                            ByteString event_url, ByteString event_referrer,
                            ByteString event_filename, u64 size,
                            u64 generation, ByteString event_validator,
                            u64 committed_bytes) {
                            event_reporter->report(id, event,
                                move(event_url), move(event_referrer),
                                move(event_filename), size, generation,
                                move(event_validator), committed_bytes);
                        };
                    this->download_file(target, move(filename),
                        move(next_failure), move(next_event), transfer_id,
                        range_resume ? transfer_generation : 0u,
                        move(validator),
                        range_resume ? resume_total_bytes : 0u,
                        range_resume ? resume_committed_bytes : 0u,
                        move(redirected_referrer),
                        static_cast<u8>(redirect_hops + 1u),
                        move(stage_url));
                });
                return;
            }
            Core::deferred_invoke([this, request_id, transfer_id] {
                m_requests.remove(request_id);
                m_cancel_callbacks.remove(transfer_id);
                m_pause_callbacks.remove(transfer_id);
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

bool FileDownloader::pause_download(u64 transfer_id)
{
    if (transfer_id == 0u)
        return false;
    auto callback = m_pause_callbacks.get(transfer_id);
    if (!callback.has_value())
        return false;
    return (*callback)();
}

}
