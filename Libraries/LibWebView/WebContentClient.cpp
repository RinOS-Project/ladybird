/*
 * Copyright (c) 2020-2021, Andreas Kling <andreas@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibHTTP/Cookie/ParsedCookie.h>
#include <LibIPC/TransportHandle.h>
#include <LibURL/Parser.h>
#include <LibURL/Site.h>
#include <LibWebView/Application.h>
#include <LibWebView/CookieJar.h>
#include <LibWebView/HelperProcess.h>
#include <LibWebView/SourceHighlighter.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <LibWebView/WebUI.h>
#include <AK/StringBuilder.h>
#include <rin/web/webcontent_protocol.h>

#include <cstring>

namespace WebView {

struct CacheStorageOwnerRecord {
    String key;
    String value;
};

static constexpr StringView cache_storage_marker = "RIN-CACHE-NAME-V1"sv;
static constexpr StringView cache_storage_name_key_prefix =
    "RIN-CACHE-NAME-KEY-V1:"sv;

static u16 cache_storage_read_u16(u8 const* bytes)
{
    return static_cast<u16>(bytes[0]) |
        (static_cast<u16>(bytes[1]) << 8u);
}

static u32 cache_storage_read_u32(u8 const* bytes)
{
    return static_cast<u32>(bytes[0]) |
        (static_cast<u32>(bytes[1]) << 8u) |
        (static_cast<u32>(bytes[2]) << 16u) |
        (static_cast<u32>(bytes[3]) << 24u);
}

static u32 cache_storage_snapshot_crc32(u8 const* bytes, size_t size)
{
    u32 crc = 0xffffffffu;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xedb88320u & static_cast<u32>(-static_cast<i32>(crc & 1u)));
    }
    return ~crc;
}

static ByteString cache_storage_name_owner_key(String const& cache_name)
{
    static constexpr char hex_digits[] = "0123456789abcdef";
    StringBuilder builder;
    builder.append(cache_storage_name_key_prefix);
    for (u8 byte : cache_name.bytes()) {
        builder.append(hex_digits[byte >> 4u]);
        builder.append(hex_digits[byte & 0x0fu]);
    }
    return builder.to_byte_string();
}

static bool parse_cache_storage_owner_snapshot(
    ByteString const& snapshot, Vector<CacheStorageOwnerRecord>& records,
    bool& owner_initialized)
{
    records.clear();
    owner_initialized = false;
    if (snapshot.length() < 20u ||
        snapshot.length() > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_SNAPSHOT_BYTES ||
        std::memcmp(snapshot.characters(), "RCS1", 4u) != 0)
        return false;

    auto const* bytes = reinterpret_cast<u8 const*>(snapshot.characters());
    const u16 version = cache_storage_read_u16(bytes + 4u);
    const u16 flags = cache_storage_read_u16(bytes + 6u);
    const u32 record_count = cache_storage_read_u32(bytes + 8u);
    const u32 body_size = cache_storage_read_u32(bytes + 12u);
    const u32 expected_crc = cache_storage_read_u32(bytes + 16u);
    if (version != 1u || (flags & ~1u) != 0u ||
        record_count > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_RECORDS ||
        body_size != snapshot.length() - 20u ||
        cache_storage_snapshot_crc32(bytes + 20u, body_size) != expected_crc)
        return false;

    size_t offset = 20u;
    for (u32 index = 0; index < record_count; ++index) {
        if (snapshot.length() - offset < 8u)
            return false;
        const u32 key_size = cache_storage_read_u32(bytes + offset);
        const u32 value_size = cache_storage_read_u32(bytes + offset + 4u);
        offset += 8u;
        if (key_size == 0u ||
            key_size > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_KEY_BYTES ||
            value_size == 0u ||
            value_size > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_VALUE_BYTES ||
            key_size > snapshot.length() - offset ||
            value_size > snapshot.length() - offset - key_size)
            return false;
        for (size_t byte_index = 0; byte_index <
                static_cast<size_t>(key_size) + value_size; ++byte_index) {
            if (bytes[offset + byte_index] == 0u)
                return false;
        }
        auto key = String::from_utf8(StringView {
            snapshot.characters() + offset, key_size });
        auto value = String::from_utf8(StringView {
            snapshot.characters() + offset + key_size, value_size });
        if (key.is_error() || value.is_error())
            return false;
        auto parsed_key = key.release_value();
        for (auto const& existing : records)
            if (existing.key == parsed_key)
                return false;
        records.append({ move(parsed_key), value.release_value() });
        offset += static_cast<size_t>(key_size) + value_size;
    }
    if (offset != snapshot.length())
        return false;
    owner_initialized = (flags & 1u) != 0u;
    return true;
}

bool WebContentClient::storage_owner_is_authorized(u64 page_id,
                                                    Web::StorageAPI::StorageEndpointType endpoint,
                                                    String const& storage_key, u64 owner_generation)
{
    auto view = view_for_page_id(page_id);
    if (!view.has_value())
        return false;
    if (endpoint != Web::StorageAPI::StorageEndpointType::Caches)
        return true;
    if (owner_generation == 0 || !view->on_service_worker_owner_request)
        return false;

    auto response = view->on_service_worker_owner_request(
        4u, {}, storage_key.to_byte_string(), {}, {}, 0u);
    return response.accepted && response.found &&
        response.generation == owner_generation &&
        response.origin == storage_key.to_byte_string();
}

HashTable<WebContentClient*> WebContentClient::s_clients;

WebContentClient::WebContentClient(NonnullOwnPtr<IPC::Transport> transport, ViewImplementation& view)
    : IPC::ConnectionToServer<WebContentClientEndpoint, WebContentServerEndpoint>(*this, move(transport))
{
    s_clients.set(this);
    m_views.set(0, view);
}

WebContentClient::WebContentClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionToServer<WebContentClientEndpoint, WebContentServerEndpoint>(*this, move(transport))
{
    s_clients.set(this);
}

WebContentClient::~WebContentClient()
{
    s_clients.remove(this);
}

void WebContentClient::die()
{
    // Intentionally empty. Restart is handled at another level.
}

void WebContentClient::assign_view(Badge<Application>, ViewImplementation& view)
{
    VERIFY(m_views.is_empty());
    m_views.set(0, view);
}

void WebContentClient::register_view(u64 page_id, ViewImplementation& view)
{
    VERIFY(page_id > 0);
    m_views.set(page_id, view);
}

void WebContentClient::unregister_view(u64 page_id)
{
    m_views.remove(page_id);
    if (m_views.is_empty())
        async_close_server();
}

void WebContentClient::web_ui_disconnected(Badge<WebUI>)
{
    m_web_ui.clear();
}

void WebContentClient::notify_all_views_of_crash()
{
    // Collect view IDs first, then use deferred_invoke to handle crashes safely
    // (avoids signal handler deadlock and allows views to be looked up by ID
    // in case they're destroyed before the deferred_invoke runs).
    Vector<u64> view_ids;
    view_ids.ensure_capacity(m_views.size());
    for (auto& [page_id, view] : m_views)
        view_ids.unchecked_append(view->view_id());

    for (auto view_id : view_ids) {
        Core::deferred_invoke([view_id] {
            auto view = ViewImplementation::find_view_by_id(view_id);
            if (!view.has_value())
                return;
            view->cache_storage_synchronized_origin = {};
            view->cache_storage_synchronized_generation = 0;
            view->handle_web_content_process_crash();
            if (view->on_web_content_crashed)
                view->on_web_content_crashed();
        });
    }
}

void WebContentClient::did_paint(u64 page_id, Gfx::IntRect rect, i32 bitmap_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->server_did_paint({}, bitmap_id, rect.size());
}

void WebContentClient::did_request_new_process_for_navigation(u64 page_id, URL::URL url)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->create_new_process_for_cross_site_navigation(url);
}

void WebContentClient::did_start_loading(u64 page_id, URL::URL url, bool is_redirect)
{
    if (auto process = WebView::Application::the().find_process(m_process_handle.pid); process.has_value())
        process->set_title(OptionalNone {});

    if (auto view = view_for_page_id(page_id); view.has_value()) {
        view->set_url({}, url);

        if (view->on_load_start)
            view->on_load_start(url, is_redirect);

        for (auto const& [id, listener] : view->m_navigation_listeners) {
            if (listener.on_load_start)
                listener.on_load_start(url, is_redirect);
        }
    }
}

void WebContentClient::did_finish_loading(u64 page_id, URL::URL url)
{
    if (url.scheme() == "about"sv && url.paths().size() == 1) {
        if (auto web_ui = WebUI::create(*this, url.paths().first()); web_ui.is_error())
            warnln("Could not create WebUI for {}: {}", url, web_ui.error());
        else
            m_web_ui = web_ui.release_value();
    }

    if (auto view = view_for_page_id(page_id); view.has_value()) {
        view->set_url({}, url);

        if (view->on_load_finish)
            view->on_load_finish(url);

        for (auto const& [id, listener] : view->m_navigation_listeners) {
            if (listener.on_load_finish)
                listener.on_load_finish(url);
        }
    }
}

void WebContentClient::did_finish_test(u64 page_id, String text)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_test_finish)
            view->on_test_finish(text);
    }
}

void WebContentClient::did_set_test_timeout(u64 page_id, double milliseconds)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_set_test_timeout)
            view->on_set_test_timeout(milliseconds);
    }
}

void WebContentClient::did_receive_reference_test_metadata(u64 page_id, JsonValue metadata)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_reference_test_metadata)
            view->on_reference_test_metadata(metadata);
    }
}

void WebContentClient::did_receive_test_variant_metadata(u64 page_id, JsonValue metadata)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_test_variant_metadata)
            view->on_test_variant_metadata(metadata);
    }
}

void WebContentClient::did_set_browser_zoom(u64 page_id, double factor)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->set_zoom(factor);
}

void WebContentClient::did_find_in_page(u64 page_id, size_t current_match_index, Optional<size_t> total_match_count)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_find_in_page)
            view->on_find_in_page(current_match_index, total_match_count);
    }
}

void WebContentClient::did_request_refresh(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->reload();
}

void WebContentClient::did_request_cursor_change(u64 page_id, Gfx::Cursor cursor)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_cursor_change)
            view->on_cursor_change(cursor);
    }
}

void WebContentClient::did_change_title(u64 page_id, Utf16String title)
{
    if (auto process = WebView::Application::the().find_process(m_process_handle.pid); process.has_value())
        process->set_title(title);

    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (title.is_empty())
            title = Utf16String::from_utf8(view->url().serialize());

        view->set_title({}, title);

        if (view->on_title_change)
            view->on_title_change(title);
    }
}

void WebContentClient::did_change_url(u64 page_id, URL::URL url)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        view->set_url({}, url);

        if (view->on_url_change)
            view->on_url_change(url);
    }
}

void WebContentClient::did_request_tooltip_override(u64 page_id, Gfx::IntPoint position, ByteString title)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_tooltip_override)
            view->on_request_tooltip_override(view->to_widget_position(position), title);
    }
}

void WebContentClient::did_stop_tooltip_override(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_stop_tooltip_override)
            view->on_stop_tooltip_override();
    }
}

void WebContentClient::did_enter_tooltip_area(u64 page_id, ByteString title)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_enter_tooltip_area)
            view->on_enter_tooltip_area(title);
    }
}

void WebContentClient::did_leave_tooltip_area(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_leave_tooltip_area)
            view->on_leave_tooltip_area();
    }
}

void WebContentClient::did_hover_link(u64 page_id, URL::URL url)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_link_hover)
            view->on_link_hover(url);
    }
}

void WebContentClient::did_unhover_link(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_link_unhover)
            view->on_link_unhover();
    }
}

void WebContentClient::did_click_link(u64 page_id, URL::URL url, ByteString target, unsigned modifiers)
{
    if (modifiers == Web::UIEvents::Mod_PlatformCtrl)
        Application::the().open_url_in_new_tab(url, Web::HTML::ActivateTab::No);
    else if (target == "_blank"sv)
        Application::the().open_url_in_new_tab(url, Web::HTML::ActivateTab::Yes);
    else if (auto view = view_for_page_id(page_id); view.has_value())
        view->load(url);
}

void WebContentClient::did_middle_click_link(u64, URL::URL url, ByteString, unsigned)
{
    Application::the().open_url_in_new_tab(url, Web::HTML::ActivateTab::No);
}

void WebContentClient::did_request_context_menu(u64 page_id, Gfx::IntPoint content_position)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_request_page_context_menu({}, content_position);
}

void WebContentClient::did_request_link_context_menu(u64 page_id, Gfx::IntPoint content_position, URL::URL url, ByteString, unsigned)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_request_link_context_menu({}, content_position, move(url));
}

void WebContentClient::did_request_image_context_menu(u64 page_id, Gfx::IntPoint content_position, URL::URL url, ByteString, unsigned, Optional<Gfx::ShareableBitmap> bitmap)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_request_image_context_menu({}, content_position, move(url), move(bitmap));
}

void WebContentClient::did_request_media_context_menu(u64 page_id, Gfx::IntPoint content_position, ByteString, unsigned, Web::Page::MediaContextMenu menu)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_request_media_context_menu({}, content_position, move(menu));
}

void WebContentClient::did_get_source(u64, URL::URL url, URL::URL base_url, String source)
{
    if (auto view = Application::the().open_blank_new_tab(Web::HTML::ActivateTab::Yes); view.has_value()) {
        auto html = highlight_source(url, base_url, source, Syntax::Language::HTML, WebView::HighlightOutputMode::FullDocument);
        view->load_html(html);
    }
}

static JsonObject parse_json(StringView json, StringView name)
{
    auto parsed_tree = JsonValue::from_string(json);
    if (parsed_tree.is_error()) {
        dbgln("Unable to parse {}: {}", name, parsed_tree.error());
        return {};
    }

    if (!parsed_tree.value().is_object()) {
        dbgln("Expected {} to be an object: {}", name, parsed_tree.value());
        return {};
    }

    return move(parsed_tree.release_value().as_object());
}

void WebContentClient::did_inspect_dom_tree(u64 page_id, String dom_tree)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_dom_tree)
            view->on_received_dom_tree(parse_json(dom_tree, "DOM tree"sv));
    }
}

void WebContentClient::did_inspect_dom_node(u64 page_id, DOMNodeProperties properties)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_dom_node_properties)
            view->on_received_dom_node_properties(move(properties));
    }
}

void WebContentClient::did_inspect_accessibility_tree(u64 page_id, String accessibility_tree)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_accessibility_tree)
            view->on_received_accessibility_tree(parse_json(accessibility_tree, "accessibility tree"sv));
    }
}

void WebContentClient::did_get_hovered_node_id(u64 page_id, Web::UniqueNodeID node_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_hovered_node_id)
            view->on_received_hovered_node_id(node_id);
    }
}

void WebContentClient::did_finish_editing_dom_node(u64 page_id, Optional<Web::UniqueNodeID> node_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_finished_editing_dom_node)
            view->on_finished_editing_dom_node(node_id);
    }
}

void WebContentClient::did_mutate_dom(u64 page_id, Mutation mutation)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_dom_mutation_received)
            view->on_dom_mutation_received(move(mutation));
    }
}

void WebContentClient::did_get_dom_node_html(u64 page_id, String html)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_dom_node_html)
            view->on_received_dom_node_html(move(html));
    }
}

void WebContentClient::did_list_style_sheets(u64 page_id, Vector<Web::CSS::StyleSheetIdentifier> stylesheets)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_style_sheet_list)
            view->on_received_style_sheet_list(stylesheets);
    }
}

void WebContentClient::did_get_style_sheet_source(u64 page_id, Web::CSS::StyleSheetIdentifier identifier, URL::URL base_url, String source)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_style_sheet_source)
            view->on_received_style_sheet_source(identifier, base_url, source);
    }
}

void WebContentClient::did_take_screenshot(u64 page_id, Gfx::ShareableBitmap screenshot)
{
#if !defined(AK_OS_RINOS)
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_receive_screenshot({}, screenshot);
#else
    (void)page_id;
    (void)screenshot;
#endif
}

void WebContentClient::did_get_internal_page_info(u64 page_id, WebView::PageInfoType type, Optional<Core::AnonymousBuffer> info)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_receive_internal_page_info({}, type, info);
}

void WebContentClient::did_execute_js_console_input(u64 page_id, JsonValue result)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_received_js_console_result)
            view->on_received_js_console_result(move(result));
    }
}

void WebContentClient::did_output_js_console_message(u64 page_id, ConsoleOutput console_output)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_console_message)
            view->on_console_message(move(console_output));
    }
}

void WebContentClient::did_start_network_request(u64 page_id, u64 request_id, URL::URL url, ByteString method, Vector<HTTP::Header> request_headers, ByteBuffer request_body, Optional<String> initiator_type)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_network_request_started)
            view->on_network_request_started(request_id, url, method, request_headers, move(request_body), move(initiator_type));
    }
}

void WebContentClient::did_receive_network_response_headers(u64 page_id, u64 request_id, u32 status_code, Optional<String> reason_phrase, Vector<HTTP::Header> response_headers)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_network_response_headers_received)
            view->on_network_response_headers_received(request_id, status_code, reason_phrase, response_headers);
    }
}

void WebContentClient::did_receive_network_response_body(u64 page_id, u64 request_id, ByteBuffer data)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_network_response_body_received)
            view->on_network_response_body_received(request_id, move(data));
    }
}

void WebContentClient::did_finish_network_request(u64 page_id, u64 request_id, u64 body_size, Requests::RequestTimingInfo timing_info, Optional<Requests::NetworkError> network_error)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_network_request_finished)
            view->on_network_request_finished(request_id, body_size, timing_info, network_error);
    }
}

void WebContentClient::did_request_alert(u64 page_id, String message)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_alert)
            view->on_request_alert(message);
    }
}

void WebContentClient::did_request_confirm(u64 page_id, String message)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_confirm)
            view->on_request_confirm(message);
    }
}

void WebContentClient::did_request_prompt(u64 page_id, String message, String default_)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_prompt)
            view->on_request_prompt(message, default_);
    }
}

void WebContentClient::did_request_set_prompt_text(u64 page_id, String message)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_set_prompt_text)
            view->on_request_set_prompt_text(message);
    }
}

void WebContentClient::did_request_accept_dialog(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_accept_dialog)
            view->on_request_accept_dialog();
    }
}

void WebContentClient::did_request_dismiss_dialog(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_dismiss_dialog)
            view->on_request_dismiss_dialog();
    }
}

void WebContentClient::did_change_favicon(u64 page_id, Gfx::ShareableBitmap favicon)
{
    if (!favicon.is_valid()) {
        dbgln("DidChangeFavicon: Received invalid favicon");
        return;
    }

    if (auto view = view_for_page_id(page_id); view.has_value())
        view->set_favicon({}, *favicon.bitmap());
}

void WebContentClient::did_request_document_cookie_version_index(u64 page_id, i64 document_id, String domain)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (auto document_index = view->ensure_document_cookie_version_index({}, domain); !document_index.is_error())
            async_set_document_cookie_version_index(page_id, document_id, document_index.value());
    }
}

Messages::WebContentClient::DidRequestAllCookiesWebdriverResponse WebContentClient::did_request_all_cookies_webdriver(URL::URL url)
{
    return Application::cookie_jar().get_all_cookies_webdriver(url);
}

Messages::WebContentClient::DidRequestAllCookiesCookiestoreResponse WebContentClient::did_request_all_cookies_cookiestore(URL::URL url)
{
    return Application::cookie_jar().get_all_cookies_cookiestore(url);
}

Messages::WebContentClient::DidRequestNamedCookieResponse WebContentClient::did_request_named_cookie(URL::URL url, String name)
{
    return Application::cookie_jar().get_named_cookie(url, name);
}

Messages::WebContentClient::DidRequestCookieResponse WebContentClient::did_request_cookie(u64 page_id, URL::URL url, HTTP::Cookie::Source source)
{
    HTTP::Cookie::VersionedCookie cookie;
    cookie.cookie = Application::cookie_jar().get_cookie(url, source);

    if (source == HTTP::Cookie::Source::NonHttp) {
        if (auto view = view_for_page_id(page_id); view.has_value())
            cookie.cookie_version = view->document_cookie_version(url);
    }

    return cookie;
}

void WebContentClient::did_set_cookie(URL::URL url, HTTP::Cookie::ParsedCookie cookie, HTTP::Cookie::Source source)
{
    Application::cookie_jar().set_cookie(url, cookie, source);
}

void WebContentClient::did_update_cookie(HTTP::Cookie::Cookie cookie)
{
    Application::cookie_jar().update_cookie(cookie);
}

void WebContentClient::did_expire_cookies_with_time_offset(AK::Duration offset)
{
    Application::cookie_jar().expire_cookies_with_time_offset(offset);
}

Messages::WebContentClient::DidRequestStorageItemResponse WebContentClient::did_request_storage_item(u64 page_id, Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, String bottle_key, u64 owner_generation)
{
    if (!storage_owner_is_authorized(page_id, storage_endpoint, storage_key, owner_generation))
        return { Optional<String> {} };
    return Application::storage_jar().get_item(storage_endpoint, storage_key, bottle_key);
}

Messages::WebContentClient::DidSetStorageItemResponse WebContentClient::did_set_storage_item(u64 page_id, Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, String bottle_key, String value, u64 owner_generation)
{
    if (!storage_owner_is_authorized(page_id, storage_endpoint, storage_key, owner_generation))
        return WebView::StorageOperationError::QuotaExceededError;
    auto result = Application::storage_jar().set_item(
        storage_endpoint, storage_key, bottle_key, value);
    if (storage_endpoint != Web::StorageAPI::StorageEndpointType::Caches ||
        result.has<WebView::StorageOperationError>())
        return result;

    auto previous_value = result.get<Optional<String>>();
    auto view = view_for_page_id(page_id);
    const bool batch_active = view.has_value() &&
        view->on_cache_storage_owner_batch_active &&
        view->on_cache_storage_owner_batch_active();
    const bool owner_committed = view.has_value() &&
        view->on_cache_storage_owner_mutation &&
        view->on_cache_storage_owner_mutation(
            value == "RIN-CACHE-NAME-V1"_string ? 3u : 1u,
            storage_key.to_byte_string(), bottle_key.to_byte_string(),
            value.to_byte_string(), owner_generation);
    if (owner_committed)
        return result;
    if (batch_active)
        return result;

    /* Keep the WebContent SQLite copy aligned if Browser could not publish
     * the same mutation to its durable SWC1 owner. */
    if (previous_value.has_value())
        (void)Application::storage_jar().set_item(
            storage_endpoint, storage_key, bottle_key, previous_value.value());
    else
        Application::storage_jar().remove_item(
            storage_endpoint, storage_key, bottle_key);
    return WebView::StorageOperationError::QuotaExceededError;
}

