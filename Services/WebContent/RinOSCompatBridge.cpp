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
#include <AK/Vector.h>
#include <LibCore/AnonymousBuffer.h>
#include <LibCore/EventLoop.h>
#include <LibCore/Notifier.h>
#include <LibCore/Promise.h>
#include <LibCore/Timer.h>
#include <LibGfx/Bitmap.h>
#include <LibGfx/SystemTheme.h>
#include <LibMain/Main.h>
#include <LibURL/Parser.h>
#include <LibWeb/Page/InputEvent.h>
#include <LibWeb/UIEvents/KeyCode.h>
#include <LibWeb/UIEvents/MouseButton.h>
#include <LibWebView/Application.h>
#include <LibWebView/HeadlessWebView.h>
#include <LibWebView/Utilities.h>

#include "webcontent_service_abi.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
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
int fs_mkdir(char const* path);
}

class BridgeApplication;
struct PageSession;
static PageSession* find_page(u32 page_id);

static constexpr unsigned long s_load_start_retry_interval_ms = 250;
static constexpr unsigned long s_load_start_retry_budget_ms = 2000;
static constexpr u32 s_load_start_retry_limit = 8;

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
            url: String(location.href || "about:blank")
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

static bool send_all(int fd, void const* data, size_t len)
{
    auto const* bytes = reinterpret_cast<u8 const*>(data);
    size_t offset = 0;
    while (offset < len) {
        auto rc = ::send(fd, bytes + offset, len - offset, 0);
        if (rc < 0) {
            if (socket_should_retry())
                continue;
            return false;
        }
        if (rc == 0)
            return false;
        offset += static_cast<size_t>(rc);
    }
    return true;
}

static bool recv_all(int fd, void* data, size_t len)
{
    auto* bytes = reinterpret_cast<u8*>(data);
    size_t offset = 0;
    while (offset < len) {
        auto rc = ::recv(fd, bytes + offset, len - offset, 0);
        if (rc < 0) {
            if (socket_should_retry())
                continue;
            return false;
        }
        if (rc == 0)
            return false;
        offset += static_cast<size_t>(rc);
    }
    return true;
}

