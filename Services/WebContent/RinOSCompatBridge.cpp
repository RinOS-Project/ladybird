#include <AK/Array.h>
#include <AK/ByteString.h>
#include <AK/HashMap.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <AK/LexicalPath.h>
#include <AK/Memory.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Optional.h>
#include <AK/StdLibExtras.h>
#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <AK/Time.h>
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Notifier.h>
#include <LibCore/Promise.h>
#include <LibCore/Timer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/SystemTheme.h>
#include <LibIPC/File.h>
#include <LibMain/Main.h>
#include <LibRequests/NetworkError.h>
#include <LibURL/Parser.h>
#include <LibWeb/Page/InputEvent.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/HTML/HTMLElement.h>
#include <LibWeb/HTML/HTMLInputElement.h>
#include <LibWeb/HTML/HTMLSelectElement.h>
#include <LibWeb/HTML/HTMLTextAreaElement.h>
#include <LibWeb/HTML/SelectedFile.h>
#include <LibWeb/UIEvents/KeyCode.h>
#include <LibWeb/UIEvents/MouseButton.h>
#include <LibWebView/Application.h>
#include <LibWebView/FileDownloader.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/Utilities.h>

#include "webcontent_service_abi.h"
#include "webcontent_bridge_recovery_policy.h"
#include "webcontent_service_policy.h"
#include "rin_socket_abi.h"
#include "webcontent_peer_identity_policy.h"
#include "webcontent_network_failure_policy.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

extern "C" {
int webcontent_run(void);
int rin_service_should_stop(void);
int rin_shm_get(char const* name, u32 size, u32 flags);
void* rin_shm_at(int handle, void* addr_hint, u32 prot);
int rin_shm_dt(int handle, void* addr);
unsigned long rin_time(void);
void rin_log(char const* msg);
}

class BridgeApplication;
struct PageSession;
static PageSession* find_page(u32 page_id);

static constexpr u64 s_load_start_retry_interval_ms = 250;
static constexpr u64 s_load_start_retry_budget_ms = 10000;
static constexpr u32 s_load_start_retry_limit = 40;

static u64 monotonic_time_ms()
{
    return static_cast<u64>(MonotonicTime::now_coarse().milliseconds());
}

static bool is_browser_builtin_url(StringView url)
{
    return url == "about:start"sv || url == "about:settings"sv;
}

static bool is_internal_markup_document_url(StringView url)
{
    return url.is_empty() || url == "about:blank"sv || url == "about:srcdoc"sv;
}

enum class PendingLoadKind : u8 {
    None = 0,
    Navigate,
    Markup,
};

static StringView pending_load_kind_name(PendingLoadKind kind)
{
    switch (kind) {
    case PendingLoadKind::Navigate:
        return "navigate"sv;
    case PendingLoadKind::Markup:
        return "load_markup"sv;
    case PendingLoadKind::None:
    default:
        return "none"sv;
    }
}

static constexpr StringView s_metrics_script = R"JS((() => {
    try {
        const root = document.scrollingElement || document.documentElement || document.body;
        const viewportWidth = Math.round(window.innerWidth || 0);
        const viewportHeight = Math.round(window.innerHeight || 0);
        const contentWidth = Math.round((root && root.scrollWidth) || viewportWidth);
        const contentHeight = Math.round((root && root.scrollHeight) || viewportHeight);
        const active = document.activeElement;
        const tag = active && String(active.tagName || "").toLowerCase();
        const inputType = tag === "input" ? String(active.type || "text").toLowerCase() : "";
        const textInputTypes = new Set(["text", "search", "url", "email", "tel", "password", "number"]);
        const textInputEnabled = !!active && !active.disabled && !active.readOnly && (
            (tag === "input" && textInputTypes.has(inputType)) ||
            tag === "textarea" || active.isContentEditable);
        let textInputContentType = 0;
        if (inputType === "password") textInputContentType = 1;
        else if (inputType === "url") textInputContentType = 2;
        else if (inputType === "number") textInputContentType = 3;
        let inputRect = { x: 0, y: 0, width: 0, height: 0 };
        if (textInputEnabled) {
            const elementRect = active.getBoundingClientRect();
            inputRect = elementRect;
            if (active.isContentEditable) {
                const selection = window.getSelection();
                if (selection && selection.rangeCount > 0) {
                    const caretRect = selection.getRangeAt(0).getBoundingClientRect();
                    if (caretRect && (caretRect.width || caretRect.height)) inputRect = caretRect;
                }
            }
        }
        return {
            scrollX: Math.round(window.scrollX || 0),
            scrollY: Math.round(window.scrollY || 0),
            viewportWidth,
            viewportHeight,
            contentWidth,
            contentHeight,
            maxScrollX: Math.max(0, contentWidth - viewportWidth),
            maxScrollY: Math.max(0, contentHeight - viewportHeight),
            title: String(document.title || ""),
            url: String(location.href || "about:blank"),
            textInputEnabled,
            textInputContentType,
            textInputX: Math.round(inputRect.x || 0),
            textInputY: Math.round(inputRect.y || 0),
            textInputWidth: Math.max(1, Math.round(inputRect.width || 1)),
            textInputHeight: Math.max(1, Math.round(inputRect.height || 1))
        };
    } catch (error) {
        return { error: String((error && error.message) || error || "metrics failed") };
    }
})())JS"sv;

static void copy_c_string(char* dst, size_t dst_size, StringView src)
{
    if (!dst || dst_size == 0)
        return;

    auto bytes = src.bytes();
    size_t copy_length = min(dst_size - 1, bytes.size());
    if (copy_length > 0)
        __builtin_memcpy(dst, bytes.data(), copy_length);
    dst[copy_length] = '\0';
}

static void copy_c_string(char* dst, size_t dst_size, char const* src)
{
    if (!src) {
        copy_c_string(dst, dst_size, StringView {});
        return;
    }
    copy_c_string(dst, dst_size, StringView { src, __builtin_strlen(src) });
}

static ErrorOr<Core::AnonymousBuffer> build_embedded_fallback_theme()
{
    auto buffer = TRY(Core::AnonymousBuffer::create_with_size(sizeof(Gfx::SystemTheme)));
    Gfx::populate_system_theme_with_default_values(*buffer.data<Gfx::SystemTheme>());
    return buffer;
}

static bool socket_should_retry()
{
    return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
}

static bool socket_set_nonblocking(int fd)
{
    auto flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    if ((flags & O_NONBLOCK) != 0)
        return true;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static bool client_rpc_deadline_create(u64* deadline_ms)
{
    auto now = monotonic_time_ms();
    if (deadline_ms == nullptr ||
        now > UINT64_MAX - static_cast<u64>(RIN_WEBCONTENT_RPC_TIMEOUT_MS)) {
        errno = ETIMEDOUT;
        return false;
    }
    *deadline_ms = now + static_cast<u64>(RIN_WEBCONTENT_RPC_TIMEOUT_MS);
    return true;
}

static bool socket_wait_for_event(int fd, short events, u64 deadline_ms)
{
    for (;;) {
        auto now = monotonic_time_ms();
        if (now >= deadline_ms) {
            errno = ETIMEDOUT;
            return false;
        }

        pollfd descriptor {};
        auto remaining_ms = deadline_ms - now;
        descriptor.fd = fd;
        descriptor.events = events;
        auto timeout_ms = remaining_ms > static_cast<u64>(INT_MAX)
            ? INT_MAX
            : static_cast<int>(remaining_ms);
        auto rc = ::poll(&descriptor, 1, timeout_ms);
        if (rc > 0) {
            if ((descriptor.revents & events) != 0)
                return true;
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                errno = ECONNRESET;
                return false;
            }
            errno = EPROTO;
            return false;
        }
        if (rc == 0) {
            errno = ETIMEDOUT;
            return false;
        }
        if (errno != EINTR)
            return false;
    }
}

static bool send_all(int fd, void const* data, size_t len, u64 deadline_ms)
{
    auto const* bytes = reinterpret_cast<u8 const*>(data);
    size_t offset = 0;
    while (offset < len) {
        if (!socket_wait_for_event(fd, POLLOUT, deadline_ms))
            return false;
        auto rc = ::send(fd, bytes + offset, len - offset, MSG_NOSIGNAL);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        if (rc == 0) {
            errno = EPIPE;
            return false;
        }
        offset += static_cast<size_t>(rc);
    }
    return true;
}