void WebContentClient::did_remove_storage_item(u64 page_id, Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, String bottle_key, u64 owner_generation)
{
    if (!storage_owner_is_authorized(page_id, storage_endpoint, storage_key, owner_generation))
        return;
    auto previous_value = Application::storage_jar().get_item(
        storage_endpoint, storage_key, bottle_key);
    Application::storage_jar().remove_item(storage_endpoint, storage_key, bottle_key);
    if (storage_endpoint != Web::StorageAPI::StorageEndpointType::Caches)
        return;

    auto view = view_for_page_id(page_id);
    const bool batch_active = view.has_value() &&
        view->on_cache_storage_owner_batch_active &&
        view->on_cache_storage_owner_batch_active();
    const bool cache_name_key = bottle_key.bytes_as_string_view().starts_with(
        cache_storage_name_key_prefix);
    const bool cache_name_marker = cache_name_key ||
        (previous_value.has_value() &&
         previous_value.value() == "RIN-CACHE-NAME-V1"_string);
    const bool owner_committed = view.has_value() &&
        view->on_cache_storage_owner_mutation &&
        view->on_cache_storage_owner_mutation(
            cache_name_marker ? 4u : 2u,
            storage_key.to_byte_string(), bottle_key.to_byte_string(), {},
            owner_generation);
    if (!owner_committed && !batch_active && previous_value.has_value())
        (void)Application::storage_jar().set_item(
            storage_endpoint, storage_key, bottle_key, previous_value.value());
}

