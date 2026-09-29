/*
 * Copyright (c) 2026, RinOS contributors
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

namespace RequestServer {

struct CurlTransferContext {
    enum class Kind {
        Request,
        WebSocket,
    };

    Kind kind;
    void* owner;
};

}