static bool recv_all(int fd, void* data, size_t len, u64 deadline_ms)
{
    auto* bytes = reinterpret_cast<u8*>(data);
    size_t offset = 0;
    while (offset < len) {
        if (!socket_wait_for_event(fd, POLLIN, deadline_ms))
            return false;
        auto rc = ::recv(fd, bytes + offset, len - offset, 0);
        if (rc < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        if (rc == 0) {
            errno = ECONNRESET;
            return false;
        }
        offset += static_cast<size_t>(rc);
    }
    return true;
}

static bool send_message(int fd, u32 command, i32 status, u32 page_id, void const* payload, u32 payload_len, u64 deadline_ms)
{
    RinWebContentMsgHeader header {};
    header.magic = RIN_WEBCONTENT_MAGIC;
    header.version = RIN_WEBCONTENT_VERSION;
    header.command = command;
    header.status = status;
    header.page_id = page_id;
    header.payload_len = payload_len;

    return send_all(fd, &header, sizeof(header), deadline_ms)
        && (payload_len == 0 || send_all(fd, payload, payload_len, deadline_ms));
}

class BridgeApplication final : public WebView::Application {
    WEB_VIEW_APPLICATION(BridgeApplication)

public:
    explicit BridgeApplication(Optional<ByteString> binary_path = {})
        : WebView::Application(move(binary_path))
    {
    }

    virtual void create_platform_options(WebView::BrowserOptions& browser_options,
        WebView::RequestServerOptions& request_server_options,
        WebView::WebContentOptions& web_content_options) override
    {
        RinWebContentBridgeLaunchPolicyV1 policy {};
        VERIFY(rin_webcontent_bridge_launch_policy_v1(&policy));
        browser_options.headless_mode = WebView::HeadlessMode::Manual;
        browser_options.skip_implicit_headless_bootstrap_view = policy.skip_implicit_headless_bootstrap_view
            ? WebView::SkipImplicitHeadlessBootstrapView::Yes
            : WebView::SkipImplicitHeadlessBootstrapView::No;
        browser_options.disable_sql_database = policy.disable_sql_database
            ? WebView::DisableSQLDatabase::Yes
            : WebView::DisableSQLDatabase::No;
        browser_options.disable_spare_web_content_processes = policy.disable_spare_webcontent_processes
            ? WebView::DisableSpareWebContentProcesses::Yes
            : WebView::DisableSpareWebContentProcesses::No;
        browser_options.allow_popups = policy.allow_popups
            ? WebView::AllowPopups::Yes
            : WebView::AllowPopups::No;

        // RinOS bridge-mode startup is currently more reliable without RequestServer disk cache setup.
        request_server_options.http_disk_cache_mode = policy.disable_http_disk_cache
            ? WebView::HTTPDiskCacheMode::Disabled
            : WebView::HTTPDiskCacheMode::Enabled;
        if (request_server_options.certificates.is_empty())
            request_server_options.certificates.append("/System/Trust/roots.rinca"sv);

        web_content_options.force_cpu_painting = policy.force_cpu_painting
            ? WebView::ForceCPUPainting::Yes
            : WebView::ForceCPUPainting::No;
        web_content_options.force_fontconfig = policy.force_fontconfig
            ? WebView::ForceFontconfig::Yes
            : WebView::ForceFontconfig::No;
        web_content_options.paint_viewport_scrollbars = policy.disable_viewport_scrollbars
            ? WebView::PaintViewportScrollbars::No
            : WebView::PaintViewportScrollbars::Yes;
        // The bridge is a browser-process owner and each actual WebContent
        // helper is launched through the native SYS_SPAWN_PROCESS path. Keep
        // Ladybird's default cross-site swap enabled so a renderer compromise
        // does not retain the prior site's process.
        web_content_options.disable_site_isolation = policy.enable_site_isolation
            ? WebView::DisableSiteIsolation::No
            : WebView::DisableSiteIsolation::Yes;

        static bool did_log_bridge_options = false;
        if (!did_log_bridge_options) {
            did_log_bridge_options = true;
            rin_log("[webcontent] RinOS bridge mode: spare WebContent disabled, implicit headless bootstrap skipped\n");
        }
    }
};

class BridgeView final : public WebView::HeadlessWebView {
public:
    static NonnullOwnPtr<BridgeView> create(Core::AnonymousBuffer theme, Web::DevicePixelSize size)
    {
        auto view = adopt_own(*new BridgeView(move(theme), size));
        view->initialize_client(CreateNewClient::Yes);
        return view;
    }

    explicit BridgeView(Core::AnonymousBuffer theme, Web::DevicePixelSize size)
        : WebView::HeadlessWebView(move(theme), size)
    {
        /* RinOS WebContent is a renderer, not a filesystem authority.  The
         * generic Ladybird ViewImplementation fallback opens the path passed
         * by WebContent directly; keep that host-only behavior out of the
         * RinOS bridge and report a capability denial instead.  Browser
         * process upload wiring must provide a File Portal descriptor before
         * this callback can be relaxed. */
        on_request_file = [this](ByteString const&, i32 request_id) {
            client().async_handle_file_return(page_id(), EPERM, {}, request_id);
        };

        /* The Browser owns the chooser and returns one authenticated
         * descriptor through the page-state/completion bridge. */
        on_request_file_picker = [this](auto const& accepted_file_types,
                                        auto allow_multiple) {
            if (on_file_picker_request_bridge)
                on_file_picker_request_bridge(accepted_file_types,
                                              allow_multiple);
        };
    }

    RefPtr<Gfx::Bitmap const> visible_bitmap() const
    {
        if (m_client_state.has_usable_bitmap)
            return m_client_state.front_bitmap.bitmap;
        return m_backup_bitmap;
    }

    Gfx::IntSize visible_bitmap_size() const
    {
        if (m_client_state.has_usable_bitmap && m_client_state.front_bitmap.bitmap)
            return m_client_state.front_bitmap.bitmap->size();
        if (m_backup_bitmap)
            return m_backup_bitmap->size();
        return {};
    }

    bool has_allocated_backing_stores() const
    {
        return m_client_state.front_bitmap.bitmap && m_client_state.back_bitmap.bitmap;
    }

    void initialize_bridge_client()
    {
        initialize_client(CreateNewClient::Yes);
    }

    ErrorOr<JsonValue> evaluate_json(StringView source, int timeout_ms = 250);
    ErrorOr<JsonObject> inspect_accessibility_json(int timeout_ms = 250);

    Function<void(Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles)>
        on_file_picker_request_bridge;
};

ErrorOr<JsonValue> BridgeView::evaluate_json(StringView source, int timeout_ms)
{
    auto promise = Core::Promise<JsonValue>::construct();
    auto weak_promise = promise->template make_weak_ptr<Core::Promise<JsonValue>>();
    auto previous_callback = move(on_received_js_console_result);

    auto timeout = Core::Timer::create_single_shot(timeout_ms, [weak_promise] {
        if (!weak_promise || weak_promise->is_resolved() || weak_promise->is_rejected())
            return;
        weak_promise->reject(Error::from_string_literal("Timed out waiting for JS result"));
    });

    on_received_js_console_result = [promise](JsonValue value) mutable {
        promise->resolve(move(value));
    };

    js_console_input(TRY(String::from_utf8(source)));
    timeout->start();
    auto result = promise->await();
    timeout->stop();
    on_received_js_console_result = move(previous_callback);

    if (result.is_error())
        return result.release_error();
    return result.release_value();
}

ErrorOr<JsonObject> BridgeView::inspect_accessibility_json(int timeout_ms)
{
    auto promise = Core::Promise<JsonObject>::construct();
    auto weak_promise = promise->template make_weak_ptr<Core::Promise<JsonObject>>();
    auto previous_callback = move(on_received_accessibility_tree);

    auto timeout = Core::Timer::create_single_shot(timeout_ms, [weak_promise] {
        if (!weak_promise || weak_promise->is_resolved() || weak_promise->is_rejected())
            return;
        weak_promise->reject(Error::from_string_literal("Timed out waiting for accessibility tree"));
    });

    on_received_accessibility_tree = [promise](JsonObject value) mutable {
        promise->resolve(move(value));
    };
    WebView::ViewImplementation::inspect_accessibility_tree();
    timeout->start();
    auto result = promise->await();
    timeout->stop();
    on_received_accessibility_tree = move(previous_callback);

    if (result.is_error())
        return result.release_error();
    return result.release_value();
}

struct PageSession {
    struct DownloadEventRecord {
        u32 event { RIN_WEBCONTENT_DOWNLOAD_EVENT_NONE };
        u32 revision { 0 };
        u64 transfer_id { 0 };
        u64 timestamp_ms { 0 };
        u64 size_bytes { 0 };
        u32 failure_status { RIN_WEBCONTENT_DOWNLOAD_STATUS_NONE };
        ByteString url;
        ByteString filename;
    };

    explicit PageSession(u32 id, Core::AnonymousBuffer theme, int width, int height)
        : page_id(id)
        , requested_viewport_width(max(width, 1))
        , requested_viewport_height(max(height, 1))
        , reported_viewport_width(requested_viewport_width)
        , reported_viewport_height(requested_viewport_height)
        , view(adopt_own(*new BridgeView(move(theme), { requested_viewport_width, requested_viewport_height })))
    {
        committed_url = ByteString { "about:blank" };
        title = ByteString { "New Tab" };

        view->on_load_start = [this](URL::URL const& url, bool) {
            auto serialized = remap_markup_internal_url(url.serialize().to_byte_string());
            loading = true;
            progress_percent = 15;
            pending_url = serialized;
            crashed = false;
            crash_reason = {};
            download_status = RIN_WEBCONTENT_DOWNLOAD_STATUS_NONE;
            download_status_revision = 0;
            download_status_timestamp_ms = 0;
            waiting_for_first_paint_after_load_finish = false;
            metrics_dirty = true;
            // P5-2: load 開始時点で DNS→HTTP 段階に入ると仮定。
            load_phase_current = RIN_WEBCONTENT_LOAD_PHASE_HTTP;
            load_phase_started_ms = monotonic_time_ms();
            load_current_url_str = serialized;
            load_resources_waiting = 0;
            load_resources_total = 0;
            load_suspected_stall = false;
            note_pending_load_started();
            kick_first_frame_if_needed("load-start"sv, true);
            auto message = ByteString::formatted("[webcontent] page {} load start {}\n", page_id, serialized);
            rin_log(message.characters());
            mark_dirty();
        };

        view->on_load_finish = [this](URL::URL const& url) {
            auto serialized = remap_markup_internal_url(url.serialize().to_byte_string());
            // A cold WebContent process finishes its implicit about:blank
            // document while the accepted navigation is still waiting for the
            // rendering transport. Do not publish that bootstrap completion as
            // completion of the requested URL.
            if (is_waiting_for_load_start()) {
                auto message = ByteString::formatted(
                    "[webcontent] page {} ignored pre-navigation load finish {}\n",
                    page_id,
                    serialized);
                rin_log(message.characters());
                metrics_dirty = true;
                mark_dirty();
                return;
            }
            active_navigation_request_id = 0;
            RinWebContentBridgeLoadFinishPlanV1 plan {};
            VERIFY(rin_webcontent_bridge_load_finish_plan_v1(
                has_first_paint_for_active_navigation() ? 1 : 0, &plan));
            committed_url = serialized;
            metrics_dirty = true;
            refresh_metrics(true);
            loading = plan.loading != 0;
            if (progress_percent < plan.progress_floor)
                progress_percent = plan.progress_floor;
            if (plan.clear_pending_url)
                pending_url = {};
            waiting_for_first_paint_after_load_finish = plan.waiting_for_first_paint != 0;
            if (plan.load_phase == RIN_WEBCONTENT_BRIDGE_LOAD_PHASE_COMPLETE) {
                load_phase_current = RIN_WEBCONTENT_LOAD_PHASE_COMPLETE;
                load_phase_started_ms = monotonic_time_ms();
                load_suspected_stall = false;
            } else {
                pending_url = serialized;
                load_phase_current = RIN_WEBCONTENT_LOAD_PHASE_PAINT;
                load_phase_started_ms = monotonic_time_ms();
            }
            if (plan.kick_first_frame) {
                kick_first_frame_if_needed("load-finish-before-first-paint"sv, false);
            }
            auto message = ByteString::formatted("[webcontent] page {} load finish {}\n", page_id, serialized);
            rin_log(message.characters());
            mark_dirty();
        };

        view->on_network_request_started = [this](u64 request_id, URL::URL const& url, ByteString const&, Vector<HTTP::Header> const&, ByteBuffer, Optional<String> initiator_type) {
            auto serialized = remap_markup_internal_url(url.serialize().to_byte_string());
            bool targets_pending_document = serialized == pending_url && !initiator_type.has_value();
            if (!rin_webcontent_network_request_tracks_navigation(
                    request_id, active_navigation_request_id, loading ? 1 : 0,
                    targets_pending_document ? 1 : 0)) {
                return;
            }
            active_navigation_request_id = request_id;
        };

        view->on_network_request_finished = [this](u64 request_id, u64, Requests::RequestTimingInfo const&, Optional<Requests::NetworkError> const& network_error) {
            if (!rin_webcontent_network_failure_ends_navigation(
                    request_id, active_navigation_request_id, loading ? 1 : 0,
                    network_error.has_value() ? 1 : 0)) {
                return;
            }

            auto reason = ByteString::formatted(
                "Network request failed: {}",
                Requests::network_error_to_string(*network_error));
            active_navigation_request_id = 0;
            fail_pending_load_request(reason.view());
        };

        view->on_url_change = [this](URL::URL const& url) {
            auto serialized = remap_markup_internal_url(url.serialize().to_byte_string());
            if (loading)
                pending_url = move(serialized);
            else
                committed_url = move(serialized);
            mark_dirty();
        };

        view->on_title_change = [this](Utf16String const& new_title) {
            auto utf8 = new_title.to_utf8();
            title = utf8.is_empty() ? committed_url : utf8.to_byte_string();
            mark_dirty();
        };

        view->on_request_download = [this](URL::URL const& url, ByteString suggested_filename) {
            start_download(url, move(suggested_filename));
        };

        /* Worker/file:// resource requests use the same authenticated Open
         * picker as an HTML file input. The requested pathname is deliberately
         * ignored; only the opaque request id is retained until Browser sends
         * back a File Portal descriptor. */
        view->on_request_file = [this](ByteString const&, i32 request_id) {
            request_file(request_id);
        };

        view->on_file_picker_request_bridge =
            [this](Web::HTML::FileFilter const&, Web::HTML::AllowMultipleFiles allow_multiple) {
                request_file_picker(allow_multiple);
            };

        view->on_resource_status_change = [this](i32 count_waiting) {
            if (!loading)
                return;
            progress_percent = count_waiting > 0 ? 60 : 90;
            // P5-2: resource 待機中 = SCRIPT/サブリソース取得フェーズ。
            // 0 まで落ちたら Parse/Paint 近傍なので PARSE とみなす。
            if (count_waiting > 0) {
                if (load_phase_current != RIN_WEBCONTENT_LOAD_PHASE_SCRIPT) {
                    load_phase_current = RIN_WEBCONTENT_LOAD_PHASE_SCRIPT;
                    load_phase_started_ms = monotonic_time_ms();
                }
            } else {
                if (load_phase_current != RIN_WEBCONTENT_LOAD_PHASE_PARSE
                    && load_phase_current != RIN_WEBCONTENT_LOAD_PHASE_PAINT) {
                    load_phase_current = RIN_WEBCONTENT_LOAD_PHASE_PARSE;
                    load_phase_started_ms = monotonic_time_ms();
                }
            }
            load_resources_waiting = count_waiting;
            if (count_waiting > load_resources_total)
                load_resources_total = count_waiting;
            // 10 秒以上同じフェーズに居たら stall 疑い
            if (load_phase_started_ms != 0 && monotonic_time_ms() - load_phase_started_ms > 10000)
                load_suspected_stall = true;
            mark_dirty();
        };

        view->on_ready_to_paint = [this] {
            // The first ready_to_paint from a cold process belongs to its
            // implicit about:blank document. Backing-store readiness will make
            // maybe_replay_pending_load_request() resend the accepted target;
            // keep the target's paint revision at zero until then.
            if (is_waiting_for_load_start()) {
                if (!logged_pre_navigation_paint) {
                    auto message = ByteString::formatted(
                        "[webcontent] page {} ignored pre-navigation ready_to_paint\n",
                        page_id);
                    rin_log(message.characters());
                    logged_pre_navigation_paint = true;
                }
                metrics_dirty = true;
                mark_dirty();
                return;
            }
            bool first_paint_for_active_navigation = !has_first_paint_for_active_navigation();
            if (paint_revision == 0) {
                auto message = ByteString::formatted("[webcontent] page {} first ready_to_paint\n", page_id);
                rin_log(message.characters());
            }
            if (loading && progress_percent < 75)
                progress_percent = 75;
            metrics_dirty = true;
            ++paint_revision;
            last_paint_revision_seen = paint_revision;
            RinWebContentBridgeFirstPaintPlanV1 plan {};
            VERIFY(rin_webcontent_bridge_first_paint_plan_v1(
                first_paint_for_active_navigation ? 1 : 0,
                waiting_for_first_paint_after_load_finish ? 1 : 0,
                &plan));
            if (plan.complete_load) {
                loading = false;
                progress_percent = plan.progress_value;
                if (plan.clear_pending_url)
                    pending_url = {};
                waiting_for_first_paint_after_load_finish = false;
            }
            clear_first_frame_wait();
            logged_missing_visible_bitmap = false;
            mark_dirty();
        };

        view->on_web_content_crashed = [this] {
            bool crashed_while_waiting_for_first_frame = first_frame_pending;
            loading = false;
            crashed = true;
            progress_percent = 0;
            crash_reason = ByteString { "WebContent crashed" };
            RinWebContentBridgeCrashPlanV1 plan {};
            VERIFY(rin_webcontent_bridge_crash_plan_v1(
                pending_url.is_empty() ? 1 : 0,
                committed_url.is_empty() ? 1 : 0,
                &plan));
            if (plan.copy_committed_to_pending)
                pending_url = committed_url;
            if (plan.clear_first_paint_wait)
                waiting_for_first_paint_after_load_finish = false;
            active_navigation_request_id = 0;
            clear_file_picker_request(true);
            clear_pending_load_request();
            clear_first_frame_wait();
            auto message = ByteString::formatted(
                "[webcontent] page {} crashed first_frame_pending={} paint_revision={} url={}\n",
                page_id,
                crashed_while_waiting_for_first_frame ? 1 : 0,
                paint_revision,
                pending_url.is_empty() ? committed_url : pending_url);
            rin_log(message.characters());
            mark_dirty();
        };

        view->on_web_content_process_change_for_cross_site_navigation = [this] {
            metrics_dirty = true;
            kick_first_frame_if_needed("process-swap"sv, true);
            mark_dirty();
        };

        view->initialize_bridge_client();
        kick_first_frame_if_needed("create-page"sv, true);
    }

    ~PageSession()
    {
        close_paint_shm();
    }

    void mark_dirty()
    {
        dirty = true;
        ++state_revision;
    }

    void start_download(URL::URL const& url, ByteString suggested_filename,
                        u64 transfer_id = 0u)
    {
        // The BridgeApplication is the browser-process side of the isolated
        // WebContent pair. FileDownloader owns authenticated request
        // transport and the File Manager destination; the renderer supplied
        // neither file access nor a handle it can retain.
        auto page_id_value = page_id;
        WebView::Application::the().file_downloader().download_file(
            url, move(suggested_filename), [page_id_value](u64 id,
                WebView::FileDownloader::DownloadFailure failure) {
                if (auto* page = find_page(page_id_value))
                    page->note_download_failure(id, failure);
            }, [page_id_value](u64 id, WebView::FileDownloader::DownloadEvent event,
                               ByteString event_url, ByteString filename,
                               u64 size) {
                if (auto* page = find_page(page_id_value))
                    page->note_download_event(id, event, move(event_url),
                                              move(filename), size);
            }, transfer_id);
    }

    void request_file_picker(Web::HTML::AllowMultipleFiles allow_multiple)
    {
        /* HTMLInputElement must not have more than one outstanding chooser.
         * A second request is completed as an empty selection by the caller
         * that owns the first request. */
        if (file_picker_request_id != 0u)
            return;
        ++next_file_picker_request_id;
        if (next_file_picker_request_id == 0u)
            ++next_file_picker_request_id;
        file_picker_request_id = next_file_picker_request_id;
        file_picker_allow_multiple =
            allow_multiple == Web::HTML::AllowMultipleFiles::Yes;
        mark_dirty();
    }

    void request_file(i32 request_id)
    {
        if (request_id < 0 || file_picker_request_id != 0u ||
            pending_file_request_id >= 0)
            return;
        pending_file_request_id = request_id;
        picker_for_file_request = true;
        request_file_picker(Web::HTML::AllowMultipleFiles::No);
    }

    void clear_file_picker_request(bool notify_webcontent)
    {
        if (file_picker_request_id == 0u)
            return;
        const bool is_file_request = picker_for_file_request;
        const i32 file_request_id = pending_file_request_id;
        file_picker_request_id = 0u;
        file_picker_allow_multiple = false;
        picker_for_file_request = false;
        pending_file_request_id = -1;
        if (notify_webcontent && is_file_request && file_request_id >= 0)
            view->client().async_handle_file_return(
                page_id, EPERM, {}, file_request_id);
        else if (notify_webcontent)
            view->file_picker_closed({});
        mark_dirty();
    }

    bool complete_file_picker(RinWebContentFilePickerCompleteV1 const& completion,
                              int descriptor)
    {
        if (file_picker_request_id == 0u ||
            completion.request_id != file_picker_request_id)
            return false;
        if (completion.result == RIN_WEBCONTENT_FILE_PICKER_RESULT_OK &&
            file_picker_allow_multiple)
            return false;

        const bool is_file_request = picker_for_file_request;
        const i32 file_request_id = pending_file_request_id;
        if (completion.result == RIN_WEBCONTENT_FILE_PICKER_RESULT_OK) {
            auto name = ByteString { StringView { completion.display_name,
                                                   completion.display_name_size } };
            if (is_file_request && file_request_id >= 0) {
                (void)name;
                view->client().async_handle_file_return(
                    page_id, 0, IPC::File::adopt_fd(descriptor),
                    file_request_id);
            } else {
                Vector<Web::HTML::SelectedFile> files;
                files.append(Web::HTML::SelectedFile {
                    move(name), IPC::File::adopt_fd(descriptor) });
                view->file_picker_closed(move(files));
            }
        } else {
            if (descriptor >= 0)
                ::close(descriptor);
            if (is_file_request && file_request_id >= 0)
                view->client().async_handle_file_return(
                    page_id, EPERM, {}, file_request_id);
            else
                view->file_picker_closed({});
        }
        file_picker_request_id = 0u;
        file_picker_allow_multiple = false;
        picker_for_file_request = false;
        pending_file_request_id = -1;
        mark_dirty();
        return true;
    }

    bool complete_file_picker_multiple(
        RinWebContentFilePickerCompleteV2 const& completion,
        int* descriptors, size_t descriptor_count)
    {
        if (descriptor_count > RIN_WEBCONTENT_FILE_PICKER_MAX_SELECTIONS ||
            file_picker_request_id == 0u ||
            completion.request_id != file_picker_request_id ||
            !file_picker_allow_multiple ||
            completion.selection_count != descriptor_count ||
            (completion.result == RIN_WEBCONTENT_FILE_PICKER_RESULT_OK &&
             (descriptors == nullptr || descriptor_count == 0u)) ||
            (completion.result != RIN_WEBCONTENT_FILE_PICKER_RESULT_OK &&
             descriptor_count != 0u))
            return false;
        if (completion.result != RIN_WEBCONTENT_FILE_PICKER_RESULT_OK) {
            view->file_picker_closed({});
            file_picker_request_id = 0u;
            file_picker_allow_multiple = false;
            picker_for_file_request = false;
            pending_file_request_id = -1;
            mark_dirty();
            return true;
        }
        Vector<Web::HTML::SelectedFile> files;
        files.ensure_capacity(descriptor_count);
        for (size_t index = 0u; index < descriptor_count; ++index) {
            if (descriptors[index] < 0) return false;
            auto name = ByteString { StringView {
                completion.selections[index].display_name,
                completion.selections[index].display_name_size } };
            files.append(Web::HTML::SelectedFile {
                move(name), IPC::File::adopt_fd(descriptors[index]) });
            descriptors[index] = -1;
        }
        view->file_picker_closed(move(files));
        file_picker_request_id = 0u;
        file_picker_allow_multiple = false;
        picker_for_file_request = false;
        pending_file_request_id = -1;
        mark_dirty();
        return true;
    }

    void note_download_event(u64 transfer_id,
                             WebView::FileDownloader::DownloadEvent event,
                             ByteString url, ByteString filename, u64 size)
    {
        if (transfer_id == 0u || url.is_empty() || filename.is_empty() ||
            url.length() >= RIN_WEBCONTENT_URL_MAX ||
            filename.length() >= RIN_WEBCONTENT_FILE_PICKER_NAME_MAX ||
            download_events.size() >= 16u) {
            if (event == WebView::FileDownloader::DownloadEvent::Completed) {
                for (size_t index = 0u; index < active_downloads.size(); ++index) {
                    if (active_downloads[index].transfer_id == transfer_id) {
                        active_downloads.remove(index);
                        break;
                    }
                }
            }
            return;
        }
        DownloadEventRecord record;
        record.event = event == WebView::FileDownloader::DownloadEvent::Started
            ? RIN_WEBCONTENT_DOWNLOAD_EVENT_STARTED
            : RIN_WEBCONTENT_DOWNLOAD_EVENT_COMPLETED;
        record.revision = next_download_event_revision++;
        if (record.revision == 0u)
            record.revision = next_download_event_revision++;
        record.transfer_id = transfer_id;
        record.timestamp_ms = monotonic_time_ms();
        record.size_bytes = size;
        record.url = move(url);
        record.filename = move(filename);
        if (record.event == RIN_WEBCONTENT_DOWNLOAD_EVENT_STARTED)
            active_downloads.append(record);
        else {
            for (size_t index = 0u; index < active_downloads.size(); ++index) {
                if (active_downloads[index].transfer_id == transfer_id) {
                    record.url = active_downloads[index].url;
                    record.filename = active_downloads[index].filename;
                    active_downloads.remove(index);
                    break;
                }
            }
            for (size_t index = 0u; index < retryable_downloads.size(); ++index) {
                if (retryable_downloads[index].transfer_id == transfer_id) {
                    retryable_downloads.remove(index);
                    break;
                }
            }
        }
        download_events.append(move(record));
        mark_dirty();
    }

    void note_download_failure(u64 transfer_id,
                               WebView::FileDownloader::DownloadFailure failure)
    {
        u32 status = RIN_WEBCONTENT_DOWNLOAD_STATUS_TRANSFER_FAILED;
        switch (failure) {
        case WebView::FileDownloader::DownloadFailure::Cancelled:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_CANCELLED;
            break;
        case WebView::FileDownloader::DownloadFailure::UnsafeURL:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_UNSAFE_URL;
            break;
        case WebView::FileDownloader::DownloadFailure::UnsafeFilename:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_UNSAFE_FILENAME;
            break;
        case WebView::FileDownloader::DownloadFailure::SizeRejected:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_SIZE_REJECTED;
            break;
        case WebView::FileDownloader::DownloadFailure::FileManagerUnavailable:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_FILE_MANAGER_UNAVAILABLE;
            break;
        case WebView::FileDownloader::DownloadFailure::DurabilityFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_DURABILITY_FAILED;
            break;
        case WebView::FileDownloader::DownloadFailure::TLSFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_TLS_FAILED;
            break;
        case WebView::FileDownloader::DownloadFailure::NetworkFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_NETWORK_FAILED;
            break;
        case WebView::FileDownloader::DownloadFailure::HttpFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_HTTP_FAILED;
            break;
        case WebView::FileDownloader::DownloadFailure::ResponseInvalid:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_RESPONSE_INVALID;
            break;
        case WebView::FileDownloader::DownloadFailure::MemoryFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_MEMORY_FAILED;
            break;
        case WebView::FileDownloader::DownloadFailure::TransferFailed:
            status = RIN_WEBCONTENT_DOWNLOAD_STATUS_TRANSFER_FAILED;
            break;
        }
        download_status = status;
        ++download_status_revision;
        if (download_status_revision == 0)
            ++download_status_revision;
        download_status_timestamp_ms = monotonic_time_ms();
        size_t started_index = active_downloads.size();
        for (size_t index = 0u; index < active_downloads.size(); ++index) {
            if (active_downloads[index].transfer_id == transfer_id) {
                started_index = index;
                break;
            }
        }
        if (started_index < active_downloads.size()) {
            DownloadEventRecord failed = active_downloads[started_index];
            active_downloads.remove(started_index);
            failed.event = RIN_WEBCONTENT_DOWNLOAD_EVENT_FAILED;
            failed.revision = next_download_event_revision++;
            if (failed.revision == 0u)
                failed.revision = next_download_event_revision++;
            failed.timestamp_ms = download_status_timestamp_ms;
            failed.size_bytes = 0u;
            failed.failure_status = status;
            for (size_t index = 0u; index < retryable_downloads.size(); ++index) {
                if (retryable_downloads[index].transfer_id == transfer_id) {
                    retryable_downloads.remove(index);
                    break;
                }
            }
            if (retryable_downloads.size() >= 16u)
                retryable_downloads.remove(0u);
            retryable_downloads.append(failed);
            if (download_events.size() < 16u)
                download_events.append(move(failed));
        }
        mark_dirty();
    }

    bool cancel_download(u64 transfer_id)
    {
        if (transfer_id == 0u)
            return false;
        return WebView::Application::the().file_downloader().cancel_download(
            transfer_id);
    }

    bool retry_download(u64 transfer_id)
    {
        if (transfer_id == 0u)
            return false;
        size_t found = retryable_downloads.size();
        for (size_t index = 0u; index < retryable_downloads.size(); ++index) {
            if (retryable_downloads[index].transfer_id == transfer_id) {
                found = index;
                break;
            }
        }
        if (found == retryable_downloads.size())
            return false;

        auto record = retryable_downloads[found];
        retryable_downloads.remove(found);
        auto parsed_url = URL::Parser::basic_parse(record.url);
        if (!parsed_url.has_value() || record.filename.is_empty())
            return false;
        /* An unconsumed failure event must not be replayed ahead of the new
         * STARTED event. The transfer identity is stable, so the Browser can
         * keep the same row while the underlying request is fresh. */
        for (size_t index = 0u; index < download_events.size();) {
            if (download_events[index].transfer_id == transfer_id)
                download_events.remove(index);
            else
                ++index;
        }
        start_download(parsed_url.value(), move(record.filename), transfer_id);
        mark_dirty();
        return true;
    }

    void fill_download_event(u32 after_revision,
                             RinWebContentDownloadEventV1& output)
    {
        __builtin_memset(&output, 0, sizeof(output));
        output.struct_size = sizeof(output);
        output.version = RIN_WEBCONTENT_EXTENSION_ABI_VERSION;
        size_t selected = download_events.size();
        for (size_t index = 0u; index < download_events.size(); ++index) {
            if (download_events[index].revision > after_revision) {
                selected = index;
                break;
            }
        }
        if (selected == download_events.size())
            return;
        const auto& event = download_events[selected];
        output.event = event.event;
        output.revision = event.revision;
        output.transfer_id = event.transfer_id;
        output.timestamp_ms = event.timestamp_ms;
        output.size_bytes = event.size_bytes;
        output.failure_status = event.failure_status;
        copy_c_string(output.url, sizeof(output.url), event.url);
        copy_c_string(output.filename, sizeof(output.filename), event.filename);
        download_events.remove(0u, selected + 1u);
        mark_dirty();
    }

    void close_paint_shm()
    {
        if (paint_shm_handle >= 0) {
            rin_shm_dt(paint_shm_handle, paint_shm_addr);
            paint_shm_handle = -1;
            paint_shm_addr = nullptr;
        }
        paint_shm_size = 0;
        paint_shm_name[0] = '\0';
    }

    bool has_visible_bitmap() const
    {
        auto bitmap = view->visible_bitmap();
        auto size = view->visible_bitmap_size();
        return bitmap && size.width() > 0 && size.height() > 0;
    }

    bool has_first_paint_for_active_navigation() const
    {
        return paint_revision > navigation_paint_revision_baseline;
    }

    void clear_first_frame_wait()
    {
        first_frame_pending = false;
        first_frame_started_ms = 0;
        first_frame_kick_count = 0;
    }

    bool pending_load_requires_load_start() const
    {
        return pending_load_kind == PendingLoadKind::Navigate;
    }

    bool is_waiting_for_load_start() const
    {
        return pending_load_requires_load_start() && !pending_load_started && !pending_load_expired;
    }

    ByteString active_builtin_shell_url() const
    {
        if (!builtin_shell_url.is_empty())
            return builtin_shell_url;
        if (is_browser_builtin_url(pending_url))
            return pending_url;
        if (is_browser_builtin_url(committed_url))
            return committed_url;
        if (pending_load_kind == PendingLoadKind::Markup && is_browser_builtin_url(pending_load_target_url))
            return pending_load_target_url;
        return {};
    }

    ByteString remap_markup_internal_url(ByteString url) const
    {
        if (!is_internal_markup_document_url(url))
            return url;

        auto shell_url = active_builtin_shell_url();
        if (!shell_url.is_empty())
            return shell_url;
        return url;
    }

    void clear_pending_load_request()
    {
        if (pending_load_replay_timer)
            pending_load_replay_timer->stop();
        pending_load_kind = PendingLoadKind::None;
        pending_load_target_url = {};
        pending_load_markup = {};
        pending_load_requested_ms = 0;
        pending_load_last_dispatch_ms = 0;
        pending_load_retry_count = 0;
        pending_load_transport_ready_ms = 0;
        pending_load_started = false;
        pending_load_expired = false;
        logged_pre_navigation_paint = false;
    }

    void note_pending_load_started()
    {
        pending_load_started = true;
        pending_load_expired = false;
        pending_load_retry_count = 0;
        pending_load_last_dispatch_ms = 0;
        pending_load_requested_ms = 0;
        pending_load_kind = PendingLoadKind::None;
        pending_load_target_url = {};
        pending_load_markup = {};
    }

    void fail_pending_load_request(StringView reason)
    {
        auto target_url = pending_load_target_url;
        auto pending_kind = pending_load_kind;

        loading = false;
        crashed = true;
        progress_percent = 0;
        crash_reason = ByteString { reason };
        metrics_dirty = true;
        waiting_for_first_paint_after_load_finish = false;
        active_navigation_request_id = 0;
        clear_first_frame_wait();
        clear_pending_load_request();

        if (pending_url.is_empty()) {
            if (!target_url.is_empty())
                pending_url = target_url;
            else if (!committed_url.is_empty())
                pending_url = committed_url;
        }

        auto message = ByteString::formatted(
            "[webcontent] page {} pending load failed kind={} reason={} target={}\n",
            page_id,
            pending_load_kind_name(pending_kind),
            reason,
            pending_url.is_empty() ? committed_url : pending_url);
        rin_log(message.characters());
        mark_dirty();
    }

    void prime_pending_load_request(PendingLoadKind kind, ByteString target_url, ByteString markup = {})
    {
        clear_pending_load_request();
        pending_load_kind = kind;
        pending_load_target_url = move(target_url);
        pending_load_markup = move(markup);
        pending_load_requested_ms = monotonic_time_ms();
    }

    bool dispatch_pending_load_request(bool replay)
    {
        if (pending_load_kind == PendingLoadKind::None)
            return false;

        auto pending_kind = pending_load_kind;
        auto target_url = pending_load_target_url;
        auto markup_length = pending_load_markup.length();

        if (pending_kind == PendingLoadKind::Navigate) {
            auto parsed = URL::Parser::basic_parse(pending_load_target_url);
            if (!parsed.has_value())
                return false;
            view->load(parsed.release_value());
        } else {
            view->load_html(pending_load_markup);
            auto shell_url = target_url.is_empty() ? ByteString { "about:blank" } : target_url;
            pending_url = shell_url;
            committed_url = ByteString { "about:blank" };
            loading = true;
            if (progress_percent < 20)
                progress_percent = 20;
            clear_pending_load_request();
            kick_first_frame_if_needed("load-markup"sv, true);
            auto wait_message = ByteString::formatted(
                "[webcontent] page {} built-in first-frame wait start {}\n",
                page_id,
                shell_url);
            rin_log(wait_message.characters());
        }

        pending_load_last_dispatch_ms = monotonic_time_ms();
        if (replay)
            ++pending_load_retry_count;

        if (replay) {
            auto message = ByteString::formatted(
                "[webcontent] page {} load replay #{} kind={} target={}\n",
                page_id,
                pending_load_retry_count,
                pending_load_kind_name(pending_kind),
                target_url);
            rin_log(message.characters());
        } else if (pending_kind == PendingLoadKind::Navigate) {
            auto message = ByteString::formatted(
                "[webcontent] page {} navigate accepted {}\n",
                page_id,
                target_url);
            rin_log(message.characters());
        } else {
            auto message = ByteString::formatted(
                "[webcontent] page {} load_markup accepted base={} bytes={}\n",
                page_id,
                target_url,
                markup_length);
            rin_log(message.characters());
        }

        return true;
    }

    void maybe_replay_pending_load_request()
    {
        if (!is_waiting_for_load_start())
            return;

        auto now = monotonic_time_ms();
        // ViewImplementation can be constructed before the cold WebContent
        // process has initialized its rendering thread. Navigation IPC sent in
        // that interval is not actionable. Backing-store allocation is the
        // first positive acknowledgement from that process, so start the 10s
        // retry budget only after it arrives.
        if (!view->has_allocated_backing_stores())
            return;

        if (pending_load_transport_ready_ms == 0) {
            pending_load_transport_ready_ms = now;
            pending_load_requested_ms = now;
            pending_load_retry_count = 0;
            auto message = ByteString::formatted(
                "[webcontent] page {} load transport ready target={}\n",
                page_id,
                pending_load_target_url);
            rin_log(message.characters());
        }

        if (pending_load_requested_ms == 0)
            pending_load_requested_ms = now;

        if (now - pending_load_requested_ms >= s_load_start_retry_budget_ms
            || pending_load_retry_count >= s_load_start_retry_limit) {
            if (!pending_load_expired) {
                pending_load_expired = true;
                auto message = ByteString::formatted(
                    "[webcontent] page {} load request expired without load start kind={} target={}\n",
                    page_id,
                    pending_load_kind_name(pending_load_kind),
                    pending_load_target_url);
                rin_log(message.characters());
                fail_pending_load_request("WebContent failed to start page load"sv);
            }
            return;
        }

        if (pending_load_last_dispatch_ms != 0
            && now - pending_load_last_dispatch_ms < s_load_start_retry_interval_ms) {
            return;
        }

        if (!dispatch_pending_load_request(true)) {
            if (!pending_load_expired) {
                pending_load_expired = true;
                auto message = ByteString::formatted(
                    "[webcontent] page {} load replay dispatch failed kind={} target={}\n",
                    page_id,
                    pending_load_kind_name(pending_load_kind),
                    pending_load_target_url);
                rin_log(message.characters());
                fail_pending_load_request("WebContent load replay failed"sv);
            }
        }
    }

    void schedule_load_dispatch_observation()
    {
        auto scheduled_page_id = page_id;
        if (!pending_load_replay_timer) {
            pending_load_replay_timer = Core::Timer::create_repeating(
                static_cast<int>(s_load_start_retry_interval_ms), [scheduled_page_id] {
                    auto* page = find_page(scheduled_page_id);
                    if (!page)
                        return;
                    page->maybe_replay_pending_load_request();
                });
        }
        pending_load_replay_timer->start();
        Core::deferred_invoke([scheduled_page_id] {
            auto* page = find_page(scheduled_page_id);
            if (!page)
                return;
            page->drain_pending_bridge_events(false);
        });
    }

    void kick_first_frame_if_needed(StringView reason, bool reset_timer)
    {
        if (has_first_paint_for_active_navigation()) {
            last_paint_revision_seen = paint_revision;
            clear_first_frame_wait();
            return;
        }

        if (!first_frame_pending || reset_timer || first_frame_started_ms == 0) {
            first_frame_pending = true;
            first_frame_started_ms = monotonic_time_ms();
            first_frame_kick_count = 0;
            logged_missing_visible_bitmap = false;
            navigation_paint_revision_baseline = paint_revision;
        }

        ++first_frame_kick_count;
        last_first_frame_kick_ms = monotonic_time_ms();
        view->reset_viewport_size({ requested_viewport_width, requested_viewport_height });
        metrics_dirty = true;
        mark_dirty();

        if (first_frame_kick_count == 1 || reset_timer) {
            auto message = ByteString::formatted(
                "[webcontent] page {} kick first frame #{} reason={} viewport={}x{}\n",
                page_id,
                first_frame_kick_count,
                reason,
                requested_viewport_width,
                requested_viewport_height);
            rin_log(message.characters());
        }
    }

    void drain_pending_bridge_events(bool wait_for_bitmap, bool allow_load_replay = true)
    {
        if (allow_load_replay)
            maybe_replay_pending_load_request();

        bool waiting_for_load_start = is_waiting_for_load_start();
        auto now_ms = monotonic_time_ms();
        if (!waiting_for_load_start
            && !has_first_paint_for_active_navigation()
            && first_frame_pending
            && (last_first_frame_kick_ms == 0 || now_ms - last_first_frame_kick_ms >= s_load_start_retry_interval_ms)) {
            kick_first_frame_if_needed("poll"sv, false);
        }
        if (!waiting_for_load_start && has_first_paint_for_active_navigation() && (!wait_for_bitmap || has_visible_bitmap()))
            return;

        bool saw_backing_stores = view->has_allocated_backing_stores();
        auto start_revision = paint_revision;
        bool saw_load_start = !waiting_for_load_start;
        bool timed_out = false;
        auto timeout = Core::Timer::create_single_shot(12, [&timed_out] {
            timed_out = true;
            Core::EventLoop::current().wake();
        });

        timeout->start();
        Core::EventLoop::current().spin_until([&] {
            if (!saw_backing_stores && view->has_allocated_backing_stores()) {
                saw_backing_stores = true;
                auto message = ByteString::formatted("[webcontent] page {} backing stores allocated\n", page_id);
                rin_log(message.characters());
            }

            if (has_first_paint_for_active_navigation()) {
                last_paint_revision_seen = paint_revision;
                clear_first_frame_wait();
            }

            if (!saw_load_start && !is_waiting_for_load_start())
                saw_load_start = true;

            if (timed_out)
                return true;
            if (crashed)
                return true;
            if (paint_revision != start_revision)
                return true;
            if (!saw_load_start)
                return false;
            if (wait_for_bitmap && has_visible_bitmap())
                return true;
            return false;
        });
        timeout->stop();
    }

    bool ensure_paint_shm(size_t size)
    {
        if (paint_shm_handle >= 0 && paint_shm_addr && paint_shm_size == size)
            return true;

        close_paint_shm();
        auto name = ByteString::formatted("wc-ladybird-{}-{}", page_id, rin_time());
        copy_c_string(paint_shm_name, sizeof(paint_shm_name), name);

        paint_shm_handle = rin_shm_get(paint_shm_name,
            static_cast<u32>(size),
            RIN_SHM_FLAG_CREAT | RIN_SHM_FLAG_EXCL | RIN_SHM_FLAG_UNLINK_ON_CLOSE);
        if (paint_shm_handle < 0)
            return false;

        paint_shm_addr = rin_shm_at(paint_shm_handle, nullptr, RIN_SHM_PROT_READ | RIN_SHM_PROT_WRITE);
        if (!paint_shm_addr) {
            close_paint_shm();
            return false;
        }

        paint_shm_size = size;
        return true;
    }

    void refresh_metrics(bool force = false)
    {
        if (!force && !metrics_dirty)
            return;

        // Keep the transport viewport authoritative until WebContent has at least
        // allocated its backing stores. Early JS metrics can transiently report 0x0
        // and accidentally collapse the bootstrap viewport if we reuse them.
        if (paint_revision == 0 && !view->has_allocated_backing_stores())
            return;

        auto result = view->evaluate_json(s_metrics_script);
        if (result.is_error())
            return;

        auto json = result.release_value();
        if (!json.is_object())
            return;

        auto const& object = json.as_object();
        if (auto value = object.get_i32("scrollX"sv); value.has_value())
            scroll_x = *value;
        if (auto value = object.get_i32("scrollY"sv); value.has_value())
            scroll_y = *value;
        if (auto value = object.get_i32("maxScrollX"sv); value.has_value())
            max_scroll_x = *value;
        if (auto value = object.get_i32("maxScrollY"sv); value.has_value())
            max_scroll_y = *value;
        if (auto value = object.get_i32("contentWidth"sv); value.has_value())
            content_width = *value;
        if (auto value = object.get_i32("contentHeight"sv); value.has_value())
            content_height = *value;
        if (auto value = object.get_i32("viewportWidth"sv); value.has_value())
            reported_viewport_width = *value;
        if (auto value = object.get_i32("viewportHeight"sv); value.has_value())
            reported_viewport_height = *value;
        if (auto value = object.get_string("title"sv); value.has_value() && !value->is_empty())
            title = value->to_byte_string();
        if (auto value = object.get_string("url"sv); value.has_value() && !value->is_empty()) {
            auto reported_url = remap_markup_internal_url(value->to_byte_string());
            if (loading)
                pending_url = move(reported_url);
            else
                committed_url = move(reported_url);
        }
        if (auto value = object.get_bool("textInputEnabled"sv); value.has_value())
            text_input_enabled = *value;
        if (auto value = object.get_i32("textInputContentType"sv); value.has_value())
            text_input_content_type = clamp(*value, 0, 3);
        if (auto value = object.get_i32("textInputX"sv); value.has_value())
            text_input_x = *value;
        if (auto value = object.get_i32("textInputY"sv); value.has_value())
            text_input_y = *value;
        if (auto value = object.get_i32("textInputWidth"sv); value.has_value())
            text_input_width = max(*value, 1);
        if (auto value = object.get_i32("textInputHeight"sv); value.has_value())
            text_input_height = max(*value, 1);

        metrics_dirty = false;
    }

    bool navigate(ByteString const& url)
    {
        clear_file_picker_request(true);
        crashed = false;
        crash_reason = {};
        active_navigation_request_id = 0;
        builtin_shell_url = {};
        text_input_enabled = false;
        // Navigation has been accepted even if the cold WebContent view has not
        // emitted on_load_start yet. Publish the pending state immediately so
        // the browser does not mistake service startup latency for a crash.
        loading = true;
        progress_percent = 10;
        pending_url = url;
        waiting_for_first_paint_after_load_finish = false;
        metrics_dirty = true;
        prime_pending_load_request(PendingLoadKind::Navigate, url);
        if (!dispatch_pending_load_request(false)) {
            clear_pending_load_request();
            loading = false;
            progress_percent = 0;
            pending_url = {};
            return false;
        }
        mark_dirty();
        schedule_load_dispatch_observation();
        return true;
    }

    bool load_markup(ByteString const& base_url, ByteString const& markup)
    {
        clear_file_picker_request(true);
        crashed = false;
        crash_reason = {};
        active_navigation_request_id = 0;
        text_input_enabled = false;
        auto shell_url = base_url.is_empty() ? ByteString { "about:blank" } : base_url;
        builtin_shell_url = is_browser_builtin_url(shell_url) ? shell_url : ByteString {};
        loading = true;
        progress_percent = 20;
        pending_url = shell_url;
        committed_url = ByteString { "about:blank" };
        waiting_for_first_paint_after_load_finish = false;
        metrics_dirty = true;
        prime_pending_load_request(PendingLoadKind::Markup,
            shell_url,
            ByteString { markup });
        if (!dispatch_pending_load_request(false)) {
            clear_pending_load_request();
            return false;
        }
        mark_dirty();
        schedule_load_dispatch_observation();
        return true;
    }

    bool scroll_to(i32 x, i32 y)
    {
        auto script = ByteString::formatted(
            "window.scrollTo({}, {}); ({})",
            x,
            y,
            s_metrics_script);

        auto result = view->evaluate_json(script);
        if (result.is_error())
            return false;
        metrics_dirty = true;
        refresh_metrics(true);
        mark_dirty();
        return true;
    }

    static Web::UIEvents::MouseButton button_from_abi(i32 button)
    {
        switch (button) {
        case 1:
            return Web::UIEvents::MouseButton::Primary;
        case 2:
            return Web::UIEvents::MouseButton::Middle;
        case 3:
            return Web::UIEvents::MouseButton::Secondary;
        default:
            return Web::UIEvents::MouseButton::None;
        }
    }

    bool dispatch_pointer(RinWebContentPointerRequest const& request)
    {
        Web::MouseEvent event {};
        switch (request.pointer_type) {
        case RIN_WEBCONTENT_POINTER_DOWN:
            event.type = Web::MouseEvent::Type::MouseDown;
            pressed_buttons |= button_from_abi(request.button);
            break;
        case RIN_WEBCONTENT_POINTER_UP:
            event.type = Web::MouseEvent::Type::MouseUp;
            pressed_buttons &= ~button_from_abi(request.button);
            break;
        case RIN_WEBCONTENT_POINTER_MOVE:
            event.type = Web::MouseEvent::Type::MouseMove;
            break;
        default:
            return false;
        }

        event.position = { request.x, request.y };
        event.screen_position = event.position;
        event.button = button_from_abi(request.button);
        event.buttons = pressed_buttons;
        view->enqueue_input_event(Web::InputEvent { move(event) });
        metrics_dirty = true;
        mark_dirty();
        return true;
    }

    static bool map_ascii_key(unsigned char ch, Web::KeyEvent& event)
    {
        using Web::UIEvents::KeyCode;
        using Web::UIEvents::KeyModifier;

        auto set_key = [&](KeyCode key, u32 code_point, KeyModifier modifiers = KeyModifier::Mod_None) {
            event.key = key;
            event.code_point = code_point;
            event.modifiers = modifiers;
            return true;
        };

        if (ch >= 'a' && ch <= 'z')
            return set_key(static_cast<KeyCode>(static_cast<u32>(KeyCode::Key_A) + (ch - 'a')), ch);
        if (ch >= 'A' && ch <= 'Z')
            return set_key(static_cast<KeyCode>(static_cast<u32>(KeyCode::Key_A) + (ch - 'A')), ch, KeyModifier::Mod_Shift);
        if (ch >= '0' && ch <= '9')
            return set_key(static_cast<KeyCode>(static_cast<u32>(KeyCode::Key_0) + (ch - '0')), ch);

        switch (ch) {
        case ' ':
            return set_key(KeyCode::Key_Space, ch);
        case '.':
            return set_key(KeyCode::Key_Period, ch);
        case ',':
            return set_key(KeyCode::Key_Comma, ch);
        case '/':
            return set_key(KeyCode::Key_Slash, ch);
        case '?':
            return set_key(KeyCode::Key_Slash, ch, KeyModifier::Mod_Shift);
        case ';':
            return set_key(KeyCode::Key_Semicolon, ch);
        case ':':
            return set_key(KeyCode::Key_Semicolon, ch, KeyModifier::Mod_Shift);
        case '\'':
            return set_key(KeyCode::Key_Apostrophe, ch);
        case '"':
            return set_key(KeyCode::Key_Apostrophe, ch, KeyModifier::Mod_Shift);
        case '-':
            return set_key(KeyCode::Key_Minus, ch);
        case '_':
            return set_key(KeyCode::Key_Minus, ch, KeyModifier::Mod_Shift);
        case '=':
            return set_key(KeyCode::Key_Equal, ch);
        case '+':
            return set_key(KeyCode::Key_Equal, ch, KeyModifier::Mod_Shift);
        case '[':
            return set_key(KeyCode::Key_LeftBracket, ch);
        case '{':
            return set_key(KeyCode::Key_LeftBracket, ch, KeyModifier::Mod_Shift);
        case ']':
            return set_key(KeyCode::Key_RightBracket, ch);
        case '}':
            return set_key(KeyCode::Key_RightBracket, ch, KeyModifier::Mod_Shift);
        case '\\':
            return set_key(KeyCode::Key_Backslash, ch);
        case '|':
            return set_key(KeyCode::Key_Backslash, ch, KeyModifier::Mod_Shift);
        case '`':
            return set_key(KeyCode::Key_Backtick, ch);
        case '~':
            return set_key(KeyCode::Key_Backtick, ch, KeyModifier::Mod_Shift);
        case '!':
            return set_key(KeyCode::Key_1, ch, KeyModifier::Mod_Shift);
        case '@':
            return set_key(KeyCode::Key_2, ch, KeyModifier::Mod_Shift);
        case '#':
            return set_key(KeyCode::Key_3, ch, KeyModifier::Mod_Shift);
        case '$':
            return set_key(KeyCode::Key_4, ch, KeyModifier::Mod_Shift);
        case '%':
            return set_key(KeyCode::Key_5, ch, KeyModifier::Mod_Shift);
        case '^':
            return set_key(KeyCode::Key_6, ch, KeyModifier::Mod_Shift);
        case '&':
            return set_key(KeyCode::Key_7, ch, KeyModifier::Mod_Shift);
        case '*':
            return set_key(KeyCode::Key_8, ch, KeyModifier::Mod_Shift);
        case '(':
            return set_key(KeyCode::Key_9, ch, KeyModifier::Mod_Shift);
        case ')':
            return set_key(KeyCode::Key_0, ch, KeyModifier::Mod_Shift);
        case '<':
            return set_key(KeyCode::Key_Comma, ch, KeyModifier::Mod_Shift);
        case '>':
            return set_key(KeyCode::Key_Period, ch, KeyModifier::Mod_Shift);
        default:
            return false;
        }
    }

    bool dispatch_key_or_text(RinWebContentKeyOrTextRequest const& request)
    {
        auto send_key = [&](Web::UIEvents::KeyCode key, u32 code_point, Web::UIEvents::KeyModifier modifiers) {
            Web::KeyEvent down {};
            down.type = Web::KeyEvent::Type::KeyDown;
            down.key = key;
            down.code_point = code_point;
            down.modifiers = modifiers;
            view->enqueue_input_event(Web::InputEvent { move(down) });

            Web::KeyEvent up {};
            up.type = Web::KeyEvent::Type::KeyUp;
            up.key = key;
            up.code_point = code_point;
            up.modifiers = modifiers;
            view->enqueue_input_event(Web::InputEvent { move(up) });
        };

        size_t text_length = 0;
        while (text_length < sizeof(request.text) && request.text[text_length] != '\0')
            ++text_length;
        if (text_length == sizeof(request.text))
            return false;

        if (text_length != 0) {
            auto text_or_error = String::from_utf8(StringView { request.text, text_length });
            if (text_or_error.is_error())
                return false;
            auto text = text_or_error.release_value();
            for (auto code_point : text.code_points()) {
                Web::KeyEvent event {};
                if (code_point <= 0x7Fu && map_ascii_key(code_point, event))
                    send_key(event.key, event.code_point, event.modifiers);
                else
                    send_key(Web::UIEvents::KeyCode::Key_Invalid,
                             code_point, Web::UIEvents::KeyModifier::Mod_None);
            }
            metrics_dirty = true;
            mark_dirty();
            return true;
        }

        using Web::UIEvents::KeyCode;
        switch (request.action) {
        case RIN_WEBCONTENT_KEY_ACTION_BACKSPACE:
            send_key(KeyCode::Key_Backspace, 0, Web::UIEvents::KeyModifier::Mod_None);
            break;
        case RIN_WEBCONTENT_KEY_ACTION_ENTER:
            send_key(KeyCode::Key_Return, '\n', Web::UIEvents::KeyModifier::Mod_None);
            break;
        case RIN_WEBCONTENT_KEY_ACTION_TAB:
            send_key(KeyCode::Key_Tab, '\t', Web::UIEvents::KeyModifier::Mod_None);
            break;
        case RIN_WEBCONTENT_KEY_ACTION_ESCAPE:
            send_key(KeyCode::Key_Escape, 0, Web::UIEvents::KeyModifier::Mod_None);
            break;
        case RIN_WEBCONTENT_KEY_ACTION_NONE:
            return true;
        default:
            return false;
        }

        metrics_dirty = true;
        mark_dirty();
        return true;
    }

    void fill_state(RinWebContentPageState& state)
    {
        refresh_metrics();

        auto effective_viewport_width = reported_viewport_width > 0 ? reported_viewport_width : requested_viewport_width;
        auto effective_viewport_height = reported_viewport_height > 0 ? reported_viewport_height : requested_viewport_height;

        __builtin_memset(&state, 0, sizeof(state));
        state.page_id = page_id;
        if (loading)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_LOADING;
        if (crashed)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_CRASHED;
        if (dirty)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_DIRTY;
        if (text_input_enabled)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_TEXT_INPUT_ENABLED;
        if (download_status != RIN_WEBCONTENT_DOWNLOAD_STATUS_NONE)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_HAS_DOWNLOAD_STATUS;
        if (file_picker_request_id != 0u)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_HAS_FILE_PICKER_REQUEST;

        state.progress_percent = static_cast<u32>(max(progress_percent, 0));
        state.state_revision = state_revision;
        state.paint_revision = paint_revision;
        state.scroll_x = scroll_x;
        state.scroll_y = scroll_y;
        state.max_scroll_x = max_scroll_x;
        state.max_scroll_y = max_scroll_y;
        state.viewport_width = static_cast<u32>(max(effective_viewport_width, 0));
        state.viewport_height = static_cast<u32>(max(effective_viewport_height, 0));
        state.content_width = static_cast<u32>(max(content_width, effective_viewport_width));
        state.content_height = static_cast<u32>(max(content_height, effective_viewport_height));
        copy_c_string(state.committed_url, sizeof(state.committed_url), committed_url);
        copy_c_string(state.pending_url, sizeof(state.pending_url), pending_url);
        copy_c_string(state.title, sizeof(state.title), title);
        copy_c_string(state.crash_reason, sizeof(state.crash_reason), crash_reason);

        // P5-2: ロードフェーズ情報を埋める
        state.flags |= RIN_WEBCONTENT_STATE_FLAG_HAS_LOAD_PHASE;
        state.load_phase = load_phase_current;
        if (load_phase_started_ms != 0) {
            auto now_ms = monotonic_time_ms();
            state.load_phase_elapsed_ms = (now_ms >= load_phase_started_ms)
                ? static_cast<u32>(now_ms - load_phase_started_ms)
                : 0;
        }
        state.load_bytes_received = 0;
        state.load_bytes_total = 0;
        state.load_scripts_pending = static_cast<u32>(max(load_resources_waiting, 0));
        state.load_scripts_total = static_cast<u32>(max(load_resources_total, 0));
        if (load_suspected_stall)
            state.flags |= RIN_WEBCONTENT_STATE_FLAG_SUSPECTED_STALL;
        copy_c_string(state.load_current_url, sizeof(state.load_current_url), load_current_url_str);
        state.text_input_content_type = static_cast<u32>(text_input_content_type);
        state.text_input_x = text_input_x;
        state.text_input_y = text_input_y;
        state.text_input_width = text_input_width;
        state.text_input_height = text_input_height;
        state.download_status = download_status;
        state.download_status_revision = download_status_revision;
        state.download_status_timestamp_ms = download_status_timestamp_ms;
        state.file_picker_request_id = file_picker_request_id;
        state.file_picker_allow_multiple = file_picker_allow_multiple ? 1u : 0u;
        state.file_picker_reserved0 = 0u;
    }

    u32 page_id { 0 };
    int requested_viewport_width { 800 };
    int requested_viewport_height { 600 };
    int reported_viewport_width { 800 };
    int reported_viewport_height { 600 };
    bool text_input_enabled { false };
    int text_input_content_type { 0 };
    int text_input_x { 0 };
    int text_input_y { 0 };
    int text_input_width { 1 };
    int text_input_height { 1 };
    NonnullOwnPtr<BridgeView> view;

    bool loading { false };
    bool crashed { false };
    bool dirty { true };
    bool metrics_dirty { true };
    int progress_percent { 0 };

    // P5-2: ロード中フェーズのトラッキング
    u32 load_phase_current { RIN_WEBCONTENT_LOAD_PHASE_NONE };
    u64 load_phase_started_ms { 0 };
    int load_resources_waiting { 0 };
    int load_resources_total { 0 };
    ByteString load_current_url_str;
    bool load_suspected_stall { false };
    u32 download_status { RIN_WEBCONTENT_DOWNLOAD_STATUS_NONE };
    u32 download_status_revision { 0 };
    u64 download_status_timestamp_ms { 0 };
    Vector<DownloadEventRecord> download_events;
    Vector<DownloadEventRecord> active_downloads;
    Vector<DownloadEventRecord> retryable_downloads;
    u32 next_download_event_revision { 1 };
    u64 file_picker_request_id { 0 };
    u64 next_file_picker_request_id { 0 };
    bool file_picker_allow_multiple { false };
    bool picker_for_file_request { false };
    i32 pending_file_request_id { -1 };
    int scroll_x { 0 };
    int scroll_y { 0 };
    int max_scroll_x { 0 };
    int max_scroll_y { 0 };
    int content_width { 800 };
    int content_height { 600 };
    u32 state_revision { 1 };
    u32 paint_revision { 0 };
    u32 navigation_paint_revision_baseline { 0 };
    u32 last_paint_revision_seen { 0 };
    ByteString committed_url;
    ByteString pending_url;
    ByteString builtin_shell_url;
    ByteString title;
    ByteString crash_reason;
    Web::UIEvents::MouseButton pressed_buttons { Web::UIEvents::MouseButton::None };
    bool first_frame_pending { false };
    u64 first_frame_started_ms { 0 };
    u32 first_frame_kick_count { 0 };
    u64 last_first_frame_kick_ms { 0 };
    bool logged_missing_visible_bitmap { false };
    bool waiting_for_first_paint_after_load_finish { false };
    u64 active_navigation_request_id { 0 };
    PendingLoadKind pending_load_kind { PendingLoadKind::None };
    ByteString pending_load_target_url;
    ByteString pending_load_markup;
    u64 pending_load_requested_ms { 0 };
    u64 pending_load_last_dispatch_ms { 0 };
    u32 pending_load_retry_count { 0 };
    u64 pending_load_transport_ready_ms { 0 };
    bool pending_load_started { false };
    bool pending_load_expired { false };
    bool logged_pre_navigation_paint { false };
    RefPtr<Core::Timer> pending_load_replay_timer;

    int paint_shm_handle { -1 };
    void* paint_shm_addr { nullptr };
    size_t paint_shm_size { 0 };
    char paint_shm_name[RIN_SHM_NAME_MAX] {};
};

static u16 accessibility_role_for(StringView role)
{
    if (role == "button"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_BUTTON;
    if (role == "checkbox"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_CHECK_BOX;
    if (role == "combobox"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_COMBO_BOX;
    if (role == "dialog"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_DIALOG;
    if (role == "group"sv || role == "document"sv || role == "generic"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_GROUP;
    if (role == "heading"sv || role == "text leaf"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_LABEL;
    if (role == "list"sv || role == "listbox"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_LIST;
    if (role == "menu"sv || role == "menubar"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_MENU;
    if (role == "menuitem"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_MENU_ITEM;
    if (role == "progressbar"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_PROGRESS_BAR;
    if (role == "radio"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_RADIO_BUTTON;
    if (role == "scrollbar"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_SCROLL_VIEW;
    if (role == "slider"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_SLIDER;
    if (role == "tablist"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_TAB_LIST;
    if (role == "table"sv || role == "grid"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_TABLE;
    if (role == "textbox"sv || role == "input"sv)
        return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_TEXT_FIELD;
    if (role == "tree"sv) return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_TREE;
    return RIN_WEBCONTENT_ACCESSIBILITY_ROLE_GROUP;
}

static bool copy_accessibility_json_text(
    JsonObject const& object, StringView key,
    RinWebContentAccessibilityTextV1& destination)
{
    auto value = object.get_string(key);
    if (!value.has_value()) return true;
    auto bytes = value->bytes();
    if (bytes.size() > RIN_WEBCONTENT_ACCESSIBILITY_MAX_TEXT_BYTES)
        return false;
    destination.size = static_cast<u16>(bytes.size());
    if (!bytes.is_empty())
        __builtin_memcpy(destination.bytes, bytes.data(), bytes.size());
    return rin_webcontent_accessibility_text_valid(&destination);
}

static bool append_accessibility_json_node(
    JsonObject const& object, RinWebContentAccessibilitySnapshotV1& snapshot,
    u64 parent_id)
{
    if (snapshot.node_count >= RIN_WEBCONTENT_ACCESSIBILITY_MAX_NODES)
        return false;

    auto node_id = object.get_u64("id"sv);
    if (!node_id.has_value() || *node_id == 0u || *node_id == UINT64_MAX ||
        *node_id == UINT64_MAX - 1u)
        return false;
    auto& node = snapshot.nodes[snapshot.node_count++];
    node.id = *node_id;
    node.parent_id = parent_id;
    auto role = object.get_string("role"sv);
    node.role = role.has_value() ? accessibility_role_for(*role)
                                 : RIN_WEBCONTENT_ACCESSIBILITY_ROLE_GROUP;
    node.state = RIN_WEBCONTENT_ACCESSIBILITY_STATE_VISIBLE |
                 RIN_WEBCONTENT_ACCESSIBILITY_STATE_ENABLED;
    if (node.role == RIN_WEBCONTENT_ACCESSIBILITY_ROLE_BUTTON ||
        node.role == RIN_WEBCONTENT_ACCESSIBILITY_ROLE_MENU_ITEM)
        node.actions = RIN_WEBCONTENT_ACCESSIBILITY_ACTION_ACTIVATE;
    if (!copy_accessibility_json_text(object, "name"sv, node.name) ||
        !copy_accessibility_json_text(object, "description"sv, node.description))
        return false;

    auto children = object.get_array("children"sv);
    if (!children.has_value()) return true;
    for (auto const& child : children->values()) {
        if (!child.is_object() ||
            !append_accessibility_json_node(child.as_object(), snapshot,
                                            node.id))
            return false;
    }
    return true;
}

static bool build_accessibility_snapshot(
    PageSession& page, u64 window,
    RinWebContentAccessibilitySnapshotV1& snapshot)
{
    __builtin_memset(&snapshot, 0, sizeof(snapshot));
    if (window == 0u) return false;
    auto json_or_error = page.view->inspect_accessibility_json();
    if (json_or_error.is_error()) return false;

    snapshot.struct_size = sizeof(snapshot);
    snapshot.version = RIN_WEBCONTENT_VERSION;
    snapshot.window = window;
    snapshot.generation = page.state_revision == 0u ? 1u : page.state_revision;
    auto& root = snapshot.nodes[snapshot.node_count++];
    root.id = UINT64_MAX - 1u;
    root.role = RIN_WEBCONTENT_ACCESSIBILITY_ROLE_WINDOW;
    root.state = RIN_WEBCONTENT_ACCESSIBILITY_STATE_VISIBLE |
                 RIN_WEBCONTENT_ACCESSIBILITY_STATE_ENABLED;
    root.width = page.reported_viewport_width;
    root.height = page.reported_viewport_height;
    auto title = page.title;
    if (title.is_empty()) title = "WebContent";
    auto title_bytes = title.bytes();
    if (title_bytes.size() > RIN_WEBCONTENT_ACCESSIBILITY_MAX_TEXT_BYTES)
        return false;
    root.name.size = static_cast<u16>(title_bytes.size());
    if (!title_bytes.is_empty())
        __builtin_memcpy(root.name.bytes, title_bytes.data(), title_bytes.size());

    auto json_root = json_or_error.value();
    if (json_root.get_u64("id"sv).has_value() &&
        !append_accessibility_json_node(json_root, snapshot, root.id))
        return false;
    return rin_webcontent_client_accessibility_snapshot_valid(&snapshot, window);
}

static HashMap<u32, NonnullOwnPtr<PageSession>> s_pages;
static OwnPtr<BridgeApplication> s_app;
static Core::AnonymousBuffer s_theme;
static RefPtr<Core::Notifier> s_server_notifier;
static RefPtr<Core::Timer> s_stop_timer;

static PageSession* find_page(u32 page_id)
{
    auto it = s_pages.find(page_id);
    if (it == s_pages.end())
        return nullptr;
    return it->value.ptr();
}

static void destroy_page(u32 page_id)
{
    s_pages.remove(page_id);
}

static int handle_create_page(u32 page_id, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentCreatePageRequest))
        return -EINVAL;

    auto const& request = *reinterpret_cast<RinWebContentCreatePageRequest const*>(payload.data());
    destroy_page(page_id);
    s_pages.set(page_id, make<PageSession>(page_id, s_theme, static_cast<int>(request.viewport_width), static_cast<int>(request.viewport_height)));
    auto message = ByteString::formatted("[webcontent] page {} created viewport={}x{}\n", page_id, request.viewport_width, request.viewport_height);
    rin_log(message.characters());
    return 0;
}

static int handle_resize(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentResizeRequest))
        return -EINVAL;

    auto const& request = *reinterpret_cast<RinWebContentResizeRequest const*>(payload.data());
    page.requested_viewport_width = max(static_cast<int>(request.viewport_width), 1);
    page.requested_viewport_height = max(static_cast<int>(request.viewport_height), 1);
    page.reported_viewport_width = page.requested_viewport_width;
    page.reported_viewport_height = page.requested_viewport_height;
    if (!page.has_first_paint_for_active_navigation()) {
        page.kick_first_frame_if_needed("resize"sv, false);
    } else {
        page.view->reset_viewport_size({ page.requested_viewport_width, page.requested_viewport_height });
        page.metrics_dirty = true;
        page.mark_dirty();
    }
    return 0;
}

static int handle_navigate(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentNavigateRequest))
        return -EINVAL;

    auto const& request = *reinterpret_cast<RinWebContentNavigateRequest const*>(payload.data());
    return page.navigate(ByteString { request.url }) ? 0 : -EINVAL;
}

static int handle_load_markup(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() < sizeof(RinWebContentLoadMarkupRequest))
        return -EINVAL;

    auto const& request = *reinterpret_cast<RinWebContentLoadMarkupRequest const*>(payload.data());
    ByteString markup;

    if (request.markup_storage_kind == RIN_WEBCONTENT_STORAGE_SHM) {
        auto handle = rin_shm_get(request.markup_region.name, request.markup_region.size, 0);
        if (handle < 0)
            return -EIO;

        auto* addr = rin_shm_at(handle, nullptr, RIN_SHM_PROT_READ);
        if (!addr) {
            rin_shm_dt(handle, nullptr);
            return -EIO;
        }

        markup = ByteString { StringView { static_cast<char const*>(addr), request.markup_region.size } };
        rin_shm_dt(handle, addr);
    } else {
        auto inline_size = payload.size() - sizeof(RinWebContentLoadMarkupRequest);
        if (inline_size < request.markup_len)
            return -EINVAL;
        auto markup_bytes = payload.slice(sizeof(RinWebContentLoadMarkupRequest), request.markup_len);
        markup = ByteString { StringView { reinterpret_cast<char const*>(markup_bytes.data()), markup_bytes.size() } };
    }

    return page.load_markup(ByteString { request.base_url }, markup) ? 0 : -EIO;
}

static int handle_pointer(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentPointerRequest))
        return -EINVAL;
    auto const& request = *reinterpret_cast<RinWebContentPointerRequest const*>(payload.data());
    return page.dispatch_pointer(request) ? 0 : -EINVAL;
}

static int handle_key_or_text(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentKeyOrTextRequest))
        return -EINVAL;
    auto const& request = *reinterpret_cast<RinWebContentKeyOrTextRequest const*>(payload.data());
    return page.dispatch_key_or_text(request) ? 0 : -EINVAL;
}

static int handle_scroll(PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentScrollRequest))
        return -EINVAL;
    auto const& request = *reinterpret_cast<RinWebContentScrollRequest const*>(payload.data());
    return page.scroll_to(request.x, request.y) ? 0 : -EIO;
}

static void close_received_descriptors(msghdr const& message)
{
    auto const* base = static_cast<u8 const*>(message.msg_control);
    size_t offset = 0u;
    if (!base || message.msg_controllen < sizeof(cmsghdr))
        return;
    while (offset <= message.msg_controllen - sizeof(cmsghdr)) {
        auto const* cmsg = reinterpret_cast<cmsghdr const*>(base + offset);
        if (cmsg->cmsg_len < CMSG_LEN(0u) ||
            cmsg->cmsg_len > message.msg_controllen - offset)
            return;
        auto payload_size = cmsg->cmsg_len - CMSG_LEN(0u);
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS &&
            payload_size % sizeof(int) == 0u) {
            for (size_t index = 0u; index < payload_size / sizeof(int); ++index) {
                int received = -1;
                __builtin_memcpy(&received,
                    CMSG_DATA(const_cast<cmsghdr*>(cmsg)) + index * sizeof(int),
                    sizeof(received));
                if (received >= 0)
                    (void)::close(received);
            }
        }
        auto step = CMSG_ALIGN(cmsg->cmsg_len);
        if (step == 0u || step > message.msg_controllen - offset)
            return;
        offset += step;
    }
}

static bool receive_file_picker_payload(int client_fd, void* output,
                                        size_t output_size, int& descriptor,
                                        u64 deadline_ms)
{
    iovec iov {};
    alignas(struct cmsghdr) u8 control[CMSG_SPACE(sizeof(int) * 2u)] {};
    msghdr message {};
    descriptor = -1;
    iov.iov_base = output;
    iov.iov_len = output_size;
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t received = recvmsg(client_fd, &message, 0);
    if (received < 0 || static_cast<size_t>(received) > output_size ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        close_received_descriptors(message);
        return false;
    }
    auto* cmsg = CMSG_FIRSTHDR(&message);
    if (cmsg) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS ||
            cmsg->cmsg_len != CMSG_LEN(sizeof(int)) ||
            CMSG_NXTHDR(&message, cmsg) != nullptr) {
            close_received_descriptors(message);
            return false;
        }
        __builtin_memcpy(&descriptor, CMSG_DATA(cmsg), sizeof(descriptor));
        if (descriptor < 0) {
            close_received_descriptors(message);
            descriptor = -1;
            return false;
        }
    }
    if (static_cast<size_t>(received) < output_size &&
        !recv_all(client_fd,
                  static_cast<u8*>(output) + received,
                  output_size - static_cast<size_t>(received), deadline_ms)) {
        if (descriptor >= 0)
            (void)::close(descriptor);
        descriptor = -1;
        return false;
    }
    return true;
}

