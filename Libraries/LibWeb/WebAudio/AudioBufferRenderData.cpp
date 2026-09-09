/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <LibWeb/WebAudio/AudioBuffer.h>
#include <LibWeb/WebAudio/AudioBufferRenderData.h>

namespace Web::WebAudio {

ErrorOr<NonnullRefPtr<AudioBufferRenderData>> AudioBufferRenderData::create(AudioBuffer const& buffer)
{
    auto data = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) AudioBufferRenderData(buffer.sample_rate(), buffer.length())));
    TRY(data->m_channels.try_resize(buffer.number_of_channels()));

    for (u32 channel = 0; channel < buffer.number_of_channels(); ++channel) {
        TRY(data->m_channels[channel].try_resize(buffer.length()));
        buffer.channel_data(channel).copy_to(data->m_channels[channel].span());
    }

    return data;
}

}