Messages::WebContentClient::DidRequestStorageKeysResponse WebContentClient::did_request_storage_keys(u64 page_id, Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, u64 owner_generation)
{
    if (!storage_owner_is_authorized(page_id, storage_endpoint, storage_key, owner_generation))
        return { Vector<String> {} };

    if (storage_endpoint != Web::StorageAPI::StorageEndpointType::Caches)
        return Application::storage_jar().get_all_keys(storage_endpoint, storage_key);

    auto view = view_for_page_id(page_id);
    if (!view.has_value() || !view->on_cache_storage_owner_snapshot ||
        !view->on_service_worker_owner_request ||
        !view->on_cache_storage_owner_mutation)
        return { Vector<String> {} };
    if (view->cache_storage_synchronized_origin == storage_key &&
        view->cache_storage_synchronized_generation == owner_generation)
        return Application::storage_jar().get_all_keys(storage_endpoint, storage_key);

    const auto origin = storage_key.to_byte_string();
    ByteString snapshot;
    Vector<CacheStorageOwnerRecord> owner_records;
    bool owner_initialized = false;
    if (!view->on_cache_storage_owner_snapshot(origin, owner_generation, snapshot) ||
        !parse_cache_storage_owner_snapshot(snapshot, owner_records, owner_initialized))
        return { Vector<String> {} };

    if (!owner_initialized) {
        Vector<CacheStorageOwnerRecord> legacy_records;
        size_t legacy_snapshot_size = 20u;
        auto legacy_keys = Application::storage_jar().get_all_keys(
            storage_endpoint, storage_key);
        if (legacy_keys.size() > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_RECORDS)
            return { Vector<String> {} };
        for (auto const& key : legacy_keys) {
            auto value = Application::storage_jar().get_item(
                storage_endpoint, storage_key, key);
            if (!value.has_value())
                continue;
            auto const& stored_value = value.value();
            auto owner_key = stored_value == cache_storage_marker
                ? cache_storage_name_owner_key(key)
                : key.to_byte_string();
            if (owner_key.is_empty() ||
                owner_key.length() > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_KEY_BYTES ||
                stored_value.bytes().size() >
                    RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_VALUE_BYTES)
                return { Vector<String> {} };
            const size_t encoded_record_size = 8u + owner_key.length() +
                stored_value.bytes().size();
            if (encoded_record_size >
                    RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_SNAPSHOT_BYTES - 20u ||
                legacy_snapshot_size >
                    RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_SNAPSHOT_BYTES -
                        encoded_record_size)
                return { Vector<String> {} };
            legacy_snapshot_size += encoded_record_size;
            legacy_records.append({ key, stored_value });
        }

        auto batch_response = view->on_service_worker_owner_request(
            RIN_WEBCONTENT_SERVICE_WORKER_OWNER_BEGIN_CACHE_BATCH,
            {}, origin, {}, {}, 0u);
        const auto batch_matches_owner = [&](auto const& response) {
            return response.accepted && response.found &&
                response.generation == owner_generation &&
                response.origin == origin && response.script_url == origin &&
                response.scope == "/";
        };
        if (!batch_matches_owner(batch_response))
            return { Vector<String> {} };

        bool migrated = true;
        for (auto const& record : legacy_records) {
            const bool is_name = record.value == cache_storage_marker;
            auto owner_key = is_name
                ? cache_storage_name_owner_key(record.key)
                : record.key.to_byte_string();
            if (!view->on_cache_storage_owner_mutation(
                    is_name
                        ? RIN_WEBCONTENT_CACHE_STORAGE_OWNER_SET_CACHE_NAME
                        : RIN_WEBCONTENT_CACHE_STORAGE_OWNER_SET,
                    origin, owner_key, record.value.to_byte_string(), owner_generation)) {
                migrated = false;
                break;
            }
        }
        if (migrated)
            migrated = view->on_cache_storage_owner_mutation(
                RIN_WEBCONTENT_CACHE_STORAGE_OWNER_INITIALIZE,
                origin, {}, {}, owner_generation);
        if (migrated) {
            auto commit_response = view->on_service_worker_owner_request(
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_COMMIT_CACHE_BATCH,
                {}, origin, {}, {}, 0u);
            migrated = batch_matches_owner(commit_response);
        }
        if (!migrated) {
            (void)view->on_service_worker_owner_request(
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_ABORT_CACHE_BATCH,
                {}, origin, {}, {}, 0u);
            return { Vector<String> {} };
        }

        snapshot = {};
        owner_records.clear();
        owner_initialized = false;
        if (!view->on_cache_storage_owner_snapshot(origin, owner_generation, snapshot) ||
            !parse_cache_storage_owner_snapshot(snapshot, owner_records, owner_initialized) ||
            !owner_initialized)
            return { Vector<String> {} };
    }

    Vector<CacheStorageOwnerRecord> previous_records;
    auto previous_keys = Application::storage_jar().get_all_keys(
        storage_endpoint, storage_key);
    if (previous_keys.size() > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_RECORDS)
        return { Vector<String> {} };
    size_t previous_snapshot_size = 20u;
    for (auto const& key : previous_keys) {
        auto value = Application::storage_jar().get_item(
            storage_endpoint, storage_key, key);
        if (!value.has_value())
            continue;
        const size_t key_size = key.bytes().size();
        const size_t value_size = value->bytes().size();
        if (key_size == 0u ||
            key_size > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_KEY_BYTES ||
            value_size > RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_VALUE_BYTES)
            return { Vector<String> {} };
        const size_t encoded_record_size = 8u + key_size + value_size;
        if (encoded_record_size >
                RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_SNAPSHOT_BYTES - 20u ||
            previous_snapshot_size >
                RIN_WEBCONTENT_CACHE_STORAGE_OWNER_MAX_SNAPSHOT_BYTES -
                    encoded_record_size)
            return { Vector<String> {} };
        previous_snapshot_size += encoded_record_size;
        previous_records.append({ key, value.release_value() });
    }

    auto& storage_jar = Application::storage_jar();
    storage_jar.clear_storage_key(storage_endpoint, storage_key);
    bool restored = true;
    for (auto const& record : owner_records) {
        auto result = storage_jar.set_item(
            storage_endpoint, storage_key, record.key, record.value);
        if (result.has<WebView::StorageOperationError>()) {
            restored = false;
            break;
        }
    }
    if (!restored) {
        storage_jar.clear_storage_key(storage_endpoint, storage_key);
        for (auto const& record : previous_records)
            (void)storage_jar.set_item(
                storage_endpoint, storage_key, record.key, record.value);
        return { Vector<String> {} };
    }

    view->cache_storage_synchronized_origin = storage_key;
    view->cache_storage_synchronized_generation = owner_generation;
    return Application::storage_jar().get_all_keys(storage_endpoint, storage_key);
}

