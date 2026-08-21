/*
 * Copyright (c) 2026, the RinOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCompress/Brotli.h>

namespace Compress {

static Error brotli_unavailable()
{
    return Error::from_string_literal("Brotli compression is not available on RinOS");
}

BrotliDecompressor::BrotliDecompressor(AK::FixedArray<u8> buffer, MaybeOwned<Stream> stream, BrotliDecoderState* decoder)
    : m_buffer(move(buffer))
    , m_stream(move(stream))
    , m_decoder(decoder)
{
}

ErrorOr<NonnullOwnPtr<BrotliDecompressor>> BrotliDecompressor::create(MaybeOwned<Stream>)
{
    return brotli_unavailable();
}

ErrorOr<ByteBuffer> BrotliDecompressor::decompress_all(ReadonlyBytes)
{
    return brotli_unavailable();
}

BrotliDecompressor::~BrotliDecompressor() = default;

ErrorOr<Bytes> BrotliDecompressor::read_some(Bytes)
{
    return brotli_unavailable();
}

ErrorOr<size_t> BrotliDecompressor::write_some(ReadonlyBytes)
{
    return brotli_unavailable();
}

bool BrotliDecompressor::is_eof() const { return false; }
bool BrotliDecompressor::is_open() const { return false; }
void BrotliDecompressor::close() { }

BrotliCompressor::BrotliCompressor(AK::FixedArray<u8> buffer, MaybeOwned<Stream> stream, BrotliEncoderState* encoder)
    : m_buffer(move(buffer))
    , m_stream(move(stream))
    , m_encoder(encoder)
{
}

ErrorOr<NonnullOwnPtr<BrotliCompressor>> BrotliCompressor::create(MaybeOwned<Stream>, BrotliCompressionLevel)
{
    return brotli_unavailable();
}

ErrorOr<ByteBuffer> BrotliCompressor::compress_all(ReadonlyBytes, BrotliCompressionLevel)
{
    return brotli_unavailable();
}

BrotliCompressor::~BrotliCompressor() = default;

ErrorOr<Bytes> BrotliCompressor::read_some(Bytes)
{
    return brotli_unavailable();
}

ErrorOr<size_t> BrotliCompressor::write_some(ReadonlyBytes)
{
    return brotli_unavailable();
}

bool BrotliCompressor::is_eof() const { return false; }
bool BrotliCompressor::is_open() const { return false; }
void BrotliCompressor::close() { }

ErrorOr<void> BrotliCompressor::finish()
{
    return brotli_unavailable();
}

}