static bool receive_file_picker_payload_multiple(
    int client_fd, void* output, size_t output_size, int* descriptors,
    size_t descriptor_capacity, size_t& descriptor_count, u64 deadline_ms)
{
    iovec iov {};
    alignas(struct cmsghdr)
        u8 control[CMSG_SPACE(sizeof(int) * RIN_WEBCONTENT_FILE_PICKER_MAX_SELECTIONS)] {};
    msghdr message {};
    descriptor_count = 0u;
    for (size_t index = 0u; index < descriptor_capacity; ++index)
        descriptors[index] = -1;
    iov.iov_base = output;
    iov.iov_len = output_size;
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ssize_t received = recvmsg(client_fd, &message, 0);
    if (received < 0 || static_cast<size_t>(received) > output_size ||
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0) {
        close_received_descriptors(message);
        return false;
    }
    auto* cmsg = CMSG_FIRSTHDR(&message);
    if (!cmsg) {
        if (static_cast<size_t>(received) < output_size &&
            !recv_all(client_fd, static_cast<u8*>(output) + received,
                      output_size - static_cast<size_t>(received), deadline_ms))
            return false;
        return true;
    }
    if (cmsg->cmsg_level != SOL_SOCKET ||
        cmsg->cmsg_type != SCM_RIGHTS ||
        cmsg->cmsg_len < CMSG_LEN(sizeof(int)) ||
        CMSG_NXTHDR(&message, cmsg) != nullptr) {
        close_received_descriptors(message);
        return false;
    }
    size_t bytes = cmsg->cmsg_len - CMSG_LEN(0);
    if (bytes == 0u || bytes % sizeof(int) != 0u) {
        close_received_descriptors(message);
        return false;
    }
    descriptor_count = bytes / sizeof(int);
    if (descriptor_count > descriptor_capacity) {
        close_received_descriptors(message);
        descriptor_count = 0u;
        return false;
    }
    __builtin_memcpy(descriptors, CMSG_DATA(cmsg), bytes);
    for (size_t index = 0u; index < descriptor_count; ++index)
        if (descriptors[index] < 0) {
            close_received_descriptors(message);
            descriptor_count = 0u;
            return false;
        }
    if (static_cast<size_t>(received) < output_size &&
        !recv_all(client_fd, static_cast<u8*>(output) + received,
                  output_size - static_cast<size_t>(received), deadline_ms)) {
        for (size_t index = 0u; index < descriptor_count; ++index)
            (void)::close(descriptors[index]);
        descriptor_count = 0u;
        return false;
    }
    return true;
}