void WebContentClient::did_clear_storage(u64 page_id, Web::StorageAPI::StorageEndpointType storage_endpoint, String storage_key, u64 owner_generation)
{
    if (!storage_owner_is_authorized(page_id, storage_endpoint, storage_key, owner_generation))
        return;

    if (storage_endpoint == Web::StorageAPI::StorageEndpointType::Caches) {
        auto view = view_for_page_id(page_id);
        if (!view.has_value() || !view->on_cache_storage_owner_snapshot ||
            !view->on_service_worker_owner_request ||
            !view->on_cache_storage_owner_mutation)
            return;
        const auto origin = storage_key.to_byte_string();
        ByteString snapshot;
        Vector<CacheStorageOwnerRecord> owner_records;
        bool owner_initialized = false;
        if (!view->on_cache_storage_owner_snapshot(origin, owner_generation, snapshot) ||
            !parse_cache_storage_owner_snapshot(snapshot, owner_records, owner_initialized))
            return;

        auto batch_response = view->on_service_worker_owner_request(
            RIN_WEBCONTENT_SERVICE_WORKER_OWNER_BEGIN_CACHE_BATCH,
            {}, origin, {}, {}, 0u);
        const auto batch_matches_owner = [&](auto const& response) {
            return response.accepted && response.found &&
                response.generation == owner_generation &&
                response.origin == origin && response.script_url == origin &&
                response.scope == "/";
        };
        if (!batch_matches_owner(batch_response))
            return;

        bool cleared = true;
        for (auto const& record : owner_records) {
            const bool is_name = record.value == cache_storage_marker;
            if (!view->on_cache_storage_owner_mutation(
                    is_name
                        ? RIN_WEBCONTENT_CACHE_STORAGE_OWNER_REMOVE_CACHE_NAME
                        : RIN_WEBCONTENT_CACHE_STORAGE_OWNER_REMOVE,
                    origin, record.key.to_byte_string(), {}, owner_generation)) {
                cleared = false;
                break;
            }
        }
        if (cleared)
            cleared = view->on_cache_storage_owner_mutation(
                RIN_WEBCONTENT_CACHE_STORAGE_OWNER_INITIALIZE,
                origin, {}, {}, owner_generation);
        if (cleared) {
            auto commit_response = view->on_service_worker_owner_request(
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_COMMIT_CACHE_BATCH,
                {}, origin, {}, {}, 0u);
            cleared = batch_matches_owner(commit_response);
        }
        if (!cleared) {
            (void)view->on_service_worker_owner_request(
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_ABORT_CACHE_BATCH,
                {}, origin, {}, {}, 0u);
            return;
        }
        view->cache_storage_synchronized_origin = storage_key;
        view->cache_storage_synchronized_generation = owner_generation;
    }

    Application::storage_jar().clear_storage_key(storage_endpoint, storage_key);
}

