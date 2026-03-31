/*
 * Copyright (c) 2020, Ali Mohammad Pur <mpfard@serenityos.org>
 * Copyright (c) 2025, Altomani Gianluca <altomanigianluca@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibCore/Socket.h>
#include <LibCrypto/Certificate/Certificate.h>
#ifndef AK_OS_RINOS
#    include <LibTLS/OpenSSLForward.h>
#endif

namespace TLS {

struct Options {
    Optional<ByteString> root_certificates_path;
};

class TLSv12 final : public Core::Socket {
public:
    virtual ErrorOr<Bytes> read_some(Bytes) override;
    virtual ErrorOr<size_t> write_some(ReadonlyBytes) override;

    virtual bool is_eof() const override;
    virtual bool is_open() const override;

    virtual void close() override;

    virtual ErrorOr<size_t> pending_bytes() const override;
    virtual ErrorOr<bool> can_read_without_blocking(int timeout = 0) const override;
    virtual ErrorOr<void> set_blocking(bool block) override;
    virtual ErrorOr<void> set_close_on_exec(bool enabled) override;

    static ErrorOr<NonnullOwnPtr<TLSv12>> connect(Core::SocketAddress const&, ByteString const& host, Options = {});
    static ErrorOr<NonnullOwnPtr<TLSv12>> connect(ByteString const& host, u16 port, Options = {});

    ~TLSv12() override;

private:
#ifdef AK_OS_RINOS
    struct rintls_ctx;
    explicit TLSv12(NonnullOwnPtr<Core::TCPSocket>, rintls_ctx*);
#else
    explicit TLSv12(NonnullOwnPtr<Core::TCPSocket>, SSL_CTX*, SSL*);
#endif

    static ErrorOr<NonnullOwnPtr<TLSv12>> connect_internal(NonnullOwnPtr<Core::TCPSocket>, ByteString const&, Options);

    void handle_fatal_error();

#ifdef AK_OS_RINOS
    rintls_ctx* m_ctx { nullptr };
#else
    SSL_CTX* m_ssl_ctx { nullptr };
    SSL* m_ssl { nullptr };
#endif

    NonnullOwnPtr<Core::TCPSocket> m_socket;
};

}