static int handle_complete_file_picker(PageSession& page, ReadonlyBytes payload,
                                       int descriptor)
{
    if (payload.size() != sizeof(RinWebContentFilePickerCompleteV1)) {
        if (descriptor >= 0)
            (void)::close(descriptor);
        return -EINVAL;
    }
    RinWebContentFilePickerCompleteV1 completion {};
    __builtin_memcpy(&completion, payload.data(), sizeof(completion));
    if (!rin_webcontent_client_file_picker_complete_valid(&completion) ||
        (completion.result == RIN_WEBCONTENT_FILE_PICKER_RESULT_OK
             ? descriptor < 0
             : descriptor >= 0)) {
        if (descriptor >= 0)
            (void)::close(descriptor);
        return -EINVAL;
    }
    if (!page.complete_file_picker(completion, descriptor)) {
        if (descriptor >= 0)
            (void)::close(descriptor);
        return -EINVAL;
    }
    return 0;
}

static int handle_complete_file_picker_multiple(
    PageSession& page, ReadonlyBytes payload, int* descriptors,
    size_t descriptor_count)
{
    if (payload.size() != sizeof(RinWebContentFilePickerCompleteV2) ||
        !rin_webcontent_client_file_picker_complete_multiple_valid(
            reinterpret_cast<const RinWebContentFilePickerCompleteV2*>(
                payload.data())) ||
        descriptor_count > RIN_WEBCONTENT_FILE_PICKER_MAX_SELECTIONS ||
        (descriptor_count == 0u &&
         reinterpret_cast<const RinWebContentFilePickerCompleteV2*>(
             payload.data())->result == RIN_WEBCONTENT_FILE_PICKER_RESULT_OK)) {
        for (size_t index = 0u; index < descriptor_count; ++index)
            if (descriptors[index] >= 0) (void)::close(descriptors[index]);
        return -EINVAL;
    }
    auto const& completion = *reinterpret_cast<
        const RinWebContentFilePickerCompleteV2*>(payload.data());
    if (!page.complete_file_picker_multiple(completion, descriptors,
                                            descriptor_count)) {
        for (size_t index = 0u; index < descriptor_count; ++index)
            if (descriptors[index] >= 0) (void)::close(descriptors[index]);
        return -EINVAL;
    }
    return 0;
}