Messages::WebContentClient::DidRequestNewWebViewResponse WebContentClient::did_request_new_web_view(u64 page_id, Web::HTML::ActivateTab activate_tab, Web::HTML::WebViewHints hints, Optional<u64> page_index)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_new_web_view)
            return view->on_new_web_view(activate_tab, hints, page_index);
    }

    return String {};
}

void WebContentClient::did_request_activate_tab(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_activate_tab)
            view->on_activate_tab();
    }
}

void WebContentClient::did_close_browsing_context(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_close)
            view->on_close();
    }
}

void WebContentClient::did_update_resource_count(u64 page_id, i32 count_waiting)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_resource_status_change)
            view->on_resource_status_change(count_waiting);
    }
}

void WebContentClient::did_request_restore_window(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_restore_window)
            view->on_restore_window();
    }
}

void WebContentClient::did_request_reposition_window(u64 page_id, Gfx::IntPoint position)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_reposition_window)
            view->on_reposition_window(position);
    }
}

void WebContentClient::did_request_resize_window(u64 page_id, Gfx::IntSize size)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_resize_window)
            view->on_resize_window(size);
    }
}

void WebContentClient::did_request_maximize_window(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_maximize_window)
            view->on_maximize_window();
    }
}

void WebContentClient::did_request_minimize_window(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_minimize_window)
            view->on_minimize_window();
    }
}

