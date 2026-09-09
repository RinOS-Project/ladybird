/*
 * Copyright (c) 2023, Ali Mohammad Pur <mpfard@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteReader.h>
#include <AK/Debug.h>
#include <AK/FlyString.h>
#include <AK/NumericLimits.h>
#include <AK/Random.h>
#include <AK/Span.h>
#include <AK/Tuple.h>
#include <LibWasm/AbstractMachine/Configuration.h>
#include <LibWasm/Wasi.h>

// RinOS provides the POSIX-compatible functions below through its syscall
// ABI. Keep the ABI declarations explicit here so WASI does not depend on a
// host libc implementation when it is built for RinOS.
#if defined(AK_OS_RINOS)
#    include <dirent.h>
#    include <poll.h>
#    include <signal.h>
#    include <sys/random.h>
#    include <sys/syscall.h>
#    include <sys/socket.h>
#    include <utime.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace Wasm::Wasi::ABI {

template<typename T>
Wasm::Value CompatibleValue<T>::to_wasm_value() const
{
    return Wasm::Value(value);
}

template<typename T>
T deserialize(CompatibleValue<T> const& data)
{
    return deserialize<T>(Array { ReadonlyBytes { &data.value, sizeof(data.value) } });
}

template<typename T, size_t N>
void serialize(T const& value, Array<Bytes, N> bytes)
{
    if constexpr (IsEnum<T>)
        return serialize(to_underlying(value), move(bytes));
    else if constexpr (IsIntegral<T>)
        ReadonlyBytes { &value, sizeof(value) }.copy_to(bytes[0]);
    else if constexpr (IsSpecializationOf<T, DistinctNumeric>)
        return serialize(value.value(), move(bytes));
    else
        return value.serialize_into(move(bytes));
}

template<typename T, size_t N>
T deserialize(Array<ReadonlyBytes, N> const& bytes)
{
    if constexpr (IsEnum<T>) {
        return static_cast<T>(deserialize<UnderlyingType<T>>(bytes));
    } else if constexpr (IsIntegral<T>) {
        T value;
        ByteReader::load(bytes[0].data(), value);
        return value;
    } else if constexpr (IsSpecializationOf<T, DistinctNumeric>) {
        return deserialize<RemoveCVReference<decltype(T(0).value())>>(bytes);
    } else {
        return T::read_from(bytes);
    }
}

template<typename T>
CompatibleValue<T> to_compatible_value(Wasm::Value const& value)
{
    using Type = typename ToCompatibleValue<T>::Type;
    // Note: the type can't be something else, we've already checked before through the function type's runtime checker.
    auto converted_value = value.template to<Type>();
    return { .value = converted_value };
}

}

namespace Wasm::Wasi {

void ArgsSizes::serialize_into(Array<Bytes, 2> bytes) const
{
    ABI::serialize(count, Array { bytes[0] });
    ABI::serialize(size, Array { bytes[1] });
}

void EnvironSizes::serialize_into(Array<Bytes, 2> bytes) const
{
    ABI::serialize(count, Array { bytes[0] });
    ABI::serialize(size, Array { bytes[1] });
}

void SockRecvResult::serialize_into(Array<Bytes, 2> bytes) const
{
    ABI::serialize(size, Array { bytes[0] });
    ABI::serialize(roflags, Array { bytes[1] });
}

void ROFlags::serialize_into(Array<Bytes, 1> bytes) const
{
    ABI::serialize(data, Array { bytes[0] });
}

template<typename T>
void LittleEndian<T>::serialize_into(Array<Bytes, 1> bytes) const
{
    ABI::serialize(m_value, move(bytes));
}

template<typename T>
LittleEndian<T> LittleEndian<T>::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    auto swapped = ABI::deserialize<T>(bytes);
    return bit_cast<LittleEndian<T>>(swapped);
}

Rights Rights::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    Rights rights { .data = 0 };
    bytes[0].copy_to(rights.data.bytes());
    return rights;
}

void Rights::serialize_into(Array<Bytes, 1> bytes) const
{
    data.bytes().copy_to(bytes[0]);
}

void FDFlags::serialize_into(Array<Bytes, 1> bytes) const
{
    ReadonlyBytes { &data, sizeof(data) }.copy_to(bytes[0]);
}

FDFlags FDFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    FDFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

FSTFlags FSTFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    FSTFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

OFlags OFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    OFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

SDFlags SDFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    SDFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

void FDStat::serialize_into(Array<Bytes, 1> bytes) const
{
    auto data = bytes[0];
    ABI::serialize(fs_filetype, Array { data.slice(offsetof(FDStat, fs_filetype), sizeof(fs_filetype)) });
    ABI::serialize(fs_flags, Array { data.slice(offsetof(FDStat, fs_flags), sizeof(fs_flags)) });
    ABI::serialize(fs_rights_base, Array { data.slice(offsetof(FDStat, fs_rights_base), sizeof(fs_rights_base)) });
    ABI::serialize(fs_rights_inheriting, Array { data.slice(offsetof(FDStat, fs_rights_inheriting), sizeof(fs_rights_inheriting)) });
}

void PreStat::serialize_into(Array<Bytes, 1> bytes) const
{
    auto data = bytes[0];
    ABI::serialize(type, Array { data.slice(0, sizeof(type)) });
    if (type == PreOpenType::Dir)
        ABI::serialize(dir, Array { data.slice(offsetof(PreStat, dir), sizeof(dir)) });
    else
        VERIFY_NOT_REACHED();
}

void PreStatDir::serialize_into(Array<Bytes, 1> bytes) const
{
    ABI::serialize(pr_name_len, move(bytes));
}

void FileStat::serialize_into(Array<Bytes, 1> bytes) const
{
    auto data = bytes[0];
    ABI::serialize(dev, Array { data.slice(0, sizeof(dev)) });
    ABI::serialize(ino, Array { data.slice(offsetof(FileStat, ino), sizeof(ino)) });
    ABI::serialize(filetype, Array { data.slice(offsetof(FileStat, filetype), sizeof(filetype)) });
    ABI::serialize(nlink, Array { data.slice(offsetof(FileStat, nlink), sizeof(nlink)) });
    ABI::serialize(size, Array { data.slice(offsetof(FileStat, size), sizeof(size)) });
    ABI::serialize(atim, Array { data.slice(offsetof(FileStat, atim), sizeof(atim)) });
    ABI::serialize(mtim, Array { data.slice(offsetof(FileStat, mtim), sizeof(mtim)) });
    ABI::serialize(ctim, Array { data.slice(offsetof(FileStat, ctim), sizeof(ctim)) });
}

RIFlags RIFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    RIFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

LookupFlags LookupFlags::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    LookupFlags flags { .data = 0 };
    bytes[0].copy_to(flags.data.bytes());
    return flags;
}

CIOVec CIOVec::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    return CIOVec {
        .buf = ABI::deserialize<decltype(buf)>(Array { bytes[0].slice(offsetof(CIOVec, buf), sizeof(buf)) }),
        .buf_len = ABI::deserialize<decltype(buf_len)>(Array { bytes[0].slice(offsetof(CIOVec, buf_len), sizeof(buf_len)) }),
    };
}

IOVec IOVec::read_from(Array<ReadonlyBytes, 1> const& bytes)
{
    return IOVec {
        .buf = ABI::deserialize<decltype(buf)>(Array { bytes[0].slice(offsetof(IOVec, buf), sizeof(buf)) }),
        .buf_len = ABI::deserialize<decltype(buf_len)>(Array { bytes[0].slice(offsetof(IOVec, buf_len), sizeof(buf_len)) }),
    };
}

template<typename T>
static bool wasm_memory_range_is_valid(size_t memory_size, size_t address, size_t count)
{
    if (address > memory_size)
        return false;
    return count <= (memory_size - address) / sizeof(T);
}

template<typename T>
ErrorOr<Vector<T>> copy_typed_array(Configuration& configuration, Pointer<T> source, Size count)
{
    auto* memory = configuration.store().get(MemoryAddress { 0 });
    if (!memory)
        return Error::from_errno(ENOMEM);

    size_t address = source.value();
    if (!wasm_memory_range_is_valid<T>(memory->size(), address, count.value())) {
        return Error::from_errno(ENOBUFS);
    }

    Vector<T> values;
    TRY(values.try_ensure_capacity(count));
    for (Size i = 0; i < count; i += 1) {
        values.unchecked_append(T::read_from(Array { ReadonlyBytes { memory->data().bytes().slice(address, sizeof(T)) } }));
        address += sizeof(T);
    }

    return values;
}

template<typename T>
ErrorOr<void> copy_typed_value_to(Configuration& configuration, T const& value, Pointer<T> destination)
{
    auto* memory = configuration.store().get(MemoryAddress { 0 });
    if (!memory)
        return Error::from_errno(ENOMEM);

    size_t address = destination.value();
    if (!wasm_memory_range_is_valid<T>(memory->size(), address, 1)) {
        return Error::from_errno(ENOBUFS);
    }

    ABI::serialize(value, Array { Bytes { memory->data().bytes().slice(address, sizeof(T)) } });
    return {};
}

template<typename T>
ErrorOr<Span<T>> slice_typed_memory(Configuration& configuration, Pointer<T> source, Size count)
{
    auto* memory = configuration.store().get(MemoryAddress { 0 });
    if (!memory)
        return Error::from_errno(ENOMEM);

    size_t address = source.value();
    if (!wasm_memory_range_is_valid<T>(memory->size(), address, count.value()))
        return Error::from_errno(ENOBUFS);

    auto untyped_slice = memory->data().bytes().slice(address, sizeof(T) * count.value());
    return Span<T>(reinterpret_cast<T*>(untyped_slice.data()), count);
}

template<typename T>
ErrorOr<Span<T const>> slice_typed_memory(Configuration& configuration, ConstPointer<T> source, Size count)
{
    auto* memory = configuration.store().get(MemoryAddress { 0 });
    if (!memory)
        return Error::from_errno(ENOMEM);

    size_t address = source.value();
    if (!wasm_memory_range_is_valid<T>(memory->size(), address, count.value()))
        return Error::from_errno(ENOBUFS);

    auto untyped_slice = memory->data().bytes().slice(address, sizeof(T) * count.value());
    return Span<T const>(reinterpret_cast<T const*>(untyped_slice.data()), count);
}

static ErrorOr<size_t> copy_string_including_terminating_null(Configuration& configuration, StringView string, Pointer<u8> target)
{
    auto slice = TRY(slice_typed_memory(configuration, target, string.bytes().size() + 1));
    string.bytes().copy_to(slice);
    slice[string.bytes().size()] = 0;
    return slice.size();
}

static ErrorOr<size_t> copy_string_excluding_terminating_null(Configuration& configuration, StringView string, Pointer<u8> target, Size target_length)
{
    auto byte_count = min(string.bytes().size(), target_length);
    auto slice = TRY(slice_typed_memory(configuration, target, byte_count));
    string.bytes().copy_trimmed_to(slice);
    return byte_count;
}

static Errno errno_value_from_errno(int value);
static FileType file_type_of(struct stat const& buf);

#if defined(AK_OS_RINOS)
static Optional<Errno> validate_beneath_path(ReadonlyBytes path)
{
    // The kernel repeats these checks. Doing them on the exact WASI byte span
    // prevents ByteString's terminating NUL from changing what was validated.
    if (path.is_empty())
        return Errno::NoEntry;
    if (path.size() >= 256)
        return Errno::NameTooLong;
    if (path[0] == '/')
        return Errno::NotCapable;

    for (size_t index = 0; index < path.size(); ++index) {
        if (path[index] == 0)
            return Errno::Invalid;
    }
    for (size_t component_start = 0; component_start < path.size();) {
        while (component_start < path.size() && path[component_start] == '/')
            ++component_start;
        auto component_end = component_start;
        while (component_end < path.size() && path[component_end] != '/')
            ++component_end;
        if (component_end - component_start == 2
            && path[component_start] == '.'
            && path[component_start + 1] == '.')
            return Errno::NotCapable;
        component_start = component_end + 1;
    }
    return {};
}
#endif

Vector<AK::String> const& Implementation::arguments() const
{
    return cache.cached_arguments.ensure([&] {
        if (provide_arguments)
            return provide_arguments();
        return Vector<AK::String> {};
    });
}

Vector<AK::String> const& Implementation::environment() const
{
    return cache.cached_environment.ensure([&] {
        if (provide_environment)
            return provide_environment();
        return Vector<AK::String> {};
    });
}

Vector<Implementation::MappedPath> const& Implementation::preopened_directories() const
{
    return cache.cached_preopened_directories.ensure([&] {
        if (provide_preopened_directories)
            return provide_preopened_directories();
        return Vector<MappedPath> {};
    });
}

Implementation::DescriptorRights* Implementation::rights_for_fd(FD fd)
{
    return m_fd_rights.find(fd.value());
}

bool Implementation::has_right(FD fd, u64 right)
{
    auto* rights = rights_for_fd(fd);
    return rights && (rights->base.data.value() & right) == right;
}

void Implementation::install_rights(u32 fd, Rights base, Rights inheriting)
{
    if (auto* rights = m_fd_rights.find(fd)) {
        rights->base = base;
        rights->inheriting = inheriting;
        return;
    }
    m_fd_rights.insert(fd, DescriptorRights { base, inheriting });
}

Implementation::Descriptor Implementation::map_fd(FD fd)
{
    u32 fd_value = fd.value();
    if (auto* value = m_fd_map.find(fd_value))
        return value->downcast<Descriptor>();

    return UnmappedDescriptor(fd_value);
}

int Implementation::resolve_host_fd(FD fd)
{
    int resolved_fd = -1;
    map_fd(fd).visit(
        [&](PreopenedDirectoryDescriptor descriptor) {
            auto& entry = preopened_directories()[descriptor.value()];
            if (entry.opened_fd.has_value()) {
                resolved_fd = entry.opened_fd.value();
                return;
            }

            ByteString path = entry.host_path.string();
            auto opened_fd = open(path.characters(), O_DIRECTORY, 0);
            if (opened_fd >= 0)
                entry.opened_fd = opened_fd;
            resolved_fd = opened_fd;
        },
        [&](u32 host_fd) {
            if (host_fd > static_cast<u32>(NumericLimits<int>::max())) {
                errno = EBADF;
                return;
            }
            resolved_fd = static_cast<int>(host_fd);
        },
        [](UnmappedDescriptor) {
            errno = EBADF;
        });
    if (resolved_fd < 0 && errno <= 0)
        errno = EIO;
    return resolved_fd;
}

ErrorOr<Result<void>> Implementation::impl$args_get(Configuration& configuration, Pointer<Pointer<u8>> argv, Pointer<u8> argv_buf)
{
    UnderlyingPointerType raw_argv_buffer = argv_buf.value();
    UnderlyingPointerType raw_argv = argv.value();

    for (auto& entry : arguments()) {
        auto ptr = Pointer<u8> { raw_argv_buffer };
        auto byte_count = TRY(copy_string_including_terminating_null(configuration, entry.bytes_as_string_view(), ptr));
        raw_argv_buffer += byte_count;

        TRY(copy_typed_value_to(configuration, ptr, Pointer<Pointer<u8>> { raw_argv }));
        raw_argv += sizeof(ptr);
    }

    return Result<void> {};
}

ErrorOr<Result<ArgsSizes>> Implementation::impl$args_sizes_get(Configuration&)
{
    size_t count = 0;
    size_t total_size = 0;
    for (auto& entry : arguments()) {
        count += 1;
        total_size += entry.bytes().size() + 1; // 1 extra byte for terminating null.
    }

    return Result<ArgsSizes>(ArgsSizes {
        count,
        total_size,
    });
}

ErrorOr<Result<void>> Implementation::impl$environ_get(Configuration& configuration, Pointer<Pointer<u8>> environ, Pointer<u8> environ_buf)
{
    UnderlyingPointerType raw_environ_buffer = environ_buf.value();
    UnderlyingPointerType raw_environ = environ.value();

    for (auto& entry : environment()) {
        auto ptr = Pointer<u8> { raw_environ_buffer };
        auto byte_count = TRY(copy_string_including_terminating_null(configuration, entry.bytes_as_string_view(), ptr));
        raw_environ_buffer += byte_count;

        TRY(copy_typed_value_to(configuration, ptr, Pointer<Pointer<u8>> { raw_environ }));
        raw_environ += sizeof(ptr);
    }

    return Result<void> {};
}

ErrorOr<Result<EnvironSizes>> Implementation::impl$environ_sizes_get(Configuration&)
{
    size_t count = 0;
    size_t total_size = 0;
    for (auto& entry : environment()) {
        count += 1;
        total_size += entry.bytes().size() + 1; // 1 extra byte for terminating null.
    }

    return Result<EnvironSizes>(EnvironSizes {
        count,
        total_size,
    });
}

ErrorOr<void> Implementation::impl$proc_exit(Configuration&, ExitCode exit_code)
{
    return Error::from_errno(-static_cast<i32>(exit_code + 1));
}

ErrorOr<Result<void>> Implementation::impl$fd_close(Configuration&, FD fd)
{
    return map_fd(fd).visit(
        [&](u32 host_fd) -> Result<void> {
            if (close(bit_cast<i32>(host_fd)) != 0)
                return errno_value_from_errno(errno);
            // A host descriptor number may be reused immediately. Keep a
            // closed WASI descriptor unmapped so it cannot acquire authority
            // over an unrelated object that later receives the same number.
            m_fd_map.remove(fd.value());
            m_fd_rights.remove(fd.value());
            return {};
        },
        [&](PreopenedDirectoryDescriptor) -> Result<void> {
            return errno_value_from_errno(EISDIR);
        },
        [&](UnmappedDescriptor) -> Result<void> {
            return errno_value_from_errno(EBADF);
        });
}

ErrorOr<Result<Size>> Implementation::impl$fd_write(Configuration& configuration, FD fd, Pointer<CIOVec> iovs, Size iovs_len)
{
    if (!has_right(fd, 1ull << 6))
        return Errno::NotCapable;
    auto mapped_fd = map_fd(fd);
    if (!mapped_fd.has<u32>())
        return errno_value_from_errno(EBADF);

    u32 fd_value = mapped_fd.get<u32>();
    Size bytes_written = 0;
    for (auto& iovec : TRY(copy_typed_array(configuration, iovs, iovs_len))) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        auto result = write(fd_value, slice.data(), slice.size());
        if (result < 0)
            return errno_value_from_errno(errno);
        bytes_written += static_cast<Size>(result);
    }
    return bytes_written;
}

ErrorOr<Result<PreStat>> Implementation::impl$fd_prestat_get(Configuration&, FD fd)
{
    auto& paths = preopened_directories();
    return map_fd(fd).visit(
        [&](UnmappedDescriptor unmapped_fd) -> Result<PreStat> {
            // Map the new fd to the next available directory.
            if (m_first_unmapped_preopened_directory_index >= paths.size())
                return errno_value_from_errno(EBADF);

            auto index = m_first_unmapped_preopened_directory_index++;
            m_fd_map.insert(unmapped_fd.value(), PreopenedDirectoryDescriptor(index));
            install_rights(unmapped_fd.value(), Rights { .data = all_rights_mask }, Rights { .data = all_rights_mask });
            return PreStat {
                .type = PreOpenType::Dir,
                .dir = PreStatDir {
                    .pr_name_len = paths[index].mapped_path.string().bytes().size(),
                },
            };
        },
        [&](u32) -> Result<PreStat> {
            return errno_value_from_errno(EBADF);
        },
        [&](PreopenedDirectoryDescriptor fd) -> Result<PreStat> {
            return PreStat {
                .type = PreOpenType::Dir,
                .dir = PreStatDir {
                    .pr_name_len = paths[fd.value()].mapped_path.string().bytes().size(),
                },
            };
        });
}

ErrorOr<Result<void>> Implementation::impl$fd_prestat_dir_name(Configuration& configuration, FD fd, Pointer<u8> path, Size path_len)
{
    auto mapped_fd = map_fd(fd);
    if (!mapped_fd.has<PreopenedDirectoryDescriptor>())
        return errno_value_from_errno(EBADF);

    auto& entry = preopened_directories()[mapped_fd.get<PreopenedDirectoryDescriptor>().value()];
    auto byte_count = TRY(copy_string_excluding_terminating_null(configuration, entry.mapped_path.string().view(), path, path_len));
    if (byte_count < path_len.value())
        return errno_value_from_errno(ENOBUFS);

    return Result<void> {};
}

ErrorOr<Result<FileStat>> Implementation::impl$path_filestat_get(Configuration& configuration, FD fd, LookupFlags flags, ConstPointer<u8> path, Size path_len)
{
    if (!has_right(fd, 1ull << 18))
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);

    int options = 0;
    if (!flags.bits.symlink_follow)
        options |= AT_SYMLINK_NOFOLLOW;

    auto slice = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(slice); error.has_value())
        return Result<FileStat> { Errno { error.value() } };
#endif
    auto null_terminated_string = ByteString::copy(slice);

    struct stat stat_buf;
#if defined(AK_OS_RINOS)
    if (rin_fstatat_beneath(dir_fd, null_terminated_string.characters(), &stat_buf, options) < 0)
#else
    if (fstatat(dir_fd, null_terminated_string.characters(), &stat_buf, options) < 0)
#endif
        return errno_value_from_errno(errno);

    constexpr auto file_type_of = [](struct stat const& buf) {
        if (S_ISDIR(buf.st_mode))
            return FileType::Directory;
        if (S_ISCHR(buf.st_mode))
            return FileType::CharacterDevice;
        if (S_ISBLK(buf.st_mode))
            return FileType::BlockDevice;
        if (S_ISREG(buf.st_mode))
            return FileType::RegularFile;
        if (S_ISFIFO(buf.st_mode))
            return FileType::Unknown; // FIXME: FileType::Pipe is currently not present in WASI (but it should be) so we use Unknown for now.
        if (S_ISLNK(buf.st_mode))
            return FileType::SymbolicLink;
        if (S_ISSOCK(buf.st_mode))
            return FileType::SocketStream;
        return FileType::Unknown;
    };

    return Result(FileStat {
        .dev = stat_buf.st_dev,
        .ino = stat_buf.st_ino,
        .filetype = file_type_of(stat_buf),
        .nlink = stat_buf.st_nlink,
        .size = stat_buf.st_size,
        .atim = stat_buf.st_atime,
        .mtim = stat_buf.st_mtime,
        .ctim = stat_buf.st_ctime,
    });
}

ErrorOr<Result<void>> Implementation::impl$path_create_directory(Configuration& configuration, FD fd, Pointer<u8> path, Size path_len)
{
    if (!has_right(fd, 1ull << 9))
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);

    auto slice = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    auto null_terminated_string = ByteString::copy(slice);

#if defined(AK_OS_RINOS)
    if (rin_mkdirat_beneath(dir_fd, null_terminated_string.characters(), 0755) < 0)
#else
    if (mkdirat(dir_fd, null_terminated_string.characters(), 0755) < 0)
#endif
        return errno_value_from_errno(errno);

    return Result<void> {};
}

ErrorOr<Result<FD>> Implementation::impl$path_open(Configuration& configuration, FD fd, LookupFlags lookup_flags, Pointer<u8> path, Size path_len, OFlags o_flags, Rights fs_rights_base, Rights fs_rights_inheriting, FDFlags fd_flags)
{
    auto* parent_rights = rights_for_fd(fd);
    if (!parent_rights || (parent_rights->base.data.value() & (1ull << 13)) == 0)
        return Errno::NotCapable;
    if ((fs_rights_base.data.value() & ~all_rights_mask) != 0 || (fs_rights_inheriting.data.value() & ~all_rights_mask) != 0)
        return Errno::Invalid;
    if ((fs_rights_base.data.value() & ~parent_rights->inheriting.data.value()) != 0
        || (fs_rights_inheriting.data.value() & ~parent_rights->inheriting.data.value()) != 0)
        return Errno::NotCapable;
    if (o_flags.bits.creat && (parent_rights->base.data.value() & (1ull << 10)) == 0)
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);

    bool needs_read = fs_rights_base.bits.fd_read;
    bool needs_write = fs_rights_base.bits.fd_write;
    if (o_flags.bits.directory && needs_write)
        return Errno::Invalid;
    if ((o_flags.bits.creat || o_flags.bits.trunc || fd_flags.bits.append) && !needs_write)
        return Errno::Invalid;

    int open_flags = needs_write
        ? (needs_read ? O_RDWR : O_WRONLY)
        : O_RDONLY;
    if (fd_flags.bits.append)
        open_flags |= O_APPEND;
    if (fd_flags.bits.dsync)
        open_flags |= O_DSYNC;
    if (fd_flags.bits.nonblock)
        open_flags |= O_NONBLOCK;
    if (fd_flags.bits.rsync)
        open_flags |= O_RSYNC;
    if (fd_flags.bits.sync)
        open_flags |= O_SYNC;

    if (o_flags.bits.trunc)
        open_flags |= O_TRUNC;
    if (o_flags.bits.creat)
        open_flags |= O_CREAT;
    if (o_flags.bits.directory)
        open_flags |= O_DIRECTORY;
    if (o_flags.bits.excl)
        open_flags |= O_EXCL;

#if defined(AK_OS_RINOS)
    // OPENAT_BENEATH rejects every symlink in the kernel. O_NOFOLLOW is not
    // part of that syscall's intentionally small flag ABI, and passing it
    // would reject even an ordinary child path before VFS resolution.
    (void)lookup_flags;
#else
    if (!lookup_flags.bits.symlink_follow)
        open_flags |= O_NOFOLLOW;
#endif

    auto path_data = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(path_data); error.has_value())
        return Result<FD> { Errno { error.value() } };
#endif
    auto path_string = ByteString::copy(path_data);

    dbgln_if(WASI_FINE_GRAINED_DEBUG, "path_open: dir_fd={}, path={}, open_flags={}", dir_fd, path_string, open_flags);

#if defined(AK_OS_RINOS)
    int opened_fd = rin_openat_beneath(dir_fd, path_string.characters(), open_flags, 0644);
#else
    int opened_fd = openat(dir_fd, path_string.characters(), open_flags, 0644);
#endif
    if (opened_fd < 0)
        return errno_value_from_errno(errno);

    m_fd_map.insert(opened_fd, static_cast<u32>(opened_fd));
    install_rights(opened_fd, fs_rights_base, fs_rights_inheriting);

    return FD(opened_fd);
}

ErrorOr<Result<Timestamp>> Implementation::impl$clock_time_get(Configuration&, ClockID id, Timestamp precision)
{
    constexpr u64 nanoseconds_in_millisecond = 1000'000ull;
    constexpr u64 nanoseconds_in_second = 1000'000'000ull;

    clockid_t clock_id;
    switch (id) {
    case ClockID::Realtime:
        if (precision >= nanoseconds_in_millisecond)
            clock_id = CLOCK_REALTIME_COARSE;
        else
            clock_id = CLOCK_REALTIME;
        break;
    case ClockID::Monotonic:
        if (precision >= nanoseconds_in_millisecond)
            clock_id = CLOCK_MONOTONIC_COARSE;
        else
            clock_id = CLOCK_MONOTONIC;
        break;
    case ClockID::ProcessCPUTimeID:
    case ClockID::ThreadCPUTimeID:
        return Errno::NoSys;
    }

    struct timespec ts;
    if (clock_gettime(clock_id, &ts) < 0)
        return errno_value_from_errno(errno);
    if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= static_cast<long>(nanoseconds_in_second))
        return Errno::IO;

    auto seconds = static_cast<u64>(ts.tv_sec);
    auto nanoseconds = static_cast<u64>(ts.tv_nsec);
    if (seconds > (NumericLimits<u64>::max() - nanoseconds) / nanoseconds_in_second)
        return Errno::Overflow;

    return Result<Timestamp> { seconds * nanoseconds_in_second + nanoseconds };
}

ErrorOr<Result<FileStat>> Implementation::impl$fd_filestat_get(Configuration&, FD fd)
{
    if (!has_right(fd, 1ull << 21))
        return Errno::NotCapable;
    auto resolved_fd = resolve_host_fd(fd);
    if (resolved_fd < 0)
        return errno_value_from_errno(errno);

    struct stat stat_buf;
    if (fstat(resolved_fd, &stat_buf) < 0)
        return errno_value_from_errno(errno);

    constexpr auto file_type_of = [](struct stat const& buf) {
        if (S_ISDIR(buf.st_mode))
            return FileType::Directory;
        if (S_ISCHR(buf.st_mode))
            return FileType::CharacterDevice;
        if (S_ISBLK(buf.st_mode))
            return FileType::BlockDevice;
        if (S_ISREG(buf.st_mode))
            return FileType::RegularFile;
        if (S_ISFIFO(buf.st_mode))
            return FileType::Unknown; // no Pipe? :yakfused:
        if (S_ISLNK(buf.st_mode))
            return FileType::SymbolicLink;
        if (S_ISSOCK(buf.st_mode))
            return FileType::SocketDGram; // :shrug:
        return FileType::Unknown;
    };

    return Result(FileStat {
        .dev = stat_buf.st_dev,
        .ino = stat_buf.st_ino,
        .filetype = file_type_of(stat_buf),
        .nlink = stat_buf.st_nlink,
        .size = stat_buf.st_size,
        .atim = stat_buf.st_atime,
        .mtim = stat_buf.st_mtime,
        .ctim = stat_buf.st_ctime,
    });
}

ErrorOr<Result<void>> Implementation::impl$random_get(Configuration& configuration, Pointer<u8> buf, Size buf_len)
{
    auto buffer_slice = TRY(slice_typed_memory(configuration, buf, buf_len));
#if defined(AK_OS_RINOS)
    size_t offset = 0;
    u32 interruptions = 0;
    while (offset < buffer_slice.size()) {
        auto remaining = buffer_slice.size() - offset;
        auto received = getrandom(buffer_slice.data() + offset, remaining, 0);
        if (received > 0) {
            auto count = static_cast<size_t>(received);
            if (count <= remaining) {
                offset += count;
                continue;
            }
        }

        int error = EIO;
        if (received < 0) {
            error = errno;
            if (error <= 0)
                error = EIO;
            if (error == EINTR && ++interruptions <= 32)
                continue;
        }

        // WASI observes this caller-owned memory even after an errno result.
        // Do not leave a partial CSPRNG result (or stale linear-memory bytes)
        // visible when the RinOS syscall cannot complete the request.
        __builtin_memset(buffer_slice.data(), 0, buffer_slice.size());
        return errno_value_from_errno(error);
    }
#else
    fill_with_random(buffer_slice);
#endif

    return Result<void> {};
}

ErrorOr<Result<Size>> Implementation::impl$fd_read(Configuration& configuration, FD fd, Pointer<IOVec> iovs, Size iovs_len)
{
    if (!has_right(fd, 1ull << 1))
        return Errno::NotCapable;
    auto mapped_fd = map_fd(fd);
    if (!mapped_fd.has<u32>())
        return errno_value_from_errno(EBADF);

    u32 fd_value = mapped_fd.get<u32>();
    Size bytes_read = 0;
    for (auto& iovec : TRY(copy_typed_array(configuration, iovs, iovs_len))) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        auto result = read(fd_value, slice.data(), slice.size());
        if (result < 0)
            return errno_value_from_errno(errno);
        bytes_read += static_cast<Size>(result);
    }
    return bytes_read;
}

ErrorOr<Result<FDStat>> Implementation::impl$fd_fdstat_get(Configuration&, FD fd)
{
    auto* rights = rights_for_fd(fd);
    if (!rights)
        return errno_value_from_errno(EBADF);
    auto resolved_fd = resolve_host_fd(fd);
    if (resolved_fd < 0)
        return errno_value_from_errno(errno);

    struct stat stat_buf;
    if (fstat(resolved_fd, &stat_buf) < 0)
        return errno_value_from_errno(errno);
    auto native_flags = fcntl(resolved_fd, F_GETFL);
    if (native_flags < 0)
        return errno_value_from_errno(errno);
    FDFlags wasi_flags {};
    wasi_flags.bits.append = (native_flags & O_APPEND) != 0;
    // RinOS provides O_DSYNC/O_RSYNC as O_SYNC-strength durability. Expose
    // all three WASI facets when that stronger status is active.
    wasi_flags.bits.dsync = (native_flags & O_DSYNC) != 0;
    wasi_flags.bits.nonblock = (native_flags & O_NONBLOCK) != 0;
    wasi_flags.bits.rsync = (native_flags & O_RSYNC) != 0;
    wasi_flags.bits.sync = (native_flags & O_SYNC) != 0;

    return FDStat {
        .fs_filetype = file_type_of(stat_buf),
        .fs_flags = wasi_flags,
        .fs_rights_base = rights->base,
        .fs_rights_inheriting = rights->inheriting,
    };
}

ErrorOr<Result<FileSize>> Implementation::impl$fd_seek(Configuration&, FD fd, FileDelta offset, Whence whence)
{
    if (!has_right(fd, 1ull << 2))
        return Errno::NotCapable;
    auto mapped_fd = map_fd(fd);
    if (!mapped_fd.has<u32>())
        return errno_value_from_errno(EBADF);

    u32 fd_value = mapped_fd.get<u32>();
    auto result = lseek(fd_value, offset, static_cast<int>(whence));
    if (result < 0)
        return errno_value_from_errno(errno);
    return FileSize(result);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

ErrorOr<Result<Timestamp>> Implementation::impl$clock_res_get(Configuration&, ClockID id)
{
#if defined(AK_OS_RINOS)
    constexpr u64 nanoseconds_in_second = 1000'000'000ull;
    clockid_t clock_id;
    switch (id) {
    case ClockID::Realtime:
        clock_id = CLOCK_REALTIME;
        break;
    case ClockID::Monotonic:
        clock_id = CLOCK_MONOTONIC;
        break;
    case ClockID::ProcessCPUTimeID:
    case ClockID::ThreadCPUTimeID:
        return Errno::NoSys;
    }

    struct timespec resolution;
    if (clock_getres(clock_id, &resolution) < 0)
        return errno_value_from_errno(errno);
    if (resolution.tv_sec < 0 || resolution.tv_nsec < 0 || resolution.tv_nsec >= static_cast<long>(nanoseconds_in_second))
        return Errno::IO;

    auto seconds = static_cast<u64>(resolution.tv_sec);
    auto nanoseconds = static_cast<u64>(resolution.tv_nsec);
    if (seconds > (NumericLimits<u64>::max() - nanoseconds) / nanoseconds_in_second)
        return Errno::Overflow;
    return Result<Timestamp> { seconds * nanoseconds_in_second + nanoseconds };
#else
    (void)id;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_advise(Configuration&, FD fd, FileSize offset, FileSize len, Advice advice)
{
    if (!has_right(fd, 1ull << 7))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    if (offset.value() > static_cast<u64>(NumericLimits<off_t>::max())
        || len.value() > static_cast<u64>(NumericLimits<off_t>::max()))
        return Errno::Overflow;
    int native_advice;
    switch (advice) {
    case Advice::Normal: native_advice = POSIX_FADV_NORMAL; break;
    case Advice::Sequential: native_advice = POSIX_FADV_SEQUENTIAL; break;
    case Advice::Random: native_advice = POSIX_FADV_RANDOM; break;
    case Advice::WillNeed: native_advice = POSIX_FADV_WILLNEED; break;
    case Advice::DontNeed: native_advice = POSIX_FADV_DONTNEED; break;
    case Advice::NoReuse: native_advice = POSIX_FADV_NOREUSE; break;
    default: return Errno::Invalid;
    }
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    auto result = posix_fadvise(host_fd, static_cast<off_t>(offset.value()), static_cast<off_t>(len.value()), native_advice);
    if (result != 0)
        return errno_value_from_errno(result);
    return Result<void> {};
#else
    (void)offset;
    (void)len;
    (void)advice;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_allocate(Configuration&, FD fd, FileSize offset, FileSize len)
{
    if (!has_right(fd, 1ull << 8))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    if (offset.value() > static_cast<u64>(NumericLimits<off_t>::max())
        || len.value() > static_cast<u64>(NumericLimits<off_t>::max())
        || len.value() > static_cast<u64>(NumericLimits<off_t>::max()) - offset.value())
        return Errno::Overflow;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    auto result = posix_fallocate(host_fd, static_cast<off_t>(offset.value()), static_cast<off_t>(len.value()));
    if (result != 0)
        return errno_value_from_errno(result);
    return Result<void> {};
#else
    (void)offset;
    (void)len;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_datasync(Configuration&, FD fd)
{
    if (!has_right(fd, 1ull << 0))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    if (fdatasync(host_fd) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)fd;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_fdstat_set_flags(Configuration&, FD fd, FDFlags fd_flags)
{
    if (!has_right(fd, 1ull << 3))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    constexpr u16 wasi_append_flag = 1u << 0;
    constexpr u16 wasi_dsync_flag = 1u << 1;
    constexpr u16 wasi_nonblock_flag = 1u << 2;
    constexpr u16 wasi_rsync_flag = 1u << 3;
    constexpr u16 wasi_sync_flag = 1u << 4;
    constexpr u16 supported_wasi_flags = wasi_append_flag | wasi_dsync_flag
        | wasi_nonblock_flag | wasi_rsync_flag | wasi_sync_flag;
    if ((fd_flags.data.value() & ~supported_wasi_flags) != 0)
        return Errno::NotSupported;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    auto native_flags = fcntl(host_fd, F_GETFL);
    if (native_flags < 0)
        return errno_value_from_errno(errno);
    native_flags &= ~(O_APPEND | O_DSYNC | O_NONBLOCK | O_RSYNC | O_SYNC);
    if (fd_flags.bits.append)
        native_flags |= O_APPEND;
    if (fd_flags.bits.dsync)
        native_flags |= O_DSYNC;
    if (fd_flags.bits.nonblock)
        native_flags |= O_NONBLOCK;
    if (fd_flags.bits.rsync)
        native_flags |= O_RSYNC;
    if (fd_flags.bits.sync)
        native_flags |= O_SYNC;
    if (fcntl(host_fd, F_SETFL, native_flags) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)fd;
    (void)fd_flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_fdstat_set_rights(Configuration&, FD fd, Rights fs_rights_base, Rights fs_rights_inheriting)
{
    auto* current = rights_for_fd(fd);
    if (!current)
        return errno_value_from_errno(EBADF);

    auto base = fs_rights_base.data.value();
    auto inheriting = fs_rights_inheriting.data.value();
    if ((base & ~all_rights_mask) != 0 || (inheriting & ~all_rights_mask) != 0)
        return Errno::Invalid;
    if ((base & ~current->base.data.value()) != 0 || (inheriting & ~current->inheriting.data.value()) != 0)
        return Errno::NotCapable;

    install_rights(fd.value(), fs_rights_base, fs_rights_inheriting);
    return Result<void> {};
}
ErrorOr<Result<void>> Implementation::impl$fd_filestat_set_size(Configuration&, FD fd, FileSize size)
{
    if (!has_right(fd, 1ull << 22))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto requested_size = size.value();
    if (requested_size > static_cast<u64>(NumericLimits<off_t>::max()))
        return Errno::Overflow;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    if (ftruncate(host_fd, static_cast<off_t>(requested_size)) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)fd;
    (void)size;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_filestat_set_times(Configuration&, FD fd, Timestamp atim, Timestamp mtim, FSTFlags flags)
{
    if (!has_right(fd, 1ull << 23))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    constexpr u16 known_flags = 0x0f;
    if ((flags.data.value() & ~known_flags) != 0
        || (flags.bits.atim && flags.bits.atim_now)
        || (flags.bits.mtim && flags.bits.mtim_now))
        return Errno::Invalid;
    constexpr u64 nanoseconds_per_second = 1'000'000'000ull;
    auto to_timespec = [&](Timestamp timestamp, struct timespec& output) {
        auto nanoseconds = timestamp.value();
        auto seconds = nanoseconds / nanoseconds_per_second;
        if (seconds > static_cast<u64>(NumericLimits<time_t>::max()))
            return false;
        output.tv_sec = static_cast<time_t>(seconds);
        output.tv_nsec = static_cast<long>(nanoseconds % nanoseconds_per_second);
        return true;
    };
    struct timespec times[2] {};
    if (flags.bits.atim_now)
        times[0].tv_nsec = UTIME_NOW;
    else if (flags.bits.atim) {
        if (!to_timespec(atim, times[0]))
            return Errno::Overflow;
    } else
        times[0].tv_nsec = UTIME_OMIT;
    if (flags.bits.mtim_now)
        times[1].tv_nsec = UTIME_NOW;
    else if (flags.bits.mtim) {
        if (!to_timespec(mtim, times[1]))
            return Errno::Overflow;
    } else
        times[1].tv_nsec = UTIME_OMIT;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    if (futimens(host_fd, times) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)atim;
    (void)mtim;
    (void)flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<Size>> Implementation::impl$fd_pread(Configuration& configuration, FD fd, Pointer<IOVec> iovs, Size iovs_len, FileSize offset)
{
    if (!has_right(fd, 1ull << 1) || !has_right(fd, 1ull << 2))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto requested_offset = offset.value();
    if (requested_offset > static_cast<u64>(NumericLimits<off_t>::max()))
        return Errno::Overflow;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);

    auto iovec_array = TRY(copy_typed_array(configuration, iovs, iovs_len));
    u64 requested_bytes = 0;
    for (auto const& iovec : iovec_array) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        (void)slice;
        if (iovec.buf_len.value() > NumericLimits<u32>::max() - requested_bytes)
            return Errno::Overflow;
        requested_bytes += iovec.buf_len.value();
    }
    if (requested_bytes > static_cast<u64>(NumericLimits<off_t>::max()) - requested_offset)
        return Errno::Overflow;

    u64 bytes_read = 0;
    for (auto const& iovec : iovec_array) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        auto result = pread(host_fd, slice.data(), slice.size(),
            static_cast<off_t>(requested_offset + bytes_read));
        if (result < 0)
            return errno_value_from_errno(errno);
        if (static_cast<size_t>(result) > slice.size())
            return Errno::IO;
        bytes_read += static_cast<size_t>(result);
        if (static_cast<size_t>(result) < slice.size())
            break;
    }
    return Size(static_cast<u32>(bytes_read));
#else
    (void)configuration;
    (void)fd;
    (void)iovs;
    (void)iovs_len;
    (void)offset;
    return Errno::NoSys;
#endif
}

ErrorOr<Result<Size>> Implementation::impl$fd_pwrite(Configuration& configuration, FD fd, Pointer<CIOVec> iovs, Size iovs_len, FileSize offset)
{
    if (!has_right(fd, 1ull << 6) || !has_right(fd, 1ull << 2))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto requested_offset = offset.value();
    if (requested_offset > static_cast<u64>(NumericLimits<off_t>::max()))
        return Errno::Overflow;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);

    auto iovec_array = TRY(copy_typed_array(configuration, iovs, iovs_len));
    u64 requested_bytes = 0;
    for (auto const& iovec : iovec_array) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        (void)slice;
        if (iovec.buf_len.value() > NumericLimits<u32>::max() - requested_bytes)
            return Errno::Overflow;
        requested_bytes += iovec.buf_len.value();
    }
    if (requested_bytes > static_cast<u64>(NumericLimits<off_t>::max()) - requested_offset)
        return Errno::Overflow;

    u64 bytes_written = 0;
    for (auto const& iovec : iovec_array) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        auto result = pwrite(host_fd, slice.data(), slice.size(),
            static_cast<off_t>(requested_offset + bytes_written));
        if (result < 0)
            return errno_value_from_errno(errno);
        if (static_cast<size_t>(result) > slice.size())
            return Errno::IO;
        bytes_written += static_cast<size_t>(result);
        if (static_cast<size_t>(result) < slice.size())
            break;
    }
    return Size(static_cast<u32>(bytes_written));
#else
    (void)configuration;
    (void)fd;
    (void)iovs;
    (void)iovs_len;
    (void)offset;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<Size>> Implementation::impl$fd_readdir(Configuration& configuration, FD fd, Pointer<u8> buf, Size buf_len, DirCookie cookie)
{
    if (!has_right(fd, 1ull << 14))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    struct stat stat_buf;
    if (fstat(host_fd, &stat_buf) < 0)
        return errno_value_from_errno(errno);
    if (!S_ISDIR(stat_buf.st_mode))
        return Errno::NotDirectory;

    auto buffer = TRY(slice_typed_memory(configuration, buf, buf_len));
    if (static_cast<u64>(cookie) > static_cast<u64>(NumericLimits<long>::max()))
        return Errno::Overflow;
    auto duplicate_fd = dup(host_fd);
    if (duplicate_fd < 0)
        return errno_value_from_errno(errno);
    auto* directory = fdopendir(duplicate_fd);
    if (!directory) {
        auto saved_errno = errno;
        close(duplicate_fd);
        return errno_value_from_errno(saved_errno);
    }
    auto fail_and_clear = [&](int error) -> ErrorOr<Result<Size>> {
        if (!buffer.is_empty())
            __builtin_memset(buffer.data(), 0, buffer.size());
        auto saved_errno = error > 0 ? error : EIO;
        closedir(directory);
        return errno_value_from_errno(saved_errno);
    };
    errno = 0;
    seekdir(directory, static_cast<long>(static_cast<u64>(cookie)));
    if (errno != 0)
        return fail_and_clear(errno);

    auto file_type_of_dirent = [](unsigned char native_type) {
        switch (native_type) {
        case DT_BLK: return FileType::BlockDevice;
        case DT_CHR: return FileType::CharacterDevice;
        case DT_DIR: return FileType::Directory;
        case DT_REG: return FileType::RegularFile;
        case DT_LNK: return FileType::SymbolicLink;
        case DT_SOCK: return FileType::SocketStream;
        default: return FileType::Unknown;
        }
    };
    size_t offset = 0;
    while (offset < buffer.size()) {
        errno = 0;
        auto entry_cookie = telldir(directory);
        if (entry_cookie < 0)
            return fail_and_clear(errno);
        auto* entry = readdir(directory);
        if (!entry) {
            if (errno != 0)
                return fail_and_clear(errno);
            break;
        }
        size_t name_length = 0;
        while (name_length < NAME_MAX + 1 && entry->d_name[name_length] != '\0')
            ++name_length;
        if (name_length > NAME_MAX)
            return fail_and_clear(EIO);
        if (name_length > NumericLimits<u32>::max())
            return fail_and_clear(EOVERFLOW);
        if (sizeof(DirEnt) > buffer.size() - offset
            || name_length > buffer.size() - offset - sizeof(DirEnt)) {
            errno = 0;
            seekdir(directory, entry_cookie);
            if (errno != 0)
                return fail_and_clear(errno);
            if (offset == 0)
                return fail_and_clear(EOVERFLOW);
            break;
        }
        errno = 0;
        auto next_cookie = telldir(directory);
        if (next_cookie < 0)
            return fail_and_clear(errno);
        auto* header = buffer.data() + offset;
        ABI::serialize(static_cast<u64>(next_cookie), Array { Bytes { header + 0, sizeof(u64) } });
        ABI::serialize(static_cast<u64>(entry->d_ino), Array { Bytes { header + 8, sizeof(u64) } });
        ABI::serialize(static_cast<u32>(name_length), Array { Bytes { header + 16, sizeof(u32) } });
        header[20] = static_cast<u8>(file_type_of_dirent(entry->d_type));
        header[21] = 0;
        header[22] = 0;
        header[23] = 0;
        ReadonlyBytes { reinterpret_cast<u8 const*>(entry->d_name), name_length }.copy_to(Bytes { buffer.data() + offset + sizeof(DirEnt), name_length });
        offset += sizeof(DirEnt) + name_length;
    }
    if (closedir(directory) < 0) {
        if (!buffer.is_empty())
            __builtin_memset(buffer.data(), 0, buffer.size());
        return errno_value_from_errno(errno);
    }
    return Size(static_cast<u32>(offset));
#else
    (void)configuration;
    (void)buf;
    (void)buf_len;
    (void)cookie;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$fd_renumber(Configuration&, FD from, FD to)
{
    auto source = map_fd(from);
    if (source.has<UnmappedDescriptor>())
        return errno_value_from_errno(EBADF);
    auto* source_rights = rights_for_fd(from);
    if (!source_rights)
        return errno_value_from_errno(EBADF);
    auto rights_to_transfer = *source_rights;
    if (from == to)
        return Result<void> {};

    auto target = map_fd(to);
    if (target.has<u32>()) {
        auto target_host_fd = target.get<u32>();
        bool aliases_source = source.has<u32>() && source.get<u32>() == target_host_fd;
        if (!aliases_source && close(static_cast<int>(target_host_fd)) < 0)
            return errno_value_from_errno(errno);
    }
    m_fd_map.remove(to.value());
    m_fd_rights.remove(to.value());
    m_fd_map.remove(from.value());
    m_fd_rights.remove(from.value());
    source.visit(
        [&](u32 host_fd) { m_fd_map.insert(to.value(), host_fd); },
        [&](PreopenedDirectoryDescriptor directory) { m_fd_map.insert(to.value(), directory); },
        [](UnmappedDescriptor) { });
    install_rights(to.value(), rights_to_transfer.base, rights_to_transfer.inheriting);
    return Result<void> {};
}
ErrorOr<Result<void>> Implementation::impl$fd_sync(Configuration&, FD fd)
{
    if (!has_right(fd, 1ull << 4))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    if (fsync(host_fd) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)fd;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<FileSize>> Implementation::impl$fd_tell(Configuration&, FD fd)
{
    if (!has_right(fd, 1ull << 5))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    auto offset = lseek(host_fd, 0, SEEK_CUR);
    if (offset < 0)
        return errno_value_from_errno(errno);
    return Result<FileSize> { static_cast<u64>(offset) };
#else
    (void)fd;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$path_filestat_set_times(Configuration& configuration, FD fd, LookupFlags lookup_flags, Pointer<u8> path, Size path_len, Timestamp atim, Timestamp mtim, FSTFlags flags)
{
    if (!has_right(fd, 1ull << 20))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);
    auto slice = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    constexpr u16 known_flags = 0x0f;
    if ((flags.data.value() & ~known_flags) != 0
        || (flags.bits.atim && flags.bits.atim_now)
        || (flags.bits.mtim && flags.bits.mtim_now))
        return Errno::Invalid;
    constexpr u64 nanoseconds_per_second = 1'000'000'000ull;
    auto to_timespec = [&](Timestamp timestamp, struct timespec& output) {
        auto nanoseconds = timestamp.value();
        auto seconds = nanoseconds / nanoseconds_per_second;
        if (seconds > static_cast<u64>(NumericLimits<time_t>::max()))
            return false;
        output.tv_sec = static_cast<time_t>(seconds);
        output.tv_nsec = static_cast<long>(nanoseconds % nanoseconds_per_second);
        return true;
    };
    struct timespec times[2] {};
    if (flags.bits.atim_now)
        times[0].tv_nsec = UTIME_NOW;
    else if (flags.bits.atim) {
        if (!to_timespec(atim, times[0]))
            return Errno::Overflow;
    } else
        times[0].tv_nsec = UTIME_OMIT;
    if (flags.bits.mtim_now)
        times[1].tv_nsec = UTIME_NOW;
    else if (flags.bits.mtim) {
        if (!to_timespec(mtim, times[1]))
            return Errno::Overflow;
    } else
        times[1].tv_nsec = UTIME_OMIT;
    auto path_string = ByteString::copy(slice);
    auto native_flags = lookup_flags.bits.symlink_follow ? 0 : AT_SYMLINK_NOFOLLOW;
    if (utimensat(dir_fd, path_string.characters(), times, native_flags) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)configuration;
    (void)fd;
    (void)lookup_flags;
    (void)path;
    (void)path_len;
    (void)atim;
    (void)mtim;
    (void)flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$path_link(Configuration& configuration, FD fd, LookupFlags lookup_flags, Pointer<u8> old_path, Size old_path_len, FD new_fd, Pointer<u8> new_path, Size new_path_len)
{
    if (!has_right(fd, 1ull << 11) || !has_right(new_fd, 1ull << 12))
        return Errno::NotCapable;
    auto source_fd = resolve_host_fd(fd);
    auto destination_fd = resolve_host_fd(new_fd);
    if (source_fd < 0 || destination_fd < 0)
        return errno_value_from_errno(errno);
    auto old_slice = TRY(slice_typed_memory(configuration, old_path, old_path_len));
    auto new_slice = TRY(slice_typed_memory(configuration, new_path, new_path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(old_slice); error.has_value())
        return Result<void> { Errno { error.value() } };
    if (auto error = validate_beneath_path(new_slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    auto old_string = ByteString::copy(old_slice);
    auto new_string = ByteString::copy(new_slice);
    auto native_flags = lookup_flags.bits.symlink_follow ? AT_SYMLINK_FOLLOW : 0;
    if (linkat(source_fd, old_string.characters(), destination_fd, new_string.characters(), native_flags) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
}
ErrorOr<Result<Size>> Implementation::impl$path_readlink(Configuration& configuration, FD fd, LookupFlags, Pointer<u8> path, Size path_len, Pointer<u8> buf, Size buf_len)
{
    if (!has_right(fd, 1ull << 15))
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);
    auto path_slice = TRY(slice_typed_memory(configuration, path, path_len));
    auto output = TRY(slice_typed_memory(configuration, buf, buf_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(path_slice); error.has_value())
        return Result<Size> { Errno { error.value() } };
#endif
    if (output.is_empty())
        return Errno::Invalid;
    auto path_string = ByteString::copy(path_slice);
    auto result = readlinkat(dir_fd, path_string.characters(), reinterpret_cast<char*>(output.data()), output.size());
    if (result < 0)
        return errno_value_from_errno(errno);
    if (static_cast<u64>(result) > NumericLimits<u32>::max())
        return Errno::Overflow;
    return Size(static_cast<u32>(result));
}
ErrorOr<Result<void>> Implementation::impl$path_remove_directory(Configuration& configuration, FD fd, Pointer<u8> path, Size path_len)
{
    if (!has_right(fd, 1ull << 25))
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);
    auto slice = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    auto path_string = ByteString::copy(slice);
    if (unlinkat(dir_fd, path_string.characters(), AT_REMOVEDIR) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
}
ErrorOr<Result<void>> Implementation::impl$path_rename(Configuration& configuration, FD old_fd, Pointer<u8> old_path, Size old_path_len, FD new_fd, Pointer<u8> new_path, Size new_path_len)
{
    if (!has_right(old_fd, 1ull << 16) || !has_right(new_fd, 1ull << 17))
        return Errno::NotCapable;
    auto source_fd = resolve_host_fd(old_fd);
    auto destination_fd = resolve_host_fd(new_fd);
    if (source_fd < 0 || destination_fd < 0)
        return errno_value_from_errno(errno);
    auto old_slice = TRY(slice_typed_memory(configuration, old_path, old_path_len));
    auto new_slice = TRY(slice_typed_memory(configuration, new_path, new_path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(old_slice); error.has_value())
        return Result<void> { Errno { error.value() } };
    if (auto error = validate_beneath_path(new_slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    auto old_string = ByteString::copy(old_slice);
    auto new_string = ByteString::copy(new_slice);
    if (renameat(source_fd, old_string.characters(), destination_fd, new_string.characters()) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
}
ErrorOr<Result<void>> Implementation::impl$path_symlink(Configuration& configuration, Pointer<u8> old_path, Size old_path_len, FD new_fd, Pointer<u8> new_path, Size new_path_len)
{
    if (!has_right(new_fd, 1ull << 24))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    // RinOS keeps preopen traversal symlink-free. Allowing a guest-created
    // link here would invalidate the beneath namespace invariant used by all
    // other WASI path owners, so this is an explicit policy result rather
    // than an unimplemented host fallback.
    (void)configuration;
    (void)old_path;
    (void)old_path_len;
    (void)new_path;
    (void)new_path_len;
    return Errno::NotCapable;
#else
    auto destination_fd = resolve_host_fd(new_fd);
    if (destination_fd < 0)
        return errno_value_from_errno(errno);
    auto target_slice = TRY(slice_typed_memory(configuration, old_path, old_path_len));
    auto link_slice = TRY(slice_typed_memory(configuration, new_path, new_path_len));
    if (target_slice.is_empty() || link_slice.is_empty())
        return Errno::Invalid;
    for (auto byte : target_slice)
        if (byte == 0)
            return Errno::Invalid;
    for (auto byte : link_slice)
        if (byte == 0)
            return Errno::Invalid;
    auto target_string = ByteString::copy(target_slice);
    auto link_string = ByteString::copy(link_slice);
    if (symlinkat(target_string.characters(), destination_fd, link_string.characters()) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#endif
}
ErrorOr<Result<void>> Implementation::impl$path_unlink_file(Configuration& configuration, FD fd, Pointer<u8> path, Size path_len)
{
    if (!has_right(fd, 1ull << 26))
        return Errno::NotCapable;
    auto dir_fd = resolve_host_fd(fd);
    if (dir_fd < 0)
        return errno_value_from_errno(errno);
    auto slice = TRY(slice_typed_memory(configuration, path, path_len));
#if defined(AK_OS_RINOS)
    if (auto error = validate_beneath_path(slice); error.has_value())
        return Result<void> { Errno { error.value() } };
#endif
    auto path_string = ByteString::copy(slice);
    if (unlinkat(dir_fd, path_string.characters(), 0) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
}
ErrorOr<Result<Size>> Implementation::impl$poll_oneoff(Configuration& configuration, ConstPointer<Subscription> in, Pointer<Event> out, Size nsubscriptions)
{
    if (nsubscriptions.value() == 0 || nsubscriptions.value() > 64)
        return Errno::TooBig;
#if defined(AK_OS_RINOS)
    auto count = nsubscriptions.value();
    auto subscriptions = TRY(copy_typed_array(configuration, Pointer<Subscription> { in.value() }, nsubscriptions));
    auto events = TRY(slice_typed_memory(configuration, out, nsubscriptions));
    struct pollfd poll_fds[64] {};
    u32 poll_indexes[64];
    bool is_clock[64] {};
    bool clock_absolute[64] {};
    ClockID clock_ids[64] {};
    u64 clock_start[64] {};
    u64 clock_timeout[64] {};
    for (u32 index = 0; index < count; ++index)
        poll_indexes[index] = NumericLimits<u32>::max();

    auto native_clock_id = [](ClockID id) -> Optional<clockid_t> {
        switch (id) {
        case ClockID::Realtime: return CLOCK_REALTIME;
        case ClockID::Monotonic: return CLOCK_MONOTONIC;
        case ClockID::ProcessCPUTimeID:
        case ClockID::ThreadCPUTimeID: return {};
        }
        return {};
    };
    auto read_clock = [&](ClockID id) -> ErrorOr<u64> {
        auto native_id = native_clock_id(id);
        if (!native_id.has_value())
            return Error::from_errno(EINVAL);
        struct timespec now {};
        if (clock_gettime(native_id.value(), &now) < 0)
            return Error::from_errno(errno);
        if (now.tv_sec < 0 || now.tv_nsec < 0 || now.tv_nsec >= 1'000'000'000)
            return Error::from_errno(EOVERFLOW);
        auto seconds = static_cast<u64>(now.tv_sec);
        auto nanos = static_cast<u64>(now.tv_nsec);
        if (seconds > (NumericLimits<u64>::max() - nanos) / 1'000'000'000ull)
            return Error::from_errno(EOVERFLOW);
        return seconds * 1'000'000'000ull + nanos;
    };

    u32 poll_count = 0;
    for (u32 index = 0; index < count; ++index) {
        auto const& subscription = subscriptions[index];
        switch (subscription.type) {
        case EventType::Clock: {
            if ((subscription.u.clock.flags.data.value() & ~1u) != 0)
                return Errno::Invalid;
            auto now = TRY(read_clock(subscription.u.clock.id));
            is_clock[index] = true;
            clock_absolute[index] = subscription.u.clock.flags.bits.subscription_clock_abstime;
            clock_ids[index] = subscription.u.clock.id;
            clock_start[index] = now;
            clock_timeout[index] = subscription.u.clock.timeout.value();
            break;
        }
        case EventType::FDRead:
        case EventType::FDWrite: {
            auto watched_fd = subscription.type == EventType::FDRead
                ? subscription.u.fd_read.file_descriptor
                : subscription.u.fd_write.file_descriptor;
            if (!has_right(watched_fd, 1ull << 27))
                return Errno::NotCapable;
            if (poll_count >= 64)
                return Errno::TooBig;
            auto host_fd = resolve_host_fd(watched_fd);
            if (host_fd < 0)
                return errno_value_from_errno(errno);
            poll_indexes[index] = poll_count;
            poll_fds[poll_count].fd = host_fd;
            poll_fds[poll_count].events = subscription.type == EventType::FDRead ? POLLIN : POLLOUT;
            ++poll_count;
            break;
        }
        default:
            return Errno::Invalid;
        }
    }

    for (;;) {
        u64 minimum_wait = NumericLimits<u64>::max();
        bool have_clock = false;
        for (u32 index = 0; index < count; ++index) {
            if (!is_clock[index])
                continue;
            have_clock = true;
            auto now = TRY(read_clock(clock_ids[index]));
            u64 wait = 0;
            if (clock_absolute[index])
                wait = clock_timeout[index] > now ? clock_timeout[index] - now : 0;
            else {
                auto elapsed = now >= clock_start[index] ? now - clock_start[index] : 0;
                wait = clock_timeout[index] > elapsed ? clock_timeout[index] - elapsed : 0;
            }
            if (wait < minimum_wait)
                minimum_wait = wait;
        }
        int timeout_ms = -1;
        if (have_clock) {
            constexpr u64 nanos_per_millisecond = 1'000'000ull;
            constexpr u64 max_timeout_ms = static_cast<u64>(NumericLimits<int>::max());
            auto rounded = minimum_wait > NumericLimits<u64>::max() - (nanos_per_millisecond - 1)
                ? NumericLimits<u64>::max()
                : minimum_wait + nanos_per_millisecond - 1;
            auto milliseconds = rounded / nanos_per_millisecond;
            timeout_ms = milliseconds > max_timeout_ms ? NumericLimits<int>::max() : static_cast<int>(milliseconds);
        }
        auto poll_result = poll(poll_fds, poll_count, timeout_ms);
        if (poll_result < 0)
            return errno_value_from_errno(errno);

        u32 event_count = 0;
        for (u32 index = 0; index < count; ++index) {
            auto const& subscription = subscriptions[index];
            bool ready = false;
            Errno event_errno = Errno::Success;
            EventRWFlags event_flags { .data = 0 };
            if (is_clock[index]) {
                auto now = TRY(read_clock(clock_ids[index]));
                if (clock_absolute[index])
                    ready = now >= clock_timeout[index];
                else
                    ready = now >= clock_start[index] && now - clock_start[index] >= clock_timeout[index];
            } else {
                auto revents = poll_fds[poll_indexes[index]].revents;
                if ((revents & POLLNVAL) != 0) {
                    ready = true;
                    event_errno = Errno::BadF;
                } else if ((revents & POLLERR) != 0) {
                    ready = true;
                    event_errno = Errno::IO;
                } else if ((revents & (POLLIN | POLLOUT | POLLHUP)) != 0) {
                    ready = true;
                    event_flags.bits.fd_readwrite_hangup = (revents & POLLHUP) != 0;
                }
            }
            if (!ready)
                continue;
            Event event {};
            event.userdata = subscription.userdata;
            event.errno_ = event_errno;
            event.type = subscription.type;
            event.fd_readwrite.nbytes = FileSize(0);
            event.fd_readwrite.flags = event_flags;
            events[event_count++] = event;
        }
        if (event_count != 0)
            return Size(event_count);
    }
#else
    (void)configuration;
    (void)in;
    (void)out;
    (void)nsubscriptions;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$proc_raise(Configuration&, Signal signal)
{
#if defined(AK_OS_RINOS)
    int native_signal;
    switch (signal) {
    case Signal::None: native_signal = 0; break;
    case Signal::HUP: native_signal = SIGHUP; break;
    case Signal::INT: native_signal = SIGINT; break;
    case Signal::QUIT: native_signal = SIGQUIT; break;
    case Signal::ILL: native_signal = SIGILL; break;
    case Signal::TRAP: native_signal = SIGTRAP; break;
    case Signal::ABRT: native_signal = SIGABRT; break;
    case Signal::BUS: native_signal = SIGBUS; break;
    case Signal::FPE: native_signal = SIGFPE; break;
    case Signal::KILL: native_signal = SIGKILL; break;
    case Signal::USR1: native_signal = SIGUSR1; break;
    case Signal::SEGV: native_signal = SIGSEGV; break;
    case Signal::USR2: native_signal = SIGUSR2; break;
    case Signal::PIPE: native_signal = SIGPIPE; break;
    case Signal::ALRM: native_signal = SIGALRM; break;
    case Signal::TERM: native_signal = SIGTERM; break;
    case Signal::CHLD: native_signal = SIGCHLD; break;
    case Signal::CONT: native_signal = SIGCONT; break;
    case Signal::STOP: native_signal = SIGSTOP; break;
    case Signal::TSTP: native_signal = SIGTSTP; break;
    case Signal::TTIN: native_signal = SIGTTIN; break;
    case Signal::TTOU: native_signal = SIGTTOU; break;
    case Signal::URG: native_signal = SIGURG; break;
    case Signal::XCPU: native_signal = SIGXCPU; break;
    case Signal::XFSZ: native_signal = SIGXFSZ; break;
    case Signal::VTALRM: native_signal = SIGVTALRM; break;
    case Signal::PROF: native_signal = SIGPROF; break;
    case Signal::WINCH: native_signal = SIGWINCH; break;
    case Signal::POLL: native_signal = SIGPOLL; break;
    case Signal::PWR: native_signal = SIGPWR; break;
    case Signal::SYS: native_signal = SIGSYS; break;
    default: return Errno::Invalid;
    }
    if (raise(native_signal) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)signal;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$sched_yield(Configuration&)
{
#if defined(AK_OS_RINOS)
    if (__rin_syscall_posixize(_syscall0(SYS_SCHED_YIELD)) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    return Errno::NoSys;
#endif
}
ErrorOr<Result<FD>> Implementation::impl$sock_accept(Configuration&, FD fd, FDFlags fd_flags)
{
    if (!has_right(fd, 1ull << 29))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    constexpr u16 supported_flags = 1u << 2;
    if ((fd_flags.data.value() & ~supported_flags) != 0)
        return Errno::Invalid;
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    int accept_flags = SOCK_CLOEXEC;
    if (fd_flags.bits.nonblock)
        accept_flags |= SOCK_NONBLOCK;
    auto accepted_fd = accept4(host_fd, nullptr, nullptr, accept_flags);
    if (accepted_fd < 0)
        return errno_value_from_errno(errno);
    if (static_cast<u64>(accepted_fd) > NumericLimits<u32>::max()) {
        close(accepted_fd);
        return Errno::Overflow;
    }
    m_fd_map.insert(static_cast<u32>(accepted_fd), static_cast<u32>(accepted_fd));
    install_rights(static_cast<u32>(accepted_fd), Rights { .data = all_rights_mask }, Rights { .data = all_rights_mask });
    return FD(static_cast<u32>(accepted_fd));
#else
    (void)fd_flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<SockRecvResult>> Implementation::impl$sock_recv(Configuration& configuration, FD fd, Pointer<IOVec> ri_data, Size ri_data_len, RIFlags ri_flags)
{
    if (!has_right(fd, 1ull << 1))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    if ((ri_flags.data.value() & ~0x3u) != 0)
        return Errno::Invalid;
    auto guest_iovecs = TRY(copy_typed_array(configuration, ri_data, ri_data_len));
    if (guest_iovecs.size() > 64)
        return Errno::TooBig;
    Vector<struct iovec> host_iovecs;
    TRY(host_iovecs.try_ensure_capacity(guest_iovecs.size()));
    u64 requested_bytes = 0;
    for (auto const& iovec : guest_iovecs) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        if (iovec.buf_len.value() > NumericLimits<u32>::max() - requested_bytes)
            return Errno::TooBig;
        requested_bytes += iovec.buf_len.value();
        struct iovec host_iovec { .iov_base = slice.data(), .iov_len = slice.size() };
        host_iovecs.unchecked_append(host_iovec);
    }
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    struct msghdr message {};
    message.msg_iov = host_iovecs.data();
    message.msg_iovlen = host_iovecs.size();
    int native_flags = 0;
    if (ri_flags.bits.recv_peek)
        native_flags |= MSG_PEEK;
    if (ri_flags.bits.recv_waitall)
        native_flags |= MSG_WAITALL;
    auto received = recvmsg(host_fd, &message, native_flags);
    if (received < 0)
        return errno_value_from_errno(errno);
    if (static_cast<u64>(received) > requested_bytes)
        return Errno::IO;
    ROFlags ro_flags { .data = 0 };
    ro_flags.bits.recv_data_truncated = (message.msg_flags & MSG_TRUNC) != 0;
    return Result<SockRecvResult> { SockRecvResult { .size = Size(static_cast<u32>(received)), .roflags = ro_flags } };
#else
    (void)configuration;
    (void)ri_data;
    (void)ri_data_len;
    (void)ri_flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<Size>> Implementation::impl$sock_send(Configuration& configuration, FD fd, Pointer<CIOVec> si_data, Size si_data_len, SIFlags si_flags)
{
    if (!has_right(fd, 1ull << 6))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    if (si_flags.value() != 0)
        return Errno::Invalid;
    auto guest_iovecs = TRY(copy_typed_array(configuration, si_data, si_data_len));
    if (guest_iovecs.size() > 64)
        return Errno::TooBig;
    Vector<struct iovec> host_iovecs;
    TRY(host_iovecs.try_ensure_capacity(guest_iovecs.size()));
    u64 requested_bytes = 0;
    for (auto const& iovec : guest_iovecs) {
        auto slice = TRY(slice_typed_memory(configuration, iovec.buf, iovec.buf_len));
        if (iovec.buf_len.value() > NumericLimits<u32>::max() - requested_bytes)
            return Errno::TooBig;
        requested_bytes += iovec.buf_len.value();
        struct iovec host_iovec { .iov_base = const_cast<u8*>(slice.data()), .iov_len = slice.size() };
        host_iovecs.unchecked_append(host_iovec);
    }
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    struct msghdr message {};
    message.msg_iov = host_iovecs.data();
    message.msg_iovlen = host_iovecs.size();
    auto sent = sendmsg(host_fd, &message, MSG_NOSIGNAL);
    if (sent < 0)
        return errno_value_from_errno(errno);
    if (static_cast<u64>(sent) > requested_bytes)
        return Errno::IO;
    return Size(static_cast<u32>(sent));
#else
    (void)configuration;
    (void)si_data;
    (void)si_data_len;
    (void)si_flags;
    return Errno::NoSys;
#endif
}
ErrorOr<Result<void>> Implementation::impl$sock_shutdown(Configuration&, FD fd, SDFlags how)
{
    if (!has_right(fd, 1ull << 28))
        return Errno::NotCapable;
#if defined(AK_OS_RINOS)
    if ((how.data.value() & ~0x3u) != 0 || how.data.value() == 0)
        return Errno::Invalid;
    int native_how;
    switch (how.data.value()) {
    case 1: native_how = SHUT_RD; break;
    case 2: native_how = SHUT_WR; break;
    case 3: native_how = SHUT_RDWR; break;
    default: return Errno::Invalid;
    }
    auto host_fd = resolve_host_fd(fd);
    if (host_fd < 0)
        return errno_value_from_errno(errno);
    if (shutdown(host_fd, native_how) < 0)
        return errno_value_from_errno(errno);
    return Result<void> {};
#else
    (void)how;
    return Errno::NoSys;
#endif
}

#pragma GCC diagnostic pop

template<size_t N>
static Array<Bytes, N> address_spans(Span<Value> values, Configuration& configuration)
{
    Array<Bytes, N> result;
    auto memory = configuration.store().get(MemoryAddress { 0 })->data().span();
    for (size_t i = 0; i < N; ++i)
        result[i] = memory.slice(values[i].to<i32>());
    return result;
}

#define ENUMERATE_FUNCTION_NAMES(M) \
    M(args_get)                     \
    M(args_sizes_get)               \
    M(environ_get)                  \
    M(environ_sizes_get)            \
    M(clock_res_get)                \
    M(clock_time_get)               \
    M(fd_advise)                    \
    M(fd_allocate)                  \
    M(fd_close)                     \
    M(fd_datasync)                  \
    M(fd_fdstat_get)                \
    M(fd_fdstat_set_flags)          \
    M(fd_fdstat_set_rights)         \
    M(fd_filestat_get)              \
    M(fd_filestat_set_size)         \
    M(fd_filestat_set_times)        \
    M(fd_pread)                     \
    M(fd_prestat_get)               \
    M(fd_prestat_dir_name)          \
    M(fd_pwrite)                    \
    M(fd_read)                      \
    M(fd_readdir)                   \
    M(fd_renumber)                  \
    M(fd_seek)                      \
    M(fd_sync)                      \
    M(fd_tell)                      \
    M(fd_write)                     \
    M(path_create_directory)        \
    M(path_filestat_get)            \
    M(path_filestat_set_times)      \
    M(path_link)                    \
    M(path_open)                    \
    M(path_readlink)                \
    M(path_remove_directory)        \
    M(path_rename)                  \
    M(path_symlink)                 \
    M(path_unlink_file)             \
    M(poll_oneoff)                  \
    M(proc_exit)                    \
    M(proc_raise)                   \
    M(sched_yield)                  \
    M(random_get)                   \
    M(sock_accept)                  \
    M(sock_recv)                    \
    M(sock_send)                    \
    M(sock_shutdown)

struct Names {
#define NAME(x) FlyString x;
    ENUMERATE_FUNCTION_NAMES(NAME)
#undef NAME

    static ErrorOr<Names> construct()
    {
        return Names {
#define NAME(x) .x = TRY(FlyString::from_utf8(#x##sv)),
            ENUMERATE_FUNCTION_NAMES(NAME)
#undef NAME
        };
    }
};

ErrorOr<HostFunction> Implementation::function_by_name(StringView name)
{
    auto name_for_comparison = TRY(FlyString::from_utf8(name));
    static auto names = TRY(Names::construct());

#define IMPL(x)                         \
    if (name_for_comparison == names.x) \
        return invocation_of<&Implementation::impl$##x>(#x##sv);

    ENUMERATE_FUNCTION_NAMES(IMPL)

#undef IMPL

    return Error::from_string_literal("No such host function");
}

namespace ABI {

template<typename T>
struct HostTypeImpl {
    using Type = T;
};

template<Enum T>
struct HostTypeImpl<T> {
    using Type = UnderlyingType<T>;
};

template<typename T>
struct HostTypeImpl<LittleEndian<T>> {
    using Type = typename HostTypeImpl<T>::Type;
};

template<typename T, typename t, typename... Fs>
struct HostTypeImpl<DistinctNumeric<T, t, Fs...>> {
    using Type = typename HostTypeImpl<T>::Type;
};

template<typename T>
using HostType = typename HostTypeImpl<T>::Type;

template<typename T>
auto CompatibleValueType = IsOneOf<HostType<T>, char, i8, i16, i32, u8, u16>
    ? Wasm::ValueType(Wasm::ValueType::I32)
    : Wasm::ValueType(Wasm::ValueType::I64);

template<typename RV, typename... Args, ErrorOr<RV> (Implementation::*impl)(Configuration&, Args...)>
struct InvocationOf<impl> {
    HostFunction operator()(Implementation& self, StringView function_name)
    {
        using R = typename decltype([] {
            if constexpr (IsSame<RV, Result<void>>)
                return TypeWrapper<void> {};
            else if constexpr (IsSpecializationOf<RV, Result>)
                return TypeWrapper<RemoveCVReference<decltype(*declval<RV>().result())>> {};
            else
                return TypeWrapper<RV> {};
        }())::Type;

        Vector<ValueType> arguments_types { CompatibleValueType<typename ABI::ToCompatibleValue<Args>::Type>... };
        if constexpr (!IsVoid<R>) {
            if constexpr (requires { declval<typename R::SerializationComponents>(); }) {
                for_each_type<typename R::SerializationComponents>([&]<typename T>(TypeWrapper<T>) {
                    arguments_types.append(CompatibleValueType<typename ABI::ToCompatibleValue<Pointer<T>>::Type>);
                });
            } else {
                arguments_types.append(CompatibleValueType<typename ABI::ToCompatibleValue<Pointer<R>>::Type>);
            }
        }

        Vector<ValueType> return_ty;
        if constexpr (IsSpecializationOf<RV, Result>)
            return_ty.append(ValueType(ValueType::I32));

        return HostFunction(
            [&self, function_name](Configuration& configuration, Span<Value> arguments) -> Wasm::Result {
                Tuple args = [&]<typename... Ts, auto... Is>(IndexSequence<Is...>) {
                    return Tuple { ABI::deserialize(ABI::to_compatible_value<Ts>(arguments[Is]))... };
                }.template operator()<Args...>(MakeIndexSequence<sizeof...(Args)>());

                auto result = args.apply_as_args([&](auto&&... impl_args) { return (self.*impl)(configuration, impl_args...); });
                dbgln_if(WASI_DEBUG, "WASI: {}({}) = {}", function_name, arguments, result);
                if (result.is_error()) {
                    auto error = result.release_error();
                    if (error.is_errno())
                        return Wasm::Trap { ByteString::formatted("exit:{}", error.code() + 1) };
                    return Wasm::Trap { ByteString::formatted("Invalid call to {}() = {}", function_name, error) };
                }

                auto value = result.release_value();
                if constexpr (IsSpecializationOf<RV, Result>) {
                    if (value.is_error())
                        return Wasm::Result { Vector { Value { static_cast<u32>(to_underlying(value.error().value())) } } };
                }

                if constexpr (!IsVoid<R>) {
                    // Return values are passed as pointers, after the arguments
                    if constexpr (requires { &R::serialize_into; }) {
                        constexpr auto ResultCount = []<auto N>(void (R::*)(Array<Bytes, N>) const) { return N; }(&R::serialize_into);
                        ABI::serialize(*value.result(), address_spans<ResultCount>(arguments.slice(sizeof...(Args)), configuration));
                    } else {
                        ABI::serialize(*value.result(), address_spans<1>(arguments.slice(sizeof...(Args)), configuration));
                    }
                }
                // Return value is errno, we have nothing to return.
                return Wasm::Result { Vector<Value> { Value(ValueType(ValueType::Kind::I32)) } };
            },
            FunctionType {
                move(arguments_types),
                return_ty,
            },
            function_name);
    }
};

};

Errno errno_value_from_errno(int value)
{
    switch (value) {
#ifdef ESUCCESS
    case ESUCCESS:
        return Errno::Success;
#endif
    case E2BIG:
        return Errno::TooBig;
    case EACCES:
        return Errno::Access;
    case EADDRINUSE:
        return Errno::AddressInUse;
    case EADDRNOTAVAIL:
        return Errno::AddressNotAvailable;
    case EAFNOSUPPORT:
        return Errno::AFNotSupported;
    case EAGAIN:
        return Errno::Again;
    case EALREADY:
        return Errno::Already;
    case EBADF:
        return Errno::BadF;
    case EBUSY:
        return Errno::Busy;
    case ECANCELED:
        return Errno::Canceled;
    case ECHILD:
        return Errno::Child;
    case ECONNABORTED:
        return Errno::ConnectionAborted;
    case ECONNREFUSED:
        return Errno::ConnectionRefused;
    case ECONNRESET:
        return Errno::ConnectionReset;
    case EDEADLK:
        return Errno::Deadlock;
    case EDESTADDRREQ:
        return Errno::DestinationAddressRequired;
    case EDOM:
        return Errno::Domain;
    case EEXIST:
        return Errno::Exist;
    case EFAULT:
        return Errno::Fault;
    case EFBIG:
        return Errno::FBig;
    case EHOSTUNREACH:
        return Errno::HostUnreachable;
    case EILSEQ:
        return Errno::IllegalSequence;
    case EINPROGRESS:
        return Errno::InProgress;
    case EINTR:
        return Errno::Interrupted;
    case EINVAL:
        return Errno::Invalid;
    case EIO:
        return Errno::IO;
    case EISCONN:
        return Errno::IsConnected;
    case EISDIR:
        return Errno::IsDirectory;
    case ELOOP:
        return Errno::Loop;
    case EMFILE:
        return Errno::MFile;
    case EMLINK:
        return Errno::MLink;
    case EMSGSIZE:
        return Errno::MessageSize;
    case ENAMETOOLONG:
        return Errno::NameTooLong;
    case ENETDOWN:
        return Errno::NetworkDown;
    case ENETRESET:
        return Errno::NetworkReset;
    case ENETUNREACH:
        return Errno::NetworkUnreachable;
    case ENFILE:
        return Errno::NFile;
    case ENOBUFS:
        return Errno::NoBufferSpace;
    case ENODEV:
        return Errno::NoDevice;
    case ENOENT:
        return Errno::NoEntry;
    case ENOEXEC:
        return Errno::NoExec;
    case ENOLCK:
        return Errno::NoLock;
    case ENOMEM:
        return Errno::NoMemory;
    case ENOPROTOOPT:
        return Errno::NoProtocolOption;
    case ENOSPC:
        return Errno::NoSpace;
    case ENOSYS:
        return Errno::NoSys;
    case ENOTCONN:
        return Errno::NotConnected;
    case ENOTDIR:
        return Errno::NotDirectory;
    case ENOTEMPTY:
        return Errno::NotEmpty;
    case ENOTRECOVERABLE:
        return Errno::NotRecoverable;
    case ENOTSOCK:
        return Errno::NotSocket;
    case ENOTSUP:
        return Errno::NotSupported;
    case ENOTTY:
        return Errno::NoTTY;
    case ENXIO:
        return Errno::NXIO;
    case EOVERFLOW:
        return Errno::Overflow;
    case EPERM:
        return Errno::Permission;
    case EPIPE:
        return Errno::Pipe;
    case EPROTO:
        return Errno::Protocol;
    case EPROTONOSUPPORT:
        return Errno::ProtocolNotSupported;
    case EPROTOTYPE:
        return Errno::ProtocolType;
    case ERANGE:
        return Errno::Range;
    case ESPIPE:
        return Errno::SPipe;
    case ESRCH:
        return Errno::SRCH;
    case ESTALE:
        return Errno::Stale;
    case ETIMEDOUT:
        return Errno::TimedOut;
    case ETXTBSY:
        return Errno::TextBusy;
    case EXDEV:
        return Errno::XDev;
    default:
        return Errno::Invalid;
    }
}

FileType file_type_of(struct stat const& buf)
{
    switch (buf.st_mode & S_IFMT) {
    case S_IFDIR:
        return FileType::Directory;
    case S_IFCHR:
        return FileType::CharacterDevice;
    case S_IFBLK:
        return FileType::BlockDevice;
    case S_IFREG:
        return FileType::RegularFile;
    case S_IFIFO:
        return FileType::Unknown; // FIXME: FileType::Pipe is currently not present in WASI (but it should be) so we use Unknown for now.
    case S_IFLNK:
        return FileType::SymbolicLink;
    case S_IFSOCK:
        return FileType::SocketStream;
    default:
        return FileType::Unknown;
    }
}
}

namespace AK {

// Don't remove, needed for dbgln_if WASI_DEBUG above to display arguments
template<>
struct Formatter<Wasm::Value> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Value const& value)
    {
        return Formatter<FormatString>::format(builder, "{}"sv, value.to<u128>());
    }
};

template<>
struct Formatter<Wasm::Wasi::Errno> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::Errno const& value)
    {
        return Formatter<FormatString>::format(builder, "{}"sv, to_underlying(value));
    }
};

template<>
struct Formatter<Empty> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder&, Empty)
    {
        return {};
    }
};

template<typename T>
struct Formatter<Wasm::Wasi::Result<T>> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::Result<T> const& value)
    {
        if (value.is_error())
            return Formatter<FormatString>::format(builder, "Error({})"sv, *value.error());

        return Formatter<FormatString>::format(builder, "Ok({})"sv, *value.result());
    }
};

template<OneOf<Wasm::Wasi::ArgsSizes, Wasm::Wasi::EnvironSizes> T>
struct Formatter<T> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, T const& value)
    {
        return Formatter<FormatString>::format(builder, "size={}, count={}"sv, value.size, value.count);
    }
};

template<>
struct Formatter<Wasm::Wasi::FDStat> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::FDStat const&)
    {
        return Formatter<FormatString>::format(builder, "(rights)"sv);
    }
};

template<>
struct Formatter<Wasm::Wasi::FileStat> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::FileStat const& value)
    {
        return Formatter<FormatString>::format(builder, "dev={}, ino={}, ft={}, nlink={}, size={}, atim={}, mtim={}, ctim={}"sv,
            value.dev, value.ino, to_underlying(value.filetype), value.nlink, value.size, value.atim, value.mtim, value.ctim);
    }
};

template<>
struct Formatter<Wasm::Wasi::PreStat> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::PreStat const& value)
    {
        return Formatter<FormatString>::format(builder, "length={}"sv, value.dir.pr_name_len);
    }
};

template<>
struct Formatter<Wasm::Wasi::SockRecvResult> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Wasm::Wasi::SockRecvResult const& value)
    {
        return Formatter<FormatString>::format(builder, "size={}"sv, value.size);
    }
};

}
