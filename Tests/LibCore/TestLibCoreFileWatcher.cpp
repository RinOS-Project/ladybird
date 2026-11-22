/*
 * Copyright (c) 2021, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/LexicalPath.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibCore/FileWatcher.h>
#include <LibCore/System.h>
#include <LibCore/Timer.h>
#include <LibFileSystem/FileSystem.h>
#include <LibTest/TestCase.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef AK_OS_MACOS
constexpr int TIMEOUT_PER_STEP_IN_MS = 350;
#else
constexpr int TIMEOUT_PER_STEP_IN_MS = 75;
#endif

struct DelayedFileCreation {
    char const* path;
};

static void* create_file_after_delay(void* opaque)
{
    auto const& context = *static_cast<DelayedFileCreation*>(opaque);
    usleep(20 * 1000);
    int fd = creat(context.path, 0600);
    if (fd < 0)
        return reinterpret_cast<void*>(1);
    close(fd);
    return nullptr;
}

TEST_CASE(blocking_file_watcher_waits_and_preserves_read_batch)
{
    char directory_template[] = "/tmp/file-watcher-blocking-XXXXXX";
    auto* directory_path = mkdtemp(directory_template);
    EXPECT_NE(directory_path, nullptr);
    if (!directory_path)
        return;

    auto directory = StringView { directory_path, strlen(directory_path) };
    auto first_path = ByteString::formatted("{}/first", directory);
    auto second_path = ByteString::formatted("{}/second", directory);
    auto trigger_path = ByteString::formatted("{}/trigger", directory);
    auto watcher = MUST(Core::BlockingFileWatcher::create());
    MUST(watcher->add_watch(directory, Core::FileWatcherEvent::Type::ChildCreated));

    DelayedFileCreation context { trigger_path.characters() };
    pthread_t writer;
    if (pthread_create(&writer, nullptr, create_file_after_delay, &context) != 0) {
        EXPECT(false);
        MUST(Core::System::rmdir(directory));
        return;
    }

    auto first_event = MUST(watcher->wait_for_event());
    void* writer_result = nullptr;
    EXPECT_EQ(pthread_join(writer, &writer_result), 0);
    EXPECT_EQ(writer_result, nullptr);
    EXPECT(first_event.has_value());
    if (first_event.has_value()) {
        EXPECT_EQ(first_event->event_path, trigger_path);
        EXPECT(has_flag(first_event->type, Core::FileWatcherEvent::Type::ChildCreated));
    }

    int first_fd = open(first_path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    int second_fd = open(second_path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    EXPECT_NE(first_fd, -1);
    EXPECT_NE(second_fd, -1);
    if (first_fd >= 0)
        EXPECT_EQ(close(first_fd), 0);
    if (second_fd >= 0)
        EXPECT_EQ(close(second_fd), 0);

    auto second_event = MUST(watcher->wait_for_event());
    auto third_event = MUST(watcher->wait_for_event());
    EXPECT(second_event.has_value());
    EXPECT(third_event.has_value());
    if (second_event.has_value() && third_event.has_value()) {
        EXPECT_EQ(second_event->event_path, first_path);
        EXPECT_EQ(third_event->event_path, second_path);
    }

    MUST(watcher->remove_watch(directory));
    MUST(Core::System::unlink(trigger_path));
    MUST(Core::System::unlink(first_path));
    MUST(Core::System::unlink(second_path));
    MUST(Core::System::rmdir(directory));
}

TEST_CASE(blocking_file_watcher_nonblocking_empty_result)
{
    char directory_template[] = "/tmp/file-watcher-nonblocking-XXXXXX";
    auto* directory_path = mkdtemp(directory_template);
    EXPECT_NE(directory_path, nullptr);
    if (!directory_path)
        return;

    auto directory = StringView { directory_path, strlen(directory_path) };
    auto file_path = ByteString::formatted("{}/created", directory);
    auto watcher = MUST(Core::BlockingFileWatcher::create(Core::FileWatcherFlags::Nonblock));
    MUST(watcher->add_watch(directory, Core::FileWatcherEvent::Type::ChildCreated));
    auto empty_result = MUST(watcher->wait_for_event());
    EXPECT(!empty_result.has_value());

    int fd = open(file_path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    EXPECT_NE(fd, -1);
    if (fd >= 0)
        EXPECT_EQ(close(fd), 0);
    auto created_event = MUST(watcher->wait_for_event());
    EXPECT(created_event.has_value());
    if (created_event.has_value()) {
        EXPECT_EQ(created_event->event_path, file_path);
        EXPECT(has_flag(created_event->type, Core::FileWatcherEvent::Type::ChildCreated));
    }

    MUST(watcher->remove_watch(directory));
    MUST(Core::System::unlink(file_path));
    MUST(Core::System::rmdir(directory));
}

TEST_CASE(file_watcher_aliases_share_a_descriptor_without_crossing_event_masks)
{
    char directory_template[] = "/tmp/file-watcher-alias-XXXXXX";
    auto* directory_path = mkdtemp(directory_template);
    EXPECT_NE(directory_path, nullptr);
    if (!directory_path)
        return;

    auto root = StringView { directory_path, strlen(directory_path) };
    auto watched_path = ByteString::formatted("{}/watched", root);
    auto alias_path = ByteString::formatted("{}/alias", root);
    auto created_path = ByteString::formatted("{}/created", watched_path);
    auto after_remove_path = ByteString::formatted("{}/after-remove", watched_path);
    MUST(Core::System::mkdir(watched_path, 0700));
    MUST(Core::System::symlink(watched_path, alias_path));

    auto watcher = MUST(Core::BlockingFileWatcher::create(Core::FileWatcherFlags::Nonblock));
    MUST(watcher->add_watch(watched_path, Core::FileWatcherEvent::Type::ChildCreated | Core::FileWatcherEvent::Type::Deleted));
    MUST(watcher->add_watch(alias_path, Core::FileWatcherEvent::Type::ContentModified));

    int fd = open(created_path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    EXPECT_NE(fd, -1);
    if (fd >= 0)
        EXPECT_EQ(close(fd), 0);
    auto created_event = MUST(watcher->wait_for_event());
    EXPECT(created_event.has_value());
    if (created_event.has_value()) {
        EXPECT_EQ(created_event->event_path, created_path);
        EXPECT_EQ(created_event->type, Core::FileWatcherEvent::Type::ChildCreated);
    }
    EXPECT(!MUST(watcher->wait_for_event()).has_value());

    fd = open(created_path.characters(), O_WRONLY | O_APPEND);
    EXPECT_NE(fd, -1);
    if (fd >= 0) {
        char byte = 'x';
        EXPECT_EQ(write(fd, &byte, sizeof(byte)), static_cast<ssize_t>(sizeof(byte)));
        EXPECT_EQ(close(fd), 0);
    }
    auto modified_event = MUST(watcher->wait_for_event());
    EXPECT(modified_event.has_value());
    if (modified_event.has_value()) {
        EXPECT_EQ(modified_event->event_path, ByteString::formatted("{}/created", alias_path));
        EXPECT_EQ(modified_event->type, Core::FileWatcherEvent::Type::ContentModified);
    }

    EXPECT(MUST(watcher->remove_watch(alias_path)));
    EXPECT(watcher->is_watching(watched_path));
    EXPECT(!watcher->is_watching(alias_path));
    fd = open(after_remove_path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    EXPECT_NE(fd, -1);
    if (fd >= 0)
        EXPECT_EQ(close(fd), 0);
    auto after_remove_event = MUST(watcher->wait_for_event());
    EXPECT(after_remove_event.has_value());
    if (after_remove_event.has_value()) {
        EXPECT_EQ(after_remove_event->event_path, after_remove_path);
        EXPECT_EQ(after_remove_event->type, Core::FileWatcherEvent::Type::ChildCreated);
    }

    MUST(Core::System::unlink(created_path));
    MUST(Core::System::unlink(after_remove_path));
    MUST(Core::System::rmdir(watched_path));
    auto deleted_event = MUST(watcher->wait_for_event());
    EXPECT(deleted_event.has_value());
    if (deleted_event.has_value()) {
        EXPECT_EQ(deleted_event->event_path, watched_path);
        EXPECT_EQ(deleted_event->type, Core::FileWatcherEvent::Type::Deleted);
    }
    EXPECT(!watcher->is_watching(watched_path));
    EXPECT(!MUST(watcher->remove_watch(watched_path)));
    MUST(Core::System::unlink(alias_path));
    MUST(Core::System::rmdir(root));
}

TEST_CASE(file_watcher_child_events)
{
    auto event_loop = Core::EventLoop();
    auto maybe_file_watcher = Core::FileWatcher::create();
    EXPECT_NE(maybe_file_watcher.is_error(), true);

    // Ensure the testfile does not already exist.
    (void)Core::System::unlink("/tmp/testfile"sv);

    auto file_watcher = maybe_file_watcher.release_value();
    auto watch_result = file_watcher->add_watch("/tmp/",
        Core::FileWatcherEvent::Type::ChildCreated
            | Core::FileWatcherEvent::Type::ChildDeleted);
    EXPECT_NE(watch_result.is_error(), true);

    int event_count = 0;
    file_watcher->on_change = [&](Core::FileWatcherEvent const& event) {
        // Ignore path events under /tmp that can occur for anything else the OS is
        // doing to create/delete files there.
        if (event.event_path != "/tmp/testfile"sv)
            return;

        if (event_count == 0) {
            EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ChildCreated));
        } else if (event_count == 1) {
            EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ChildDeleted));
            EXPECT(MUST(file_watcher->remove_watch("/tmp/"sv)));

            event_loop.quit(0);
        }

        event_count++;
    };

    auto timer1 = Core::Timer::create_single_shot(1 * TIMEOUT_PER_STEP_IN_MS, [&] {
        int rc = creat("/tmp/testfile", 0777);
        EXPECT_NE(rc, -1);
    });
    timer1->start();

    auto timer2 = Core::Timer::create_single_shot(2 * TIMEOUT_PER_STEP_IN_MS, [&] {
        MUST(Core::System::unlink("/tmp/testfile"sv));
    });
    timer2->start();

    auto catchall_timer = Core::Timer::create_single_shot(3 * TIMEOUT_PER_STEP_IN_MS, [&] {
        VERIFY_NOT_REACHED();
    });
    catchall_timer->start();

    event_loop.exec();
}

TEST_CASE(file_watcher_child_rename_events)
{
    auto event_loop = Core::EventLoop();
    static constexpr char source_path[] = "/tmp/file-watcher-rename-source";
    static constexpr char destination_path[] = "/tmp/file-watcher-rename-destination";
    static constexpr auto source_path_view = "/tmp/file-watcher-rename-source"sv;
    static constexpr auto destination_path_view = "/tmp/file-watcher-rename-destination"sv;
    (void)Core::System::unlink(source_path_view);
    (void)Core::System::unlink(destination_path_view);
    EXPECT_NE(creat(source_path, 0600), -1);

    auto file_watcher = MUST(Core::FileWatcher::create());
    MUST(file_watcher->add_watch("/tmp/"sv,
        Core::FileWatcherEvent::Type::ChildCreated
            | Core::FileWatcherEvent::Type::ChildDeleted));

    int event_count = 0;
    file_watcher->on_change = [&](Core::FileWatcherEvent const& event) {
        if (event.event_path != source_path_view && event.event_path != destination_path_view)
            return;

        if (event_count == 0) {
            EXPECT_EQ(event.event_path, source_path_view);
            EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ChildDeleted));
        } else if (event_count == 1) {
            EXPECT_EQ(event.event_path, destination_path_view);
            EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ChildCreated));
        } else if (event_count == 2) {
            EXPECT_EQ(event.event_path, destination_path_view);
            EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ChildDeleted));
            MUST(file_watcher->remove_watch("/tmp/"sv));
            event_loop.quit(0);
        }

        ++event_count;
    };

    auto rename_timer = Core::Timer::create_single_shot(TIMEOUT_PER_STEP_IN_MS, [&] {
        EXPECT_EQ(rename(source_path, destination_path), 0);
    });
    rename_timer->start();

    auto delete_timer = Core::Timer::create_single_shot(2 * TIMEOUT_PER_STEP_IN_MS, [&] {
        MUST(Core::System::unlink(destination_path_view));
    });
    delete_timer->start();

    auto catchall_timer = Core::Timer::create_single_shot(4 * TIMEOUT_PER_STEP_IN_MS, [&] {
        VERIFY_NOT_REACHED();
    });
    catchall_timer->start();

    event_loop.exec();
}

TEST_CASE(file_watcher_queue_overflow)
{
    char directory_template[] = "/tmp/file-watcher-overflow-XXXXXX";
    auto* directory_path = mkdtemp(directory_template);
    EXPECT_NE(directory_path, nullptr);
    if (!directory_path)
        return;

    auto directory = StringView { directory_path, strlen(directory_path) };
    auto event_loop = Core::EventLoop();
    auto file_watcher = MUST(Core::FileWatcher::create());
    MUST(file_watcher->add_watch(directory, Core::FileWatcherEvent::Type::ChildCreated));
    auto blocking_file_watcher = MUST(Core::BlockingFileWatcher::create(Core::FileWatcherFlags::Nonblock));
    MUST(blocking_file_watcher->add_watch(directory, Core::FileWatcherEvent::Type::ChildCreated));

    bool saw_overflow = false;
    file_watcher->on_change = [&](Core::FileWatcherEvent const& event) {
        if (!has_flag(event.type, Core::FileWatcherEvent::Type::QueueOverflow))
            return;

        EXPECT(event.event_path.is_empty());
        saw_overflow = true;
        event_loop.quit(0);
    };

    static constexpr size_t event_count = 20000;
    size_t files_created = 0;
    for (; files_created < event_count; ++files_created) {
        auto path = ByteString::formatted("{}/{}", directory, files_created);
        int fd = open(path.characters(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        EXPECT_NE(fd, -1);
        if (fd < 0)
            break;
        EXPECT_EQ(close(fd), 0);
    }

    EXPECT_EQ(files_created, event_count);

    bool blocking_watcher_saw_overflow = false;
    for (size_t attempt = 0; attempt < event_count; ++attempt) {
        auto event = MUST(blocking_file_watcher->wait_for_event());
        if (!event.has_value())
            break;
        if (!has_flag(event->type, Core::FileWatcherEvent::Type::QueueOverflow))
            continue;
        EXPECT(event->event_path.is_empty());
        blocking_watcher_saw_overflow = true;
        break;
    }
    EXPECT(blocking_watcher_saw_overflow);

    auto timeout = Core::Timer::create_single_shot(15000, [&] {
        VERIFY_NOT_REACHED();
    });
    timeout->start();
    event_loop.exec();
    EXPECT(saw_overflow);

    for (size_t index = 0; index < files_created; ++index) {
        auto path = ByteString::formatted("{}/{}", directory, index);
        (void)Core::System::unlink(path);
    }
    MUST(Core::System::rmdir(directory));
}

TEST_CASE(contents_changed)
{
    auto event_loop = Core::EventLoop();

    auto temp_path = MUST(FileSystem::real_path("/tmp"sv));
    auto test_path = LexicalPath::join(temp_path, "testfile"sv);

    auto write_file = [&](auto contents) {
        auto file = MUST(Core::File::open(test_path.string(), Core::File::OpenMode::Write));
        MUST(file->write_until_depleted(contents));
    };

    write_file("line1\n"sv);

    auto file_watcher = MUST(Core::FileWatcher::create());
    MUST(file_watcher->add_watch(test_path.string(), Core::FileWatcherEvent::Type::ContentModified));

    int event_count = 0;
    file_watcher->on_change = [&](Core::FileWatcherEvent const& event) {
        EXPECT_EQ(event.event_path, test_path.string());
        EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::ContentModified));

        if (++event_count == 2) {
            MUST(Core::System::unlink(test_path.string()));
            event_loop.quit(0);
        }
    };

    auto timer1 = Core::Timer::create_single_shot(1 * TIMEOUT_PER_STEP_IN_MS, [&] { write_file("line2\n"sv); });
    timer1->start();

    auto timer2 = Core::Timer::create_single_shot(2 * TIMEOUT_PER_STEP_IN_MS, [&] { write_file("line3\n"sv); });
    timer2->start();

    auto catchall_timer = Core::Timer::create_single_shot(3 * TIMEOUT_PER_STEP_IN_MS, [&] {
        VERIFY_NOT_REACHED();
    });
    catchall_timer->start();

    event_loop.exec();
}

TEST_CASE(symbolic_link)
{
    auto event_loop = Core::EventLoop();

    auto temp_path = MUST(FileSystem::real_path("/tmp"sv));
    auto test_file = LexicalPath::join(temp_path, "testfile"sv);
    auto test_link1 = LexicalPath::join(temp_path, "testlink1"sv);
    auto test_link2 = LexicalPath::join(temp_path, "testlink2"sv);

    (void)MUST(Core::File::open(test_link1.string(), Core::File::OpenMode::ReadWrite));
    (void)MUST(Core::File::open(test_link2.string(), Core::File::OpenMode::ReadWrite));
    MUST(Core::System::symlink(test_link1.string(), test_file.string()));

    auto file_watcher = MUST(Core::FileWatcher::create());
    MUST(file_watcher->add_watch(test_file.string(), Core::FileWatcherEvent::Type::Deleted | Core::FileWatcherEvent::Type::DoNotFollowLink));

    int event_count = 0;
    file_watcher->on_change = [&](Core::FileWatcherEvent const& event) {
        EXPECT_EQ(event.event_path, test_file.string());
        EXPECT(has_flag(event.type, Core::FileWatcherEvent::Type::Deleted));

        MUST(file_watcher->add_watch(test_file.string(), Core::FileWatcherEvent::Type::Deleted | Core::FileWatcherEvent::Type::DoNotFollowLink));

        if (++event_count == 2) {
            MUST(Core::System::unlink(test_file.string()));
            MUST(Core::System::unlink(test_link1.string()));
            MUST(Core::System::unlink(test_link2.string()));
            event_loop.quit(0);
        }
    };

    auto timer1 = Core::Timer::create_single_shot(1 * TIMEOUT_PER_STEP_IN_MS, [&] {
        MUST(Core::System::unlink(test_file.string()));
        MUST(Core::System::symlink(test_link1.string(), test_file.string()));
    });
    timer1->start();

    auto timer2 = Core::Timer::create_single_shot(2 * TIMEOUT_PER_STEP_IN_MS, [&] {
        MUST(Core::System::unlink(test_file.string()));
        MUST(Core::System::symlink(test_link2.string(), test_file.string()));
    });
    timer2->start();

    auto catchall_timer = Core::Timer::create_single_shot(3 * TIMEOUT_PER_STEP_IN_MS, [&] {
        VERIFY_NOT_REACHED();
    });
    catchall_timer->start();

    event_loop.exec();
}
