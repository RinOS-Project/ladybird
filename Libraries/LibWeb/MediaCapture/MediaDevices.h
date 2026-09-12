/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Bindings/MediaDevicesPrototype.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/MediaCapture/MediaStream.h>
#include <LibWeb/WebIDL/Types.h>

namespace Web::MediaCapture {

struct MediaStreamConstraints {
    bool audio { false };
    bool video { false };
};

class MediaDevices final : public DOM::EventTarget {
    WEB_PLATFORM_OBJECT(MediaDevices, DOM::EventTarget);
    GC_DECLARE_ALLOCATOR(MediaDevices);

public:
    GC::Ref<WebIDL::Promise> get_user_media(MediaStreamConstraints const&);

private:
    explicit MediaDevices(JS::Realm&);

    virtual void initialize(JS::Realm&) override;
};

}