static bool send_message(int fd, u32 command, i32 status, u32 page_id, void const* payload, u32 payload_len)
{
    RinWebContentMsgHeader header {};
    header.magic = RIN_WEBCONTENT_MAGIC;
    header.version = RIN_WEBCONTENT_VERSION;
    header.command = command;
    header.status = status;
    header.page_id = page_id;
    header.payload_len = payload_len;

    return send_all(fd, &header, sizeof(header))
        && (payload_len == 0 || send_all(fd, payload, payload_len));
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
        browser_options.headless_mode = WebView::HeadlessMode::Manual;
        browser_options.skip_implicit_headless_bootstrap_view = WebView::SkipImplicitHeadlessBootstrapView::Yes;
        browser_options.disable_sql_database = WebView::DisableSQLDatabase::Yes;
        browser_options.disable_spare_web_content_processes = WebView::DisableSpareWebContentProcesses::Yes;
        browser_options.allow_popups = WebView::AllowPopups::Yes;

        // RinOS bridge-mode startup is currently more reliable without RequestServer disk cache setup.
        request_server_options.http_disk_cache_mode = WebView::HTTPDiskCacheMode::Disabled;

        web_content_options.force_cpu_painting = WebView::ForceCPUPainting::Yes;
        web_content_options.force_fontconfig = WebView::ForceFontconfig::Yes;
        web_content_options.paint_viewport_scrollbars = WebView::PaintViewportScrollbars::No;
        web_content_options.disable_site_isolation = WebView::DisableSiteIsolation::Yes;

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

struct PageSession {
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
            waiting_for_first_paint_after_load_finish = false;
            metrics_dirty = true;
            note_pending_load_started();
            kick_first_frame_if_needed("load-start"sv, true);
            auto message = ByteString::formatted("[webcontent] page {} load start {}\n", page_id, serialized);
            rin_log(message.characters());
            mark_dirty();
        };

        view->on_load_finish = [this](URL::URL const& url) {
            auto serialized = remap_markup_internal_url(url.serialize().to_byte_string());
            committed_url = serialized;
            metrics_dirty = true;
            refresh_metrics(true);
            if (has_first_paint_for_active_navigation()) {
                loading = false;
                progress_percent = 100;
                pending_url = {};
                waiting_for_first_paint_after_load_finish = false;
            } else {
                loading = true;
                if (progress_percent < 95)
                    progress_percent = 95;
                pending_url = serialized;
                waiting_for_first_paint_after_load_finish = true;
                kick_first_frame_if_needed("load-finish-before-first-paint"sv, false);
            }
            auto message = ByteString::formatted("[webcontent] page {} load finish {}\n", page_id, serialized);
            rin_log(message.characters());
            mark_dirty();
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

        view->on_resource_status_change = [this](i32 count_waiting) {
            if (!loading)
                return;
            progress_percent = count_waiting > 0 ? 60 : 90;
            mark_dirty();
        };

        view->on_ready_to_paint = [this] {
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
            if (first_paint_for_active_navigation && waiting_for_first_paint_after_load_finish) {
                loading = false;
                progress_percent = 100;
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
            if (pending_url.is_empty() && !committed_url.is_empty())
                pending_url = committed_url;
            waiting_for_first_paint_after_load_finish = false;
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
        pending_load_kind = PendingLoadKind::None;
        pending_load_target_url = {};
        pending_load_markup = {};
        pending_load_requested_ms = 0;
        pending_load_last_dispatch_ms = 0;
        pending_load_retry_count = 0;
        pending_load_started = false;
        pending_load_expired = false;
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
        pending_load_requested_ms = rin_time();
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

        pending_load_last_dispatch_ms = rin_time();
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

        auto now = rin_time();
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
        Core::deferred_invoke([scheduled_page_id] {
            auto* page = find_page(scheduled_page_id);
            if (!page)
                return;
            page->drain_pending_bridge_events(false, false);
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
            first_frame_started_ms = rin_time();
            first_frame_kick_count = 0;
            logged_missing_visible_bitmap = false;
            navigation_paint_revision_baseline = paint_revision;
        }

        ++first_frame_kick_count;
        last_first_frame_kick_ms = rin_time();
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
        auto now_ms = rin_time();
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

        metrics_dirty = false;
    }

    bool navigate(ByteString const& url)
    {
        crashed = false;
        crash_reason = {};
        builtin_shell_url = {};
        loading = false;
        progress_percent = 0;
        pending_url = {};
        waiting_for_first_paint_after_load_finish = false;
        metrics_dirty = true;
        prime_pending_load_request(PendingLoadKind::Navigate, url);
        if (!dispatch_pending_load_request(false)) {
            clear_pending_load_request();
            return false;
        }
        mark_dirty();
        schedule_load_dispatch_observation();
        return true;
    }

    bool load_markup(ByteString const& base_url, ByteString const& markup)
    {
        crashed = false;
        crash_reason = {};
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

        if (request.text[0] != '\0') {
            auto text = ByteString { request.text };
            for (auto ch : text.bytes()) {
                Web::KeyEvent event {};
                if (!map_ascii_key(ch, event))
                    continue;
                send_key(event.key, event.code_point, event.modifiers);
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
    }

    u32 page_id { 0 };
    int requested_viewport_width { 800 };
    int requested_viewport_height { 600 };
    int reported_viewport_width { 800 };
    int reported_viewport_height { 600 };
    NonnullOwnPtr<BridgeView> view;

    bool loading { false };
    bool crashed { false };
    bool dirty { true };
    bool metrics_dirty { true };
    int progress_percent { 0 };
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
    unsigned long first_frame_started_ms { 0 };
    u32 first_frame_kick_count { 0 };
    unsigned long last_first_frame_kick_ms { 0 };
    bool logged_missing_visible_bitmap { false };
    bool waiting_for_first_paint_after_load_finish { false };
    PendingLoadKind pending_load_kind { PendingLoadKind::None };
    ByteString pending_load_target_url;
    ByteString pending_load_markup;
    unsigned long pending_load_requested_ms { 0 };
    unsigned long pending_load_last_dispatch_ms { 0 };
    u32 pending_load_retry_count { 0 };
    bool pending_load_started { false };
    bool pending_load_expired { false };

    int paint_shm_handle { -1 };
    void* paint_shm_addr { nullptr };
    size_t paint_shm_size { 0 };
    char paint_shm_name[RIN_SHM_NAME_MAX] {};
};

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

static int handle_get_state(PageSession& page, int client_fd, u32 command)
{
    page.drain_pending_bridge_events(false);
    RinWebContentPageState state {};
    page.fill_state(state);
    auto sent = send_message(client_fd, command, 0, page.page_id, &state, sizeof(state));
    page.dirty = false;
    return sent ? 1 : -EIO;
}

static int handle_paint(PageSession& page, int client_fd)
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

    return send_message(client_fd, RIN_WEBCONTENT_CMD_PAINT_V1, 0, page.page_id, &response, sizeof(response))
        ? 1
        : -EIO;
}

static void handle_client(int client_fd)
{
    RinWebContentMsgHeader header {};
    if (!recv_all(client_fd, &header, sizeof(header)))
        return;
    if (header.magic != RIN_WEBCONTENT_MAGIC || header.version != RIN_WEBCONTENT_VERSION)
        return;

    Vector<u8> payload;
    if (header.payload_len > 0) {
        payload.resize(header.payload_len);
        if (!recv_all(client_fd, payload.data(), payload.size()))
            return;
    }
    ReadonlyBytes payload_bytes { payload.data(), payload.size() };

    if (header.command == RIN_WEBCONTENT_CMD_CREATE_PAGE_V1) {
        auto rc = handle_create_page(header.page_id, payload_bytes);
        (void)send_message(client_fd, header.command, rc, header.page_id, nullptr, 0);
        return;
    }

    if (header.command == RIN_WEBCONTENT_CMD_DESTROY_PAGE_V1) {
        destroy_page(header.page_id);
        (void)send_message(client_fd, header.command, 0, header.page_id, nullptr, 0);
        return;
    }

    auto* page = find_page(header.page_id);
    if (!page) {
        (void)send_message(client_fd, header.command, -ENOENT, header.page_id, nullptr, 0);
        return;
    }

    switch (header.command) {
    case RIN_WEBCONTENT_CMD_NAVIGATE_V1:
        (void)send_message(client_fd, header.command, handle_navigate(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_LOAD_MARKUP_V1:
        (void)send_message(client_fd, header.command, handle_load_markup(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_RESIZE_V1:
        (void)send_message(client_fd, header.command, handle_resize(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_PUMP_EVENTS_V1:
        (void)handle_get_state(*page, client_fd, header.command);
        return;
    case RIN_WEBCONTENT_CMD_PAINT_V1:
        (void)handle_paint(*page, client_fd);
        return;
    case RIN_WEBCONTENT_CMD_DISPATCH_POINTER_V1:
        (void)send_message(client_fd, header.command, handle_pointer(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_DISPATCH_KEY_OR_TEXT_V1:
        (void)send_message(client_fd, header.command, handle_key_or_text(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_SCROLL_TO_V1:
        (void)send_message(client_fd, header.command, handle_scroll(*page, payload_bytes), header.page_id, nullptr, 0);
        return;
    case RIN_WEBCONTENT_CMD_GET_PAGE_STATE_V1:
        (void)handle_get_state(*page, client_fd, header.command);
        return;
    default:
        (void)send_message(client_fd, header.command, -EINVAL, header.page_id, nullptr, 0);
        return;
    }
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

    fs_mkdir("/tmp");
    unlink(RIN_WEBCONTENT_SOCKET_PATH);

    int server_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
        return Error::from_errno(errno);

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

#ifdef SO_RIN_UNIX_PUBLISH_SERVICE
    {
        int one = 1;
        (void)::setsockopt(server_fd, SOL_SOCKET, SO_RIN_UNIX_PUBLISH_SERVICE, &one, sizeof(one));
    }
#endif

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
