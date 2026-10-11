/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "WebViewImplementationNative.h"
#include "JNIHelpers.h"
#include <LibGfx/Bitmap.h>
#include <LibGfx/ImmutableBitmap.h>
#include <LibGfx/Painter.h>
#include <LibWeb/Crypto/Crypto.h>
#include <LibWeb/Page/Page.h>
#include <LibURL/Parser.h>
#include <LibWebView/ViewImplementation.h>
#include <LibWebView/WebContentClient.h>
#include <android/bitmap.h>
#include <jni.h>

namespace Ladybird {

static Gfx::BitmapFormat to_gfx_bitmap_format(i32 f)
{
    switch (f) {
    case ANDROID_BITMAP_FORMAT_RGBA_8888:
        return Gfx::BitmapFormat::RGBA8888;
    default:
        VERIFY_NOT_REACHED();
    }
}

WebViewImplementationNative::WebViewImplementationNative(jobject thiz)
    : m_java_instance(thiz)
{
    // NOTE: m_java_instance's global ref is controlled by the JNI bindings
    initialize_client(CreateNewClient::Yes);

    on_ready_to_paint = [this]() {
        JavaEnvironment env(global_vm);
        env.get()->CallVoidMethod(m_java_instance, invalidate_layout_method);
    };

    on_load_start = [this](URL::URL const& url, bool is_redirect) {
        ++m_geolocation_navigation_generation;
        if (m_geolocation_navigation_generation == 0)
            ++m_geolocation_navigation_generation;
        JavaEnvironment env(global_vm);
        auto url_string = env.jstring_from_ak_string(url.to_string());
        env.get()->CallVoidMethod(m_java_instance, on_load_start_method, url_string, is_redirect,
            static_cast<jint>(m_geolocation_navigation_generation));
        env.get()->DeleteLocalRef(url_string);
    };

    on_request_geolocation_permission = [this](ByteString origin, u32 timeout_ms) {
        auto request_id = ++m_next_geolocation_request_id;
        if (request_id == 0)
            return PermissionRequest {};

        auto navigation_generation = m_geolocation_navigation_generation;
        auto parsed_origin = URL::Parser::basic_parse(origin.view());
        if (!parsed_origin.has_value() ||
            (parsed_origin->scheme() != "http"sv && parsed_origin->scheme() != "https"sv) ||
            parsed_origin->includes_credentials() ||
            parsed_origin->origin().serialize().to_byte_string() != origin) {
            complete_geolocation_permission(navigation_generation, request_id,
                false, false, 0, 0, 0, 0, 0, 0, 0, 0);
            return PermissionRequest { navigation_generation, request_id };
        }

        JavaEnvironment env(global_vm);
        auto origin_string = env.jstring_from_ak_string(origin);
        env.get()->CallVoidMethod(m_java_instance, request_geolocation_permission_method,
            origin_string, static_cast<jint>(timeout_ms),
            static_cast<jint>(navigation_generation), static_cast<jlong>(request_id));
        env.get()->DeleteLocalRef(origin_string);
        return PermissionRequest { navigation_generation, request_id };
    };

    on_cancel_geolocation_permission = [this](u32 navigation_generation,
                                                u64 request_id) {
        if (request_id == 0u ||
            navigation_generation != m_geolocation_navigation_generation)
            return;
        JavaEnvironment env(global_vm);
        env.get()->CallVoidMethod(m_java_instance,
            cancel_geolocation_permission_method,
            static_cast<jint>(navigation_generation),
            static_cast<jlong>(request_id));
    };
}

void WebViewImplementationNative::initialize_client(WebView::ViewImplementation::CreateNewClient)
{
    m_client_state = {};

    auto new_client = bind_web_content_client();

    m_client_state.client = new_client;
    m_client_state.client->on_web_content_process_crash = [] {
        warnln("WebContent crashed!");
        // FIXME: launch a new client
    };

    m_client_state.client_handle = MUST(Web::Crypto::generate_random_uuid());
    client().async_set_window_handle(0, m_client_state.client_handle);

    client().async_set_viewport(0, viewport_size(), m_device_pixel_ratio, Web::ViewportIsFullscreen::No);
    client().async_set_zoom_level(0, m_zoom_level);

    set_system_visibility_state(Web::HTML::VisibilityState::Visible);

    // FIXME: update_palette, update system fonts
}

void WebViewImplementationNative::paint_into_bitmap(void* android_bitmap_raw, AndroidBitmapInfo const& info)
{
    // Software bitmaps only for now!
    VERIFY((info.flags & ANDROID_BITMAP_FLAGS_IS_HARDWARE) == 0);

    auto android_bitmap = MUST(Gfx::Bitmap::create_wrapper(to_gfx_bitmap_format(info.format), Gfx::AlphaType::Premultiplied, { info.width, info.height }, info.stride, android_bitmap_raw));
    auto painter = Gfx::Painter::create(android_bitmap);
    if (auto* bitmap = m_client_state.has_usable_bitmap ? m_client_state.front_bitmap.bitmap.ptr() : m_backup_bitmap.ptr())
        painter->draw_bitmap(android_bitmap->rect().to_type<float>(), Gfx::ImmutableBitmap::create(MUST(bitmap->clone())), bitmap->rect(), Gfx::ScalingMode::NearestNeighbor, {}, 1.0f, Gfx::CompositingAndBlendingOperator::Copy);
    else
        painter->fill_rect(android_bitmap->rect().to_type<float>(), Gfx::Color::Magenta);
}

void WebViewImplementationNative::set_viewport_geometry(int w, int h)
{
    m_viewport_size = { w, h };
    handle_resize();
}

void WebViewImplementationNative::set_device_pixel_ratio(double f)
{
    m_device_pixel_ratio = f;
    handle_resize();
}

void WebViewImplementationNative::set_zoom_level(double f)
{
    m_zoom_level = f;
    client().async_set_zoom_level(0, m_zoom_level);
}

void WebViewImplementationNative::mouse_event(Web::MouseEvent::Type event_type, float x, float y, float raw_x, float raw_y)
{
    Gfx::IntPoint position = { x, y };
    Gfx::IntPoint screen_position = { raw_x, raw_y };
    auto event = Web::MouseEvent {
        event_type,
        position.to_type<Web::DevicePixels>(),
        screen_position.to_type<Web::DevicePixels>(),
        Web::UIEvents::MouseButton::Primary,
        Web::UIEvents::MouseButton::Primary,
        Web::UIEvents::KeyModifier::Mod_None,
        0,
        0,
        nullptr
    };

    enqueue_input_event(move(event));
}

NonnullRefPtr<WebView::WebContentClient> WebViewImplementationNative::bind_web_content_client()
{
    JavaEnvironment env(global_vm);

    int socket_fds[2] {};
    MUST(Core::System::socketpair(AF_LOCAL, SOCK_STREAM, 0, socket_fds));

    int ui_fd = socket_fds[0];
    int wc_fd = socket_fds[1];

    // NOTE: The java object takes ownership of the socket fds
    env.get()->CallVoidMethod(m_java_instance, bind_webcontent_method, wc_fd);

    auto socket = MUST(Core::LocalSocket::adopt_fd(ui_fd));
    MUST(socket->set_blocking(true));

    auto new_client = make_ref_counted<WebView::WebContentClient>(make<IPC::Transport>(move(socket)), *this);

    return new_client;
}

}
