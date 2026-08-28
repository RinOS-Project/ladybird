/*
 * Copyright (c) 2024, Tim Flynn <trflynn89@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibCore/File.h>
#include <LibIPC/Decoder.h>
#include <LibIPC/Encoder.h>
#include <LibWeb/HTML/SelectedFile.h>

namespace Web::HTML {

/* File Portal descriptors are the only file-upload authority in RinOS.  Do
 * not let the IPC decoder turn an untrusted descriptor into an unbounded
 * allocation: HTML file inputs are intentionally capped at the same 128 MiB
 * ceiling as the browser upload transaction. */
static constexpr size_t max_portal_file_bytes = 128u * 1024u * 1024u;
static constexpr size_t portal_read_chunk_bytes = 64u * 1024u;

static ErrorOr<ByteBuffer> read_portal_file_contents(Core::File& file)
{
    auto declared_size = TRY(file.size());
    if (declared_size > max_portal_file_bytes)
        return Error::from_string_literal("File Portal object exceeds upload limit");
    TRY(file.seek(0, SeekMode::SetPosition));

    ByteBuffer contents;
    contents.ensure_capacity(declared_size);
    while (contents.size() < declared_size) {
        auto remaining = declared_size - contents.size();
        auto requested = remaining < portal_read_chunk_bytes
            ? remaining : portal_read_chunk_bytes;
        auto chunk = TRY(ByteBuffer::create_uninitialized(requested));
        auto bytes = TRY(file.read_some(chunk.span()));
        if (bytes.is_empty())
            return Error::from_string_literal("File Portal object changed while reading");
        TRY(contents.try_append(bytes));
    }

    /* A regular file can grow after fstat. Probe one byte so that the size
     * ceiling remains fail-closed instead of silently truncating the upload. */
    if (contents.size() == max_portal_file_bytes) {
        u8 extra_byte;
        auto extra = TRY(file.read_some(Bytes { &extra_byte, 1 }));
        if (!extra.is_empty())
            return Error::from_string_literal("File Portal object exceeds upload limit");
    }
    return contents;
}

ErrorOr<SelectedFile> SelectedFile::from_file_path(ByteString const& file_path)
{
    // https://html.spec.whatwg.org/multipage/input.html#file-upload-state-(type=file):concept-input-file-path
    // Filenames must not contain path components, even in the case that a user has selected an entire directory
    // hierarchy or multiple files with the same name from different directories.
    auto name = LexicalPath::basename(file_path);

    auto file = TRY(Core::File::open(file_path, Core::File::OpenMode::Read));
    return SelectedFile { move(name), IPC::File::adopt_file(move(file)) };
}

SelectedFile::SelectedFile(ByteString name, ByteBuffer contents)
    : m_name(move(name))
    , m_file_or_contents(move(contents))
{
}

SelectedFile::SelectedFile(ByteString name, IPC::File file)
    : m_name(move(name))
    , m_file_or_contents(move(file))
{
}

ByteBuffer SelectedFile::take_contents()
{
    VERIFY(m_file_or_contents.has<ByteBuffer>());
    return move(m_file_or_contents.get<ByteBuffer>());
}

}

template<>
ErrorOr<void> IPC::encode(Encoder& encoder, Web::HTML::SelectedFile const& file)
{
    TRY(encoder.encode(file.name()));
    TRY(encoder.encode(file.file_or_contents()));
    return {};
}

template<>
ErrorOr<Web::HTML::SelectedFile> IPC::decode(Decoder& decoder)
{
    auto name = TRY(decoder.decode<ByteString>());
    auto file_or_contents = TRY((decoder.decode<Variant<IPC::File, ByteBuffer>>()));

    ByteBuffer contents;

    if (file_or_contents.has<IPC::File>()) {
        auto file = TRY(Core::File::adopt_fd(file_or_contents.get<IPC::File>().take_fd(), Core::File::OpenMode::Read));
        contents = TRY(Web::HTML::read_portal_file_contents(*file));
    } else {
        contents = move(file_or_contents.get<ByteBuffer>());
    }

    return Web::HTML::SelectedFile { move(name), move(contents) };
}