void WebContentClient::did_request_fullscreen_window(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_fullscreen_window)
            view->on_fullscreen_window();
    }
}

void WebContentClient::did_request_exit_fullscreen(u64 page_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_exit_fullscreen_window)
            view->on_exit_fullscreen_window();
    }
}

void WebContentClient::did_request_file(u64 page_id, ByteString path, i32 request_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_file)
            view->on_request_file(path, request_id);
    }
}

void WebContentClient::did_request_download(u64 page_id, URL::URL url, ByteString suggested_filename)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_download) {
            auto origin = view->url().origin();
            auto referrer = !origin.is_opaque() &&
                    origin.scheme() == "https"sv
                ? origin.serialize().to_byte_string()
                : ByteString {};
            view->on_request_download(url, move(suggested_filename),
                                      move(referrer));
        }
    }
}

void WebContentClient::did_request_color_picker(u64 page_id, Color current_color)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_color_picker)
            view->on_request_color_picker(current_color);
    }
}

void WebContentClient::did_request_file_picker(u64 page_id, Web::HTML::FileFilter accepted_file_types, Web::HTML::AllowMultipleFiles allow_multiple_files)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_file_picker)
            view->on_request_file_picker(accepted_file_types, allow_multiple_files);
    }
}

void WebContentClient::did_request_select_dropdown(u64 page_id, Gfx::IntPoint content_position, i32 minimum_width, Vector<Web::HTML::SelectItem> items)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_request_select_dropdown)
            view->on_request_select_dropdown(view->to_widget_position(content_position), minimum_width / view->device_pixel_ratio(), items);
    }
}

