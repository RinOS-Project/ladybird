/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWebView/ViewImplementation.h>
#include <android/bitmap.h>
#include <jni.h>

namespace Ladybird {

class WebViewImplementationNative : public WebView::ViewImplementation {
public:
    WebViewImplementationNative(jobject thiz);

    virtual Web::DevicePixelSize viewport_size() const override { return m_viewport_size; }
    virtual Gfx::IntPoint to_content_position(Gfx::IntPoint p) const override { return p; }
    virtual Gfx::IntPoint to_widget_position(Gfx::IntPoint p) const override { return p; }
    virtual void update_zoom() override { }

    NonnullRefPtr<WebView::WebContentClient> bind_web_content_client();

    virtual void initialize_client(CreateNewClient) override;

    void paint_into_bitmap(void* android_bitmap_raw, AndroidBitmapInfo const& info);

    void set_viewport_geometry(int w, int h);
    void set_zoom_level(double zoom_level);
    void set_device_pixel_ratio(double f);

    void mouse_event(Web::MouseEvent::Type event_type, float x, float y, float raw_x, float raw_y);

    void complete_geolocation_permission(u32 navigation_generation, u64 request_id,
        bool allowed, bool fix_available, i64 latitude_e7, i64 longitude_e7,
        i64 altitude_mm, u32 horizontal_accuracy_mm, u32 vertical_accuracy_mm,
        u32 speed_mm_per_second, u32 heading_millidegrees, u32 flags)
    {
        WebView::ViewImplementation::complete_geolocation_permission(
            navigation_generation, request_id, allowed, fix_available, latitude_e7,
            longitude_e7, altitude_mm, horizontal_accuracy_mm, vertical_accuracy_mm,
            speed_mm_per_second, heading_millidegrees, flags);
    }

    static jclass global_class_reference;
    static jmethodID bind_webcontent_method;
    static jmethodID invalidate_layout_method;
    static jmethodID on_load_start_method;
    static jmethodID request_geolocation_permission_method;
    static jmethodID cancel_geolocation_permission_method;

    jobject java_instance() const { return m_java_instance; }

private:
    jobject m_java_instance = nullptr;
    Web::DevicePixelSize m_viewport_size;
    u32 m_geolocation_navigation_generation { 1 };
    u64 m_next_geolocation_request_id { 0 };
};

}