static int handle_get_state(PageSession& page, int client_fd, u32 command, u64 deadline_ms)
{
    page.drain_pending_bridge_events(false);
    RinWebContentPageState state {};
    page.fill_state(state);
    auto sent = send_message(client_fd, command, 0, page.page_id, &state, sizeof(state), deadline_ms);
    page.dirty = false;
    return sent ? 1 : -EIO;
}

static int handle_get_download_event(PageSession& page, int client_fd,
                                     ReadonlyBytes payload, u64 deadline_ms)
{
    if (payload.size() != sizeof(RinWebContentDownloadEventRequestV1))
        return -EINVAL;
    RinWebContentDownloadEventRequestV1 request {};
    __builtin_memcpy(&request, payload.data(), sizeof(request));
    if (!rin_webcontent_client_download_event_request_valid(&request))
        return -EINVAL;
    RinWebContentDownloadEventV1 event {};
    page.fill_download_event(request.after_revision, event);
    if (!rin_webcontent_client_download_event_valid(&event))
        return -EPROTO;
    return send_message(client_fd, RIN_WEBCONTENT_CMD_GET_DOWNLOAD_EVENT_V1,
                        0, page.page_id, &event, sizeof(event), deadline_ms)
        ? 1 : -EIO;
}

static int handle_download_control(PageSession& page, ReadonlyBytes payload,
                                    bool retry)
{
    if (payload.size() != sizeof(RinWebContentDownloadControlV1))
        return -EINVAL;
    RinWebContentDownloadControlV1 request {};
    __builtin_memcpy(&request, payload.data(), sizeof(request));
    if (!rin_webcontent_client_download_control_valid(&request))
        return -EINVAL;
    bool accepted = retry ? page.retry_download(request.transfer_id)
                          : page.cancel_download(request.transfer_id);
    return accepted ? 0 : -ENOENT;
}