void WebContentClient::did_finish_handling_input_event(u64 page_id, Web::EventResult event_result)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_finish_handling_input_event({}, event_result);
}

void WebContentClient::did_change_theme_color(u64 page_id, Gfx::Color color)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        if (view->on_theme_color_change)
            view->on_theme_color_change(color);
    }
}

void WebContentClient::did_insert_clipboard_entry(u64, Web::Clipboard::SystemClipboardRepresentation entry, String)
{
    Application::the().insert_clipboard_entry(move(entry));
}

void WebContentClient::did_request_clipboard_entries(u64 page_id, u64 request_id)
{
    if (auto view = view_for_page_id(page_id); view.has_value()) {
        Vector<Web::Clipboard::SystemClipboardItem> items;
        if (auto entries = Application::the().clipboard_entries(); !entries.is_empty())
            items.empend(move(entries));

        view->retrieved_clipboard_entries(request_id, items);
    }
}

void WebContentClient::did_change_audio_play_state(u64 page_id, Web::HTML::AudioPlayState play_state)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_change_audio_play_state({}, play_state);
}

void WebContentClient::did_update_navigation_buttons_state(u64 page_id, bool back_enabled, bool forward_enabled)
{
    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_update_navigation_buttons_state({}, back_enabled, forward_enabled);
}

void WebContentClient::did_allocate_backing_stores(u64 page_id, i32 front_bitmap_id, Web::SharedBackingStore front_backing_store, i32 back_bitmap_id, Web::SharedBackingStore back_backing_store)
{
    auto front_valid = front_backing_store.bitmap().is_valid();
    auto back_valid = back_backing_store.bitmap().is_valid();
#ifdef AK_OS_RINOS
    dbgln("[webcontent] did_allocate_backing_stores page={} front_id={} back_id={} front_valid={} back_valid={}",
        page_id, front_bitmap_id, back_bitmap_id, front_valid, back_valid);
#endif

    if (!front_valid || !back_valid) {
#ifdef AK_OS_RINOS
        dbgln("[webcontent] did_allocate_backing_stores accept failure page={} front_valid={} back_valid={}",
            page_id, front_valid, back_valid);
#endif
        return;
    }

    if (auto view = view_for_page_id(page_id); view.has_value())
        view->did_allocate_backing_stores({}, front_bitmap_id, move(front_backing_store), back_bitmap_id, move(back_backing_store));
}

Messages::WebContentClient::RequestWorkerAgentResponse WebContentClient::request_worker_agent(u64 page_id, Web::Bindings::AgentType worker_type)
{
    if (!view_for_page_id(page_id).has_value())
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };

    // The consumer profile packages isolated dedicated/shared workers only.
    // Service workers require a durable registration/cache owner and must not
    // accidentally reach the generic worker process.
    if (worker_type != Web::Bindings::AgentType::DedicatedWorker && worker_type != Web::Bindings::AgentType::SharedWorker)
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };

    auto request_server_handle = connect_new_request_server_client();
    if (request_server_handle.is_error()) {
        dbgln("WebContentClient: unable to connect worker to RequestServer: {}", request_server_handle.error());
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };
    }
    auto image_decoder_handle = connect_new_image_decoder_client();
    if (image_decoder_handle.is_error()) {
        dbgln("WebContentClient: unable to connect worker to ImageDecoder: {}", image_decoder_handle.error());
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };
    }
    auto worker_client = WebView::launch_web_worker_process(worker_type);
    if (worker_client.is_error()) {
        dbgln("WebContentClient: unable to launch worker: {}", worker_client.error());
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };
    }
    auto worker_handle = worker_client.value()->transport().release_for_transfer();
    if (worker_handle.is_error()) {
        dbgln("WebContentClient: unable to transfer worker transport: {}", worker_handle.error());
        return { IPC::TransportHandle {}, IPC::TransportHandle {}, IPC::TransportHandle {} };
    }

    return { worker_handle.release_value(), request_server_handle.release_value(), image_decoder_handle.release_value() };
}

Messages::WebContentClient::RequestServiceWorkerOwnerResponse WebContentClient::request_service_worker_owner(
    u64 page_id, u32 operation, ByteString client_url, ByteString origin,
    ByteString script_url, ByteString scope, u32 update_via_cache)
{
    auto view = view_for_page_id(page_id);
    if (!view.has_value() || !view->on_service_worker_owner_request)
        return { false, false, 0, 0, {}, {}, {} };

    auto result = view->on_service_worker_owner_request(
        operation, move(client_url), move(origin), move(script_url),
        move(scope), update_via_cache);
    return { result.accepted, result.found, result.generation, result.state,
             move(result.origin), move(result.script_url), move(result.scope) };
}

Messages::WebContentClient::RequestHttpCookieOwnerResponse WebContentClient::request_http_cookie_owner(
    u64 page_id, u32 operation, ByteString request_url, ByteString origin,
    ByteString cookie_data, u32 policy)
{
    if ((operation != RIN_WEBCONTENT_SERVICE_WORKER_OWNER_GET_HTTP_COOKIE_HEADER &&
         operation != RIN_WEBCONTENT_SERVICE_WORKER_OWNER_COMMIT_HTTP_COOKIES) ||
        request_url.is_empty() || request_url.length() >= RIN_WEBCONTENT_URL_MAX ||
        origin.is_empty() || origin.length() >= RIN_WEBCONTENT_URL_MAX ||
        cookie_data.length() > RIN_WEBCONTENT_HTTP_COOKIE_OWNER_MAX_DATA_BYTES)
        return { false, false, 0, {} };
    auto view = view_for_page_id(page_id);
    if (!view.has_value() || !view->on_http_cookie_owner_request)
        return { false, false, 0, {} };

    auto result = view->on_http_cookie_owner_request(
        operation, move(request_url), move(origin), move(cookie_data), policy);
    if (result.data.length() > RIN_WEBCONTENT_HTTP_COOKIE_OWNER_MAX_DATA_BYTES)
        return { false, false, 0, {} };
    return { result.accepted, result.found, result.generation, move(result.data) };
}

