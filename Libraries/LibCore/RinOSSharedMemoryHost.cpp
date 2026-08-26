/*
 * Copyright (c) 2026, RinOS contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

// This file is deliberately selected only for native AK_OS_RINOS CTests. The
// target image receives these symbols from src/apps/common/rin_runtime.c and
// must never execute host POSIX syscalls instead.

#include <AK/Types.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
int rin_shm_get(char const* name, u32 size, u32 flags);
void* rin_shm_at(int handle, void* addr_hint, u32 prot);
int rin_shm_dt(int handle, void* addr);
}

namespace {

constexpr u32 rinos_shm_name_max = 64;
constexpr u32 rinos_shm_flag_creat = 0x00000001u;
constexpr u32 rinos_shm_flag_excl = 0x00000002u;
constexpr u32 rinos_shm_flag_unlink_on_close = 0x00000004u;
constexpr u32 rinos_shm_prot_read = 0x00000001u;
constexpr u32 rinos_shm_prot_write = 0x00000002u;

constexpr size_t max_posix_name_length = 160;
constexpr size_t max_shared_objects = 128;
constexpr size_t max_handles = 256;

struct SharedObject {
    bool active { false };
    bool created_by_this_process { false };
    bool unlink_on_last_close { false };
    size_t handle_count { 0 };
    char posix_name[max_posix_name_length] {};
};

struct Handle {
    bool active { false };
    int fd { -1 };
    void* mapping { nullptr };
    size_t mapping_size { 0 };
    size_t object_index { 0 };
};

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static SharedObject s_objects[max_shared_objects];
static Handle s_handles[max_handles];
static bool s_cleanup_registered;

static bool name_has_prefix(char const* name, char const* prefix)
{
    while (*prefix != '\0') {
        if (*name++ != *prefix++)
            return false;
    }
    return true;
}

static bool valid_rinos_name(char const* name)
{
    if (!name || name[0] == '\0')
        return false;

    for (size_t index = 0; index < rinos_shm_name_max; ++index) {
        if (name[index] == '\0')
            return true;
    }
    return false;
}

static bool make_posix_name(char const* name, char (&output)[max_posix_name_length])
{
    static constexpr char prefix[] = "/rin-lb-shm-";
    static constexpr char hex_digits[] = "0123456789abcdef";
    size_t name_length = 0;

    while (name[name_length] != '\0')
        ++name_length;

    constexpr size_t prefix_length = sizeof(prefix) - 1;
    if (prefix_length + name_length * 2 + 1 > sizeof(output))
        return false;

    __builtin_memcpy(output, prefix, prefix_length);
    for (size_t index = 0; index < name_length; ++index) {
        auto byte = static_cast<unsigned char>(name[index]);
        output[prefix_length + index * 2] = hex_digits[byte >> 4];
        output[prefix_length + index * 2 + 1] = hex_digits[byte & 0x0f];
    }
    output[prefix_length + name_length * 2] = '\0';
    return true;
}

static int find_object(char const* posix_name)
{
    for (size_t index = 0; index < max_shared_objects; ++index) {
        if (s_objects[index].active && __builtin_strcmp(s_objects[index].posix_name, posix_name) == 0)
            return static_cast<int>(index);
    }
    return -1;
}

static int allocate_object(char const* posix_name, bool created_by_this_process)
{
    for (size_t index = 0; index < max_shared_objects; ++index) {
        if (s_objects[index].active)
            continue;

        auto& object = s_objects[index];
        __builtin_memset(&object, 0, sizeof(object));
        object.active = true;
        object.created_by_this_process = created_by_this_process;
        __builtin_strcpy(object.posix_name, posix_name);
        return static_cast<int>(index);
    }
    errno = EMFILE;
    return -1;
}

static int allocate_handle(int fd, size_t object_index)
{
    for (size_t index = 0; index < max_handles; ++index) {
        if (s_handles[index].active)
            continue;

        auto& handle = s_handles[index];
        __builtin_memset(&handle, 0, sizeof(handle));
        handle.active = true;
        handle.fd = fd;
        handle.object_index = object_index;
        ++s_objects[object_index].handle_count;
        return static_cast<int>(index);
    }
    errno = EMFILE;
    return -1;
}

static Handle* find_handle(int fd)
{
    for (auto& handle : s_handles) {
        if (handle.active && handle.fd == fd)
            return &handle;
    }
    return nullptr;
}

static void cleanup_host_shared_memory()
{
    (void)pthread_mutex_lock(&s_lock);
    for (auto& object : s_objects) {
        if (object.active && object.created_by_this_process)
            (void)shm_unlink(object.posix_name);
    }
    (void)pthread_mutex_unlock(&s_lock);
}

static bool register_cleanup()
{
    if (s_cleanup_registered)
        return true;
    if (atexit(cleanup_host_shared_memory) != 0) {
        errno = ENOMEM;
        return false;
    }
    s_cleanup_registered = true;
    return true;
}

static int open_posix_shared_memory(char const* name, u32 flags, bool& created)
{
    created = false;

    if ((flags & rinos_shm_flag_creat) == 0)
        return shm_open(name, O_RDWR | O_CLOEXEC, 0);

    if ((flags & rinos_shm_flag_excl) != 0) {
        auto fd = shm_open(name, O_RDWR | O_CLOEXEC | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
        if (fd >= 0)
            created = true;
        return fd;
    }

    auto fd = shm_open(name, O_RDWR | O_CLOEXEC, 0);
    if (fd >= 0)
        return fd;
    if (errno != ENOENT)
        return -1;

    fd = shm_open(name, O_RDWR | O_CLOEXEC | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0) {
        created = true;
        return fd;
    }
    if (errno != EEXIST)
        return -1;
    return shm_open(name, O_RDWR | O_CLOEXEC, 0);
}

}

extern "C" int rin_shm_get(char const* name, u32 size, u32 flags)
{
    constexpr u32 permitted_flags = rinos_shm_flag_creat | rinos_shm_flag_excl | rinos_shm_flag_unlink_on_close;
    char posix_name[max_posix_name_length] {};
    bool created = false;

    if (!valid_rinos_name(name) || name_has_prefix(name, ".rin.sem:") || (flags & ~permitted_flags) != 0 || ((flags & rinos_shm_flag_excl) != 0 && (flags & rinos_shm_flag_creat) == 0)) {
        errno = EINVAL;
        return -1;
    }
    if ((flags & rinos_shm_flag_creat) != 0 && size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (!make_posix_name(name, posix_name)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    (void)pthread_mutex_lock(&s_lock);
    if (!register_cleanup()) {
        (void)pthread_mutex_unlock(&s_lock);
        return -1;
    }

    auto fd = open_posix_shared_memory(posix_name, flags, created);
    if (fd < 0) {
        (void)pthread_mutex_unlock(&s_lock);
        return -1;
    }

    if (created && ftruncate(fd, static_cast<off_t>(size)) != 0) {
        auto saved_errno = errno;
        (void)close(fd);
        (void)shm_unlink(posix_name);
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return -1;
    }

    struct stat state {};
    if (fstat(fd, &state) != 0 || state.st_size <= 0 || static_cast<uintmax_t>(state.st_size) > UINT32_MAX) {
        auto saved_errno = errno == 0 ? EINVAL : errno;
        (void)close(fd);
        if (created)
            (void)shm_unlink(posix_name);
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return -1;
    }
    if (!created && size != static_cast<u32>(state.st_size)) {
        (void)close(fd);
        (void)pthread_mutex_unlock(&s_lock);
        errno = EINVAL;
        return -1;
    }

    auto object_index = find_object(posix_name);
    if (object_index < 0)
        object_index = allocate_object(posix_name, created);
    if (object_index < 0) {
        auto saved_errno = errno;
        (void)close(fd);
        if (created)
            (void)shm_unlink(posix_name);
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return -1;
    }

    auto handle_index = allocate_handle(fd, static_cast<size_t>(object_index));
    if (handle_index < 0) {
        auto saved_errno = errno;
        (void)close(fd);
        if (created)
            (void)shm_unlink(posix_name);
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return -1;
    }
    if ((flags & rinos_shm_flag_unlink_on_close) != 0)
        s_objects[object_index].unlink_on_last_close = true;

    (void)pthread_mutex_unlock(&s_lock);
    return fd;
}

extern "C" void* rin_shm_at(int handle, void* addr_hint, u32 prot)
{
    constexpr u32 permitted_protection = rinos_shm_prot_read | rinos_shm_prot_write;
    if ((prot & ~permitted_protection) != 0 || (prot & rinos_shm_prot_read) == 0) {
        errno = EINVAL;
        return nullptr;
    }

    (void)pthread_mutex_lock(&s_lock);
    auto* slot = find_handle(handle);
    if (!slot) {
        (void)pthread_mutex_unlock(&s_lock);
        errno = EBADF;
        return nullptr;
    }
    if (slot->mapping) {
        auto* mapping = slot->mapping;
        (void)pthread_mutex_unlock(&s_lock);
        return mapping;
    }

    struct stat state {};
    if (fstat(slot->fd, &state) != 0 || state.st_size <= 0) {
        auto saved_errno = errno == 0 ? EINVAL : errno;
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return nullptr;
    }

    auto protection = PROT_READ;
    if ((prot & rinos_shm_prot_write) != 0)
        protection |= PROT_WRITE;
    auto* mapping = mmap(addr_hint, static_cast<size_t>(state.st_size), protection, MAP_SHARED, slot->fd, 0);
    if (mapping == MAP_FAILED) {
        (void)pthread_mutex_unlock(&s_lock);
        return nullptr;
    }
    slot->mapping = mapping;
    slot->mapping_size = static_cast<size_t>(state.st_size);
    (void)pthread_mutex_unlock(&s_lock);
    return mapping;
}

extern "C" int rin_shm_dt(int handle, void* addr)
{
    (void)pthread_mutex_lock(&s_lock);
    auto* slot = find_handle(handle);
    if (!slot) {
        (void)pthread_mutex_unlock(&s_lock);
        errno = EBADF;
        return -1;
    }
    if (addr && (!slot->mapping || addr != slot->mapping)) {
        (void)pthread_mutex_unlock(&s_lock);
        errno = EINVAL;
        return -1;
    }

    if (slot->mapping && munmap(slot->mapping, slot->mapping_size) != 0) {
        (void)pthread_mutex_unlock(&s_lock);
        return -1;
    }
    if (close(slot->fd) != 0) {
        auto saved_errno = errno;
        (void)pthread_mutex_unlock(&s_lock);
        errno = saved_errno;
        return -1;
    }

    auto object_index = slot->object_index;
    __builtin_memset(slot, 0, sizeof(*slot));
    if (object_index < max_shared_objects) {
        auto& object = s_objects[object_index];
        if (object.active && object.handle_count > 0)
            --object.handle_count;
        if (object.active && object.unlink_on_last_close && object.handle_count == 0) {
            auto unlink_result = shm_unlink(object.posix_name);
            auto saved_errno = errno;
            __builtin_memset(&object, 0, sizeof(object));
            (void)pthread_mutex_unlock(&s_lock);
            if (unlink_result != 0 && saved_errno != ENOENT) {
                errno = saved_errno;
                return -1;
            }
            return 0;
        }
    }

    (void)pthread_mutex_unlock(&s_lock);
    return 0;
}