static int handle_get_accessibility_tree(PageSession& page, int client_fd,
                                          ReadonlyBytes payload,
                                          u64 deadline_ms)
{
    if (payload.size() != sizeof(RinWebContentAccessibilityRequestV1))
        return -EINVAL;
    RinWebContentAccessibilityRequestV1 request {};
    __builtin_memcpy(&request, payload.data(), sizeof(request));
    RinWebContentAccessibilitySnapshotV1 snapshot {};
    page.drain_pending_bridge_events(false);
    if (!build_accessibility_snapshot(page, request.window, snapshot))
        return send_message(client_fd,
                            RIN_WEBCONTENT_CMD_GET_ACCESSIBILITY_TREE_V1,
                            -EIO, page.page_id, nullptr, 0, deadline_ms)
            ? 1
            : -EIO;
    return send_message(client_fd,
                        RIN_WEBCONTENT_CMD_GET_ACCESSIBILITY_TREE_V1, 0,
                        page.page_id, &snapshot, sizeof(snapshot), deadline_ms)
        ? 1
        : -EIO;
}

static int handle_perform_accessibility_action(
    PageSession& page, ReadonlyBytes payload)
{
    if (payload.size() != sizeof(RinWebContentAccessibilityActionV1))
        return -EINVAL;

    RinWebContentAccessibilityActionV1 request {};
    __builtin_memcpy(&request, payload.data(), sizeof(request));
    if (!rin_webcontent_client_accessibility_action_valid(&request))
        return -EINVAL;

    page.drain_pending_bridge_events(false);
    RinWebContentAccessibilitySnapshotV1 snapshot {};
    if (!build_accessibility_snapshot(page, request.window, snapshot))
        return -EIO;
    if (snapshot.generation != request.generation)
        return -ESTALE;

    RinWebContentAccessibilityNodeV1 const* wire_node = nullptr;
    for (u32 index = 0u; index < snapshot.node_count; ++index) {
        if (snapshot.nodes[index].id == request.node_id) {
            wire_node = &snapshot.nodes[index];
            break;
        }
    }
    if (!wire_node ||
        (wire_node->actions & request.action) != request.action)
        return -EINVAL;

    if (request.action == RIN_WEBCONTENT_ACCESSIBILITY_ACTION_SCROLL_FORWARD ||
        request.action == RIN_WEBCONTENT_ACCESSIBILITY_ACTION_SCROLL_BACKWARD) {
        auto delta = max(page.reported_viewport_height, 1);
        auto target = request.action ==
                RIN_WEBCONTENT_ACCESSIBILITY_ACTION_SCROLL_FORWARD
            ? page.scroll_y + delta
            : page.scroll_y - delta;
        return page.scroll_to(page.scroll_x, target) ? 0 : -EIO;
    }

    auto* dom_node = Web::DOM::Node::from_unique_id(
        Web::UniqueNodeID(request.node_id));
    auto* element = dom_node ? as_if<Web::HTML::HTMLElement>(dom_node) : nullptr;
    if (!element)
        return -EINVAL;

    if (request.action == RIN_WEBCONTENT_ACCESSIBILITY_ACTION_FOCUS) {
        element->focus();
    } else if (request.action ==
               RIN_WEBCONTENT_ACCESSIBILITY_ACTION_ACTIVATE) {
        element->click();
    } else if (request.action ==
               RIN_WEBCONTENT_ACCESSIBILITY_ACTION_SET_VALUE) {
        auto value = Utf16String::from_utf8(StringView {
            reinterpret_cast<char const*>(request.value.bytes),
            request.value.size });
        if (auto* input = as_if<Web::HTML::HTMLInputElement>(dom_node)) {
            if (input->set_value(value).is_throw_completion())
                return -EINVAL;
        } else if (auto* textarea =
                       as_if<Web::HTML::HTMLTextAreaElement>(dom_node)) {
            textarea->set_value(value);
        } else if (auto* select =
                       as_if<Web::HTML::HTMLSelectElement>(dom_node)) {
            if (select->set_value(value).is_throw_completion())
                return -EINVAL;
        } else {
            return -EINVAL;
        }
    } else if (request.action ==
               RIN_WEBCONTENT_ACCESSIBILITY_ACTION_INCREMENT ||
               request.action ==
                   RIN_WEBCONTENT_ACCESSIBILITY_ACTION_DECREMENT) {
        auto* input = as_if<Web::HTML::HTMLInputElement>(dom_node);
        if (!input)
            return -EINVAL;
        auto result = request.action ==
                RIN_WEBCONTENT_ACCESSIBILITY_ACTION_INCREMENT
            ? input->step_up()
            : input->step_down();
        if (result.is_throw_completion())
            return -EINVAL;
    } else {
        return -EINVAL;
    }

    page.metrics_dirty = true;
    page.mark_dirty();
    return 0;
}

