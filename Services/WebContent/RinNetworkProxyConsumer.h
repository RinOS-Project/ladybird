/*
 * Copyright (c) 2026 RinOS contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

namespace WebContent {

// Polls networkd off the WebContent event loop and applies validated snapshots
// from the event loop thread.
bool start_network_proxy_consumer();
void apply_network_proxy_snapshot();
void stop_network_proxy_consumer();

}