static ByteString serialize_cookie_owner_context(
    URL::URL const& url, ByteString const& request_context,
    ByteString const& method)
{
    if (request_context.is_empty() || request_context.length() > 4096u)
        return {};
    StringView input { request_context.characters(), request_context.length() };
    auto first_separator = input.find('|');
    auto second_separator = first_separator.has_value()
        ? input.find('|', first_separator.value() + 1u) : Optional<size_t> {};
    auto third_separator = second_separator.has_value()
        ? input.find('|', second_separator.value() + 1u) : Optional<size_t> {};
    auto fourth_separator = third_separator.has_value()
        ? input.find('|', third_separator.value() + 1u) : Optional<size_t> {};
    if (!first_separator.has_value() || !second_separator.has_value() ||
        !third_separator.has_value() || !fourth_separator.has_value() ||
        input.find('|', fourth_separator.value() + 1u).has_value() ||
        input.substring_view(0u, first_separator.value()) != "RCX1"sv)
        return {};

    auto origin_text = input.substring_view(first_separator.value() + 1u,
        second_separator.value() - first_separator.value() - 1u);
    auto ancestor_text = input.substring_view(second_separator.value() + 1u,
        third_separator.value() - second_separator.value() - 1u);
    auto navigation_text = input.substring_view(third_separator.value() + 1u,
        fourth_separator.value() - third_separator.value() - 1u);
    auto partition_text = input.substring_view(fourth_separator.value() + 1u);
    if ((ancestor_text != "0"sv && ancestor_text != "1"sv) ||
        (navigation_text != "0"sv && navigation_text != "1"sv) ||
        partition_text.is_empty() || partition_text.length() > 272u)
        return {};

    Optional<URL::Origin> request_origin;
    const bool no_client = origin_text == "-"sv;
    if (!no_client && origin_text != "null"sv) {
        auto origin_url = URL::Parser::basic_parse(
            MUST(String::formatted("{}/", origin_text)));
        if (!origin_url.has_value() ||
            origin_url->origin().serialize() != origin_text)
            return {};
        request_origin = origin_url->origin();
    }

    String partition_site;
    if (partition_text == "null"sv) {
        partition_site = "null"_string;
    } else {
        auto site_url = URL::Parser::basic_parse(
            MUST(String::formatted("{}/", partition_text)));
        if (!site_url.has_value() ||
            URL::Site::obtain(site_url->origin()).serialize() != partition_text)
            return {};
        partition_site = MUST(String::from_utf8(partition_text));
    }

    const bool has_cross_site_ancestor = ancestor_text == "1"sv;
    const bool top_level_navigation = navigation_text == "1"sv;
    const bool same_site = request_origin.has_value() &&
        !has_cross_site_ancestor && request_origin->is_same_site(url.origin());
    const bool safe_method = method.equals_ignoring_ascii_case("GET"sv) ||
        method.equals_ignoring_ascii_case("HEAD"sv) ||
        method.equals_ignoring_ascii_case("OPTIONS"sv) ||
        method.equals_ignoring_ascii_case("TRACE"sv);
    const bool top_level_safe_navigation = top_level_navigation && safe_method;
    if (top_level_navigation)
        partition_site = URL::Site::obtain(url.origin()).serialize();
    return ByteString::formatted("RSC1|{}|{}|{}|{}\n",
        same_site ? "1"sv : "0"sv,
        top_level_safe_navigation ? "1"sv : "0"sv,
        top_level_navigation ? "1"sv : "0"sv,
        partition_site.to_byte_string());
}

String WebContentClient::retrieve_http_cookie_header(
    URL::URL const& url, ByteString const& request_context,
    ByteString const& method)
{
    auto cookie_context = serialize_cookie_owner_context(url, request_context, method);
    if (cookie_context.is_empty())
        return {};
    String cookie_header;
    WebContentClient::for_each_client([&](WebContentClient& client) {
        for (auto const& [page_id, view] : client.m_views) {
            (void)view;
            if (page_id == 0u)
                continue;
            auto serialized_origin = url.origin().serialize();
            auto response = client.request_http_cookie_owner(
                page_id,
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_GET_HTTP_COOKIE_HEADER,
                url.to_byte_string(), serialized_origin.to_byte_string(), cookie_context,
                RIN_WEBCONTENT_SERVICE_WORKER_OWNER_COOKIE_CREDENTIALS_INCLUDE);
            if (!response.accepted || !response.found || response.generation == 0u)
                continue;

            StringBuilder builder;
            builder.append(response.response_data());
            auto header = builder.to_string();
            if (header.is_error())
                continue;
            cookie_header = header.release_value();
            return IterationDecision::Break;
        }
        return IterationDecision::Continue;
    });
    return cookie_header;
}

Messages::WebContentClient::RequestNotificationPermissionResponse
WebContentClient::request_notification_permission(u64 page_id)
{
    auto view = view_for_page_id(page_id);
    if (!view.has_value() || !view->on_request_notification_permission)
        return { 0u, 0u };

    auto request = view->on_request_notification_permission();
    return { request.navigation_generation, request.request_id };
}

Optional<ViewImplementation&> WebContentClient::view_for_page_id(u64 page_id, SourceLocation location)
{
    // Don't bother logging anything for the spare WebContent process. It will only receive a load notification for about:blank.
    if (m_views.is_empty())
        return {};

    if (auto view = m_views.get(page_id); view.has_value())
        return *view.value();

    dbgln("WebContentClient::{}: Did not find a page with ID {}", location.function_name(), page_id);
    return {};
}

}