static int handle_paint(PageSession& page, int client_fd, u64 deadline_ms)
{
    page.drain_pending_bridge_events(true);
    page.refresh_metrics();
    auto bitmap = page.view->visible_bitmap();
    auto size = page.view->visible_bitmap_size();
    if (!bitmap || size.width() <= 0 || size.height() <= 0) {
        if (!page.logged_missing_visible_bitmap) {
            auto message = ByteString::formatted(
                "[webcontent] page {} paint requested without visible bitmap revision={} first_frame_pending={}\n",
                page.page_id,
                page.paint_revision,
                page.first_frame_pending ? 1 : 0);
            rin_log(message.characters());
            page.logged_missing_visible_bitmap = true;
        }
        return -EIO;
    }

    page.logged_missing_visible_bitmap = false;

    auto row_bytes = static_cast<size_t>(size.width()) * sizeof(u32);
    auto total_bytes = row_bytes * static_cast<size_t>(size.height());
    if (!page.ensure_paint_shm(total_bytes))
        return -EIO;

    auto* dst = static_cast<u8*>(page.paint_shm_addr);
    for (int y = 0; y < size.height(); ++y)
        __builtin_memcpy(dst + static_cast<size_t>(y) * row_bytes, bitmap->scanline_u8(y), row_bytes);

    RinWebContentPaintResponse response {};
    response.width = static_cast<u32>(size.width());
    response.height = static_cast<u32>(size.height());
    response.pixel_storage_kind = RIN_WEBCONTENT_STORAGE_SHM;
    response.paint_revision = page.paint_revision;
    response.pixel_region.size = static_cast<u32>(total_bytes);
    copy_c_string(response.pixel_region.name, sizeof(response.pixel_region.name), page.paint_shm_name);

    return send_message(client_fd, RIN_WEBCONTENT_CMD_PAINT_V1, 0, page.page_id, &response, sizeof(response), deadline_ms)
        ? 1
        : -EIO;
}

