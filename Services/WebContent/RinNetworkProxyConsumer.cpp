/*
 * Copyright (c) 2026 RinOS contributors
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "RinNetworkProxyConsumer.h"

#include <AK/ByteString.h>
#include <AK/StringBuilder.h>
#include <LibCore/Proxy.h>
#include <LibURL/Parser.h>
#include <LibWeb/Loader/ProxyMappings.h>

#include "../../../../src/services/networkd/rin_network_service.h"
#include "../../../../src/shared/rin_socket_abi.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

namespace WebContent {

static constexpr u64 s_networkd_request_timeout_ms = 2000;
static constexpr u64 s_networkd_poll_interval_ms = 1000;

struct ProxySnapshot {
    bool available { false };
    u64 revision { 1 };
    RinNetworkProxyConfigV1 config { };
};

static pthread_mutex_t s_snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_worker;
static bool s_worker_started;
static bool s_stop_requested;
static ProxySnapshot s_snapshot;
static u64 s_applied_revision;

static u64 monotonic_time_ms()
{
    timespec now { };
    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return static_cast<u64>(now.tv_sec) * 1000u + static_cast<u64>(now.tv_nsec / 1000000);
}

static bool wait_for_socket(int fd, short events, u64 deadline_ms)
{
    for (;;) {
        auto now = monotonic_time_ms();
        if (now == 0 || now >= deadline_ms) {
            errno = ETIMEDOUT;
            return false;
        }

        pollfd descriptor { };
        descriptor.fd = fd;
        descriptor.events = events;
        auto remaining = deadline_ms - now;
        descriptor.revents = 0;
        auto timeout = remaining > static_cast<u64>(INT_MAX)
            ? INT_MAX
            : static_cast<int>(remaining);
        auto result = ::poll(&descriptor, 1, timeout);
        if (result > 0) {
            // A peer may close immediately after writing its fixed response;
            // consume readable bytes before treating POLLHUP as a failure.
            if ((descriptor.revents & events) != 0)
                return true;
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                errno = ECONNRESET;
                return false;
            }
            errno = EPROTO;
            return false;
        }
        if (result == 0) {
            errno = ETIMEDOUT;
            return false;
        }
        if (errno != EINTR)
            return false;
    }
}

static bool send_exact(int fd, void const* input, size_t size, u64 deadline_ms)
{
    auto const* bytes = static_cast<u8 const*>(input);
    size_t offset = 0;
    while (offset < size) {
        if (!wait_for_socket(fd, POLLOUT, deadline_ms))
            return false;
        auto written = ::send(fd, bytes + offset, size - offset, MSG_NOSIGNAL);
        if (written < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (written <= 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

static bool receive_exact(int fd, void* output, size_t size, u64 deadline_ms)
{
    auto* bytes = static_cast<u8*>(output);
    size_t offset = 0;
    while (offset < size) {
        if (!wait_for_socket(fd, POLLIN, deadline_ms))
            return false;
        auto received = ::recv(fd, bytes + offset, size - offset, 0);
        if (received < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (received <= 0)
            return false;
        offset += static_cast<size_t>(received);
    }
    return true;
}

static bool authenticated_service_peer(RinNetworkServicePeerV1& peer)
{
    rin_unix_peer_app_identity_v1 app { };
    rin_unix_peer_session_identity_v1 session { };
    socklen_t app_size = sizeof(app);
    socklen_t session_size = sizeof(session);
    int pair[2] { -1, -1 };
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0)
        return false;

    bool valid = ::getsockopt(pair[0], SOL_SOCKET,
                     SO_RIN_UNIX_PEER_APP_IDENTITY, &app, &app_size)
            == 0
        && app_size == sizeof(app) && rin_unix_peer_app_identity_valid(&app) && ::getsockopt(pair[0], SOL_SOCKET, SO_RIN_UNIX_PEER_SESSION_IDENTITY, &session, &session_size) == 0 && session_size == sizeof(session) && rin_unix_peer_session_identity_valid(&session) && session.uid == app.owner_uid;
    if (valid) {
        memset(&peer, 0, sizeof(peer));
        peer.struct_size = sizeof(peer);
        peer.version = RIN_NETWORK_SERVICE_VERSION;
        peer.flags = RIN_NETWORK_SERVICE_PEER_AUTHENTICATED;
        peer.uid = app.owner_uid;
        peer.process_id = app.process_id;
        peer.process_generation = app.process_instance_cookie;
        peer.connection_id = app.connection_id;
        peer.session_id = session.session_id;
        peer.session_cookie = session.instance_cookie;
        peer.capabilities = app.capabilities;
        valid = rin_network_service_peer_valid(&peer);
    }

    (void)::close(pair[0]);
    (void)::close(pair[1]);
    return valid;
}

static bool response_header_valid(RinNetworkServiceResponseV1 const& response,
    RinNetworkServiceRequestV1 const& request,
    RinNetworkServicePeerV1 const& peer)
{
    auto const& header = response.header;
    return header.magic == RIN_NETWORK_SERVICE_PROTOCOL_MAGIC && header.version == RIN_NETWORK_SERVICE_VERSION && header.operation == request.header.operation && header.header_size == sizeof(header) && header.payload_size == sizeof(response.body) && header.reserved0 == 0u && header.reserved1 == 0u && header.request_id == request.header.request_id && header.session_id == peer.session_id && header.session_cookie == peer.session_cookie && header.status <= RIN_NETWORK_SERVICE_OK && header.status >= RIN_NETWORK_SERVICE_PERSISTENCE_UNKNOWN;
}

static bool fetch_proxy_config(RinNetworkProxyConfigV1& config)
{
    RinNetworkServicePeerV1 peer { };
    RinNetworkServiceRequestV1 request { };
    RinNetworkServiceResponseV1 response { };
    int fd = -1;
    bool success = false;

    if (!authenticated_service_peer(peer))
        goto done;

    request.header.magic = RIN_NETWORK_SERVICE_PROTOCOL_MAGIC;
    request.header.version = RIN_NETWORK_SERVICE_VERSION;
    request.header.operation = RIN_NETWORK_SERVICE_OP_GET_PROXY;
    request.header.header_size = sizeof(request.header);
    request.header.payload_size = sizeof(request) - sizeof(request.header);
    request.header.request_id = 1u;
    request.header.session_id = peer.session_id;
    request.header.session_cookie = peer.session_cookie;
    request.peer = peer;

    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        goto done;
    {
        auto flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            goto done;
    }
    {
        sockaddr_un address { };
        address.sun_family = AF_UNIX;
        auto path_size = sizeof(RIN_NETWORK_SERVICE_SOCKET_PATH);
        if (path_size > sizeof(address.sun_path))
            goto done;
        memcpy(address.sun_path, RIN_NETWORK_SERVICE_SOCKET_PATH, path_size);
        auto connected = ::connect(fd, reinterpret_cast<sockaddr*>(&address),
            sizeof(address));
        if (connected < 0 && errno != EINPROGRESS && errno != EAGAIN)
            goto done;
        if (connected < 0) {
            auto now = monotonic_time_ms();
            if (now == 0 || !wait_for_socket(fd, POLLOUT, now + s_networkd_request_timeout_ms))
                goto done;
            int socket_error = 0;
            socklen_t socket_error_size = sizeof(socket_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                    &socket_error_size)
                    != 0
                || socket_error != 0) {
                if (socket_error != 0)
                    errno = socket_error;
                goto done;
            }
        }
    }

    {
        auto now = monotonic_time_ms();
        if (now == 0 || now > UINT64_MAX - s_networkd_request_timeout_ms)
            goto done;
        auto deadline = now + s_networkd_request_timeout_ms;
        if (!send_exact(fd, &request, sizeof(request), deadline) || !receive_exact(fd, &response, sizeof(response), deadline) || !response_header_valid(response, request, peer) || response.header.status != RIN_NETWORK_SERVICE_OK || !rin_network_proxy_config_valid(&response.body.proxy))
            goto done;
    }
    config = response.body.proxy;
    success = true;

done:
    if (fd >= 0)
        (void)::close(fd);
    memset(&peer, 0, sizeof(peer));
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    return success;
}

static void publish_snapshot(bool available,
    RinNetworkProxyConfigV1 const* config)
{
    pthread_mutex_lock(&s_snapshot_mutex);
    s_snapshot.available = available;
    memset(&s_snapshot.config, 0, sizeof(s_snapshot.config));
    if (available && config != nullptr)
        s_snapshot.config = *config;
    if (++s_snapshot.revision == 0u)
        ++s_snapshot.revision;
    pthread_mutex_unlock(&s_snapshot_mutex);
}

static bool stop_requested()
{
    pthread_mutex_lock(&s_snapshot_mutex);
    bool stop = s_stop_requested;
    pthread_mutex_unlock(&s_snapshot_mutex);
    return stop;
}

static void* networkd_worker(void*)
{
    while (!stop_requested()) {
        RinNetworkProxyConfigV1 config { };
        auto available = fetch_proxy_config(config);
        publish_snapshot(available, available ? &config : nullptr);

        for (u64 elapsed = 0; elapsed < s_networkd_poll_interval_ms && !stop_requested(); elapsed += 100u) {
            timespec delay { 0, 100000000 };
            while (::nanosleep(&delay, &delay) != 0 && errno == EINTR) {
            }
        }
    }
    return nullptr;
}

bool start_network_proxy_consumer()
{
    pthread_mutex_lock(&s_snapshot_mutex);
    if (s_worker_started) {
        pthread_mutex_unlock(&s_snapshot_mutex);
        return true;
    }
    s_stop_requested = false;
    s_snapshot.available = false;
    memset(&s_snapshot.config, 0, sizeof(s_snapshot.config));
    if (++s_snapshot.revision == 0u)
        ++s_snapshot.revision;
    auto result = ::pthread_create(&s_worker, nullptr, networkd_worker, nullptr);
    if (result == 0)
        s_worker_started = true;
    pthread_mutex_unlock(&s_snapshot_mutex);
    if (result != 0) {
        errno = result;
        return false;
    }
    return true;
}

static Core::ProxyData proxy_from_snapshot(ProxySnapshot const& snapshot)
{
    if (!snapshot.available || !rin_network_proxy_config_valid(&snapshot.config)) {
        return { .type = Core::ProxyData::Type::Blocked };
    }
    if (snapshot.config.enabled == 0u)
        return { };

    StringView host { snapshot.config.host };
    StringBuilder url_builder;
    auto is_bracketed_ipv6 = host.length() >= 2 && host[0] == '[' && host[host.length() - 1] == ']';
    if (host.contains(':') && !is_bracketed_ipv6)
        url_builder.appendff("http://[{}]:{}", host, snapshot.config.port);
    else
        url_builder.appendff("http://{}:{}", host, snapshot.config.port);
    auto parsed_url = URL::Parser::basic_parse(url_builder.to_byte_string());
    if (!parsed_url.has_value())
        return { .type = Core::ProxyData::Type::Blocked };
    auto proxy = Core::ProxyData::parse_url(parsed_url.value());
    if (proxy.is_error())
        return { .type = Core::ProxyData::Type::Blocked };
    return proxy.release_value();
}

void apply_network_proxy_snapshot()
{
    ProxySnapshot snapshot { };
    pthread_mutex_lock(&s_snapshot_mutex);
    snapshot = s_snapshot;
    pthread_mutex_unlock(&s_snapshot_mutex);
    if (snapshot.revision == s_applied_revision)
        return;
    Web::ProxyMappings::the().set_system_proxy(proxy_from_snapshot(snapshot));
    s_applied_revision = snapshot.revision;
}

void stop_network_proxy_consumer()
{
    pthread_mutex_lock(&s_snapshot_mutex);
    auto should_join = s_worker_started;
    s_stop_requested = true;
    auto worker = s_worker;
    s_worker_started = false;
    pthread_mutex_unlock(&s_snapshot_mutex);
    if (should_join)
        (void)::pthread_join(worker, nullptr);
}

}