static void handle_client(int client_fd)
{
    RinWebContentMsgHeader header {};
    u64 deadline_ms = 0;
    if (!client_rpc_deadline_create(&deadline_ms) ||
        !recv_all(client_fd, &header, sizeof(header), deadline_ms))
        return;
    if (!rin_webcontent_service_request_payload_length_valid(&header))
        return;

    Vector<u8> payload;
    int received_descriptor = -1;
    int received_descriptors[RIN_WEBCONTENT_FILE_PICKER_MAX_SELECTIONS] {};
    size_t received_descriptor_count = 0u;
    if (header.payload_len > 0) {
        payload.resize(header.payload_len);
        if (header.command == RIN_WEBCONTENT_CMD_COMPLETE_FILE_PICKER_V1) {
            if (!receive_file_picker_payload(client_fd, payload.data(),
                                              payload.size(), received_descriptor,
                                              deadline_ms))
                return;
        } else if (header.command == RIN_WEBCONTENT_CMD_COMPLETE_FILE_PICKER_V2) {
            if (!receive_file_picker_payload_multiple(
                    client_fd, payload.data(), payload.size(),
                    received_descriptors,
                    RIN_WEBCONTENT_FILE_PICKER_MAX_SELECTIONS,
                    received_descriptor_count, deadline_ms))
                return;
        } else if (!recv_all(client_fd, payload.data(), payload.size(), deadline_ms)) {
            return;
        }
    }
    if (!rin_webcontent_service_request_valid(
            &header, payload.is_empty() ? nullptr : payload.data())) {
        if (received_descriptor >= 0)
            (void)::close(received_descriptor);
        for (size_t index = 0u; index < received_descriptor_count; ++index)
            if (received_descriptors[index] >= 0)
                (void)::close(received_descriptors[index]);
        return;
    }
    ReadonlyBytes payload_bytes { payload.data(), payload.size() };

    if (header.command == RIN_WEBCONTENT_CMD_GET_CAPABILITIES_V1) {
        RinWebContentCapabilitiesV1 capabilities {};
        if (!rin_webcontent_service_capabilities_build(&header, &capabilities)) {
            (void)send_message(client_fd, header.command, -EINVAL, header.page_id, nullptr, 0, deadline_ms);
            return;
        }
        (void)send_message(client_fd, header.command, 0, 0, &capabilities, sizeof(capabilities), deadline_ms);
        return;
    }

    if (header.command == RIN_WEBCONTENT_CMD_CREATE_PAGE_V1) {
        auto rc = handle_create_page(header.page_id, payload_bytes);
        (void)send_message(client_fd, header.command, rc, header.page_id, nullptr, 0, deadline_ms);
        return;
    }

    if (header.command == RIN_WEBCONTENT_CMD_DESTROY_PAGE_V1) {
        destroy_page(header.page_id);
        (void)send_message(client_fd, header.command, 0, header.page_id, nullptr, 0, deadline_ms);
        return;
    }

    auto* page = find_page(header.page_id);
    if (!page) {
        (void)send_message(client_fd, header.command, -ENOENT, header.page_id, nullptr, 0, deadline_ms);
        return;
    }

    switch (header.command) {
    case RIN_WEBCONTENT_CMD_NAVIGATE_V1:
        (void)send_message(client_fd, header.command, handle_navigate(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_LOAD_MARKUP_V1:
        (void)send_message(client_fd, header.command, handle_load_markup(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_RESIZE_V1:
        (void)send_message(client_fd, header.command, handle_resize(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_PUMP_EVENTS_V1:
        (void)handle_get_state(*page, client_fd, header.command, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_PAINT_V1:
        (void)handle_paint(*page, client_fd, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_DISPATCH_POINTER_V1:
        (void)send_message(client_fd, header.command, handle_pointer(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_DISPATCH_KEY_OR_TEXT_V1:
        (void)send_message(client_fd, header.command, handle_key_or_text(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_SCROLL_TO_V1:
        (void)send_message(client_fd, header.command, handle_scroll(*page, payload_bytes), header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_GET_PAGE_STATE_V1:
        (void)handle_get_state(*page, client_fd, header.command, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_GET_DOWNLOAD_EVENT_V1:
        (void)handle_get_download_event(*page, client_fd, payload_bytes,
                                        deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_CANCEL_DOWNLOAD_V1:
        (void)send_message(client_fd, header.command,
                           handle_download_control(*page, payload_bytes, false),
                           header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_RETRY_DOWNLOAD_V1:
        (void)send_message(client_fd, header.command,
                           handle_download_control(*page, payload_bytes, true),
                           header.page_id, nullptr, 0, deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_GET_ACCESSIBILITY_TREE_V1:
        (void)handle_get_accessibility_tree(*page, client_fd, payload_bytes,
                                            deadline_ms);
        return;
    case RIN_WEBCONTENT_CMD_PERFORM_ACCESSIBILITY_ACTION_V1: {
        auto rc = handle_perform_accessibility_action(*page, payload_bytes);
        (void)send_message(client_fd, header.command, rc, header.page_id,
                           nullptr, 0, deadline_ms);
        return;
    }
    case RIN_WEBCONTENT_CMD_COMPLETE_FILE_PICKER_V1: {
        auto rc = handle_complete_file_picker(*page, payload_bytes,
                                              received_descriptor);
        (void)send_message(client_fd, header.command, rc, header.page_id,
                           nullptr, 0, deadline_ms);
        return;
    }
    case RIN_WEBCONTENT_CMD_COMPLETE_FILE_PICKER_V2: {
        auto rc = handle_complete_file_picker_multiple(
            *page, payload_bytes, received_descriptors, received_descriptor_count);
        (void)send_message(client_fd, header.command, rc, header.page_id,
                           nullptr, 0, deadline_ms);
        return;
    }
    default:
        (void)send_message(client_fd, header.command, -EINVAL, header.page_id, nullptr, 0, deadline_ms);
        return;
    }
}

static bool client_peer_authenticated(int client_fd)
{
    rin_unix_peer_app_identity_v1 identity {};
    socklen_t identity_size = sizeof(identity);

    if (client_fd < 0) {
        errno = EINVAL;
        return false;
    }
    if (::getsockopt(client_fd, SOL_SOCKET, SO_RIN_UNIX_PEER_APP_IDENTITY,
            &identity, &identity_size) != 0) {
        return false;
    }
    if (identity_size != sizeof(identity) ||
        !rin_webcontent_authenticated_client_peer_valid(&identity)) {
        errno = EPROTO;
        return false;
    }
    return true;
}

static bool ensure_system_runtime_directory(char const* path)
{
    struct stat status {};

    if (::mkdir(path, 0755) < 0 && errno != EEXIST)
        return false;
    if (::stat(path, &status) < 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != 0 ||
        (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        errno = EPERM;
        return false;
    }
    return true;
}

static ErrorOr<int> run_bridge()
{
    char app_name[] = "webcontent-bridge";
    char* argv_values[] = { app_name, nullptr };
    auto argument_strings = Array<StringView, 1> { "webcontent-bridge"sv };
    Main::Arguments arguments {
        .argc = 1,
        .argv = argv_values,
        .strings = argument_strings.span(),
    };

    if (!ensure_system_runtime_directory("/run") ||
        !ensure_system_runtime_directory("/run/rin"))
        return Error::from_errno(errno);
    unlink(RIN_WEBCONTENT_SOCKET_PATH);

    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
        return Error::from_errno(errno);
    if (!socket_set_nonblocking(server_fd)) {
        auto socket_error = Error::from_errno(errno);
        ::close(server_fd);
        return socket_error;
    }

    auto cleanup_server_socket = [&] {
        s_server_notifier = nullptr;
        if (server_fd >= 0) {
            ::close(server_fd);
            server_fd = -1;
        }
        unlink(RIN_WEBCONTENT_SOCKET_PATH);
    };

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    copy_c_string(addr.sun_path, sizeof(addr.sun_path), RIN_WEBCONTENT_SOCKET_PATH);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        auto bind_error = Error::from_errno(errno);
        cleanup_server_socket();
        return bind_error;
    }

    {
        int one = 1;
        if (::setsockopt(server_fd, SOL_SOCKET, SO_RIN_UNIX_PUBLISH_SERVICE, &one, sizeof(one)) < 0) {
            auto publish_error = Error::from_errno(errno);
            cleanup_server_socket();
            return publish_error;
        }
    }

    if (::listen(server_fd, 16) < 0) {
        auto listen_error = Error::from_errno(errno);
        cleanup_server_socket();
        return listen_error;
    }

    // RinOS does not provide /proc/self/exe, so pin helper/resource discovery
    // to the packaged bridge bundle directory.
    auto bundle_path = ByteString("/sys/apps/webcontent"sv);
    auto app_or_error = BridgeApplication::create(arguments, Optional<ByteString> { bundle_path });
    if (app_or_error.is_error()) {
        auto message = ByteString::formatted("[webcontent] BridgeApplication::create failed (binary_path={}, resource_root={}): {}\n",
            bundle_path, WebView::s_ladybird_resource_root, app_or_error.error());
        rin_log(message.characters());
        auto error = app_or_error.release_error();
        cleanup_server_socket();
        return error;
    }
    s_app = app_or_error.release_value();

    auto theme_path = LexicalPath::join(WebView::s_ladybird_resource_root, "themes"sv, "Default.ini"sv);
    {
        auto message = ByteString::formatted("[webcontent] Using Ladybird resource root: {}\n", WebView::s_ladybird_resource_root);
        rin_log(message.characters());
    }
    {
        auto message = ByteString::formatted("[webcontent] Loading theme from: {}\n", theme_path.string());
        rin_log(message.characters());
    }
    auto theme_or_error = Gfx::load_system_theme(theme_path.string());
    if (theme_or_error.is_error()) {
        auto message = ByteString::formatted("[webcontent] load_system_theme failed (resource_root={}, theme_path={}): {}\n",
            WebView::s_ladybird_resource_root, theme_path.string(), theme_or_error.error());
        rin_log(message.characters());
        auto fallback_or_error = build_embedded_fallback_theme();
        if (fallback_or_error.is_error()) {
            auto fallback_message = ByteString::formatted("[webcontent] embedded fallback theme failed: {}\n",
                fallback_or_error.error());
            rin_log(fallback_message.characters());
            auto error = fallback_or_error.release_error();
            cleanup_server_socket();
            return error;
        }
        rin_log("[webcontent] Continuing with embedded fallback theme\n");
        s_theme = fallback_or_error.release_value();
    } else {
        s_theme = theme_or_error.release_value();
    }

    s_server_notifier = Core::Notifier::construct(server_fd, Core::Notifier::Type::Read);
    s_server_notifier->on_activation = [server_fd] {
        for (;;) {
            int client_fd = ::accept(server_fd, nullptr, nullptr);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    return;
                if (socket_should_retry())
                    continue;
                return;
            }

            if (!socket_set_nonblocking(client_fd)) {
                ::close(client_fd);
                return;
            }
            if (!client_peer_authenticated(client_fd)) {
                ::close(client_fd);
                return;
            }
            handle_client(client_fd);
            ::close(client_fd);
            return;
        }
    };

    s_stop_timer = Core::Timer::create_repeating(50, [server_fd] {
        if (!rin_service_should_stop())
            return;

        s_pages.clear();
        s_server_notifier = nullptr;
        s_stop_timer = nullptr;
        unlink(RIN_WEBCONTENT_SOCKET_PATH);
        ::close(server_fd);
        Core::EventLoop::current().quit(0);
    });
    s_stop_timer->start();

    rin_log("[webcontent] Ladybird bridge ready\n");
    auto result = s_app->execute();
    if (result.is_error()) {
        cleanup_server_socket();
        s_stop_timer = nullptr;
        return result;
    }
    return result;
}

} // namespace

extern "C" int webcontent_run(void)
{
    auto result = run_bridge();
    if (result.is_error()) {
        auto message = ByteString::formatted("[webcontent] Ladybird bridge failed: {}\n", result.error());
        rin_log(message.characters());
        return 1;
    }
    return result.release_value();
}
