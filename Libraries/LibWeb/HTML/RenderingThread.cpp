/*
 * Copyright (c) 2025-2026, Aliaksandr Kalenik <kalenik.aliaksandr@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifdef AK_OS_RINOS
#include <unistd.h>
static void rt_serial(const char* msg) { write(2, msg, __builtin_strlen(msg)); }
#endif
#include <LibCore/EventLoop.h>
#include <LibGfx/PaintingSurface.h>
#include <LibThreading/Thread.h>
#include <LibWeb/HTML/RenderingThread.h>
#include <LibWeb/HTML/TraversableNavigable.h>
#if defined(AK_OS_RINOS)
#    include <LibWeb/Painting/DisplayListPlayerAquamarine.h>
#else
#include <LibWeb/Painting/DisplayListPlayerSkia.h>
#endif

#include <LibCore/Platform/ScopedAutoreleasePool.h>

namespace Web::HTML {

struct BackingStoreState {
    RefPtr<Gfx::PaintingSurface> front_store;
    RefPtr<Gfx::PaintingSurface> back_store;
    i32 front_bitmap_id { -1 };
    i32 back_bitmap_id { -1 };

    void swap()
    {
        AK::swap(front_store, back_store);
        AK::swap(front_bitmap_id, back_bitmap_id);
    }

    bool is_valid() const { return front_store && back_store; }
};

struct UpdateDisplayListCommand {
    NonnullRefPtr<Painting::DisplayList> display_list;
    Painting::ScrollStateSnapshotByDisplayList scroll_state_snapshot;
};

struct UpdateBackingStoresCommand {
    RefPtr<Gfx::PaintingSurface> front_store;
    RefPtr<Gfx::PaintingSurface> back_store;
    i32 front_bitmap_id;
    i32 back_bitmap_id;
};

struct ScreenshotCommand {
    NonnullRefPtr<Gfx::PaintingSurface> target_surface;
    Function<void()> callback;
};

using CompositorCommand = Variant<UpdateDisplayListCommand, UpdateBackingStoresCommand, ScreenshotCommand>;

class RenderingThread::ThreadData final : public AtomicRefCounted<ThreadData> {
public:
    ThreadData(NonnullRefPtr<Core::WeakEventLoopReference>&& main_thread_event_loop, RenderingThread::PresentationCallback presentation_callback)
        : m_main_thread_event_loop(move(main_thread_event_loop))
        , m_presentation_callback(move(presentation_callback))
    {
    }

    ~ThreadData() = default;

    void set_display_list_player(
#if defined(AK_OS_RINOS)
        OwnPtr<Painting::DisplayListPlayerAquamarine>&& player
#else
        OwnPtr<Painting::DisplayListPlayerSkia>&& player
#endif
    )
    {
        m_display_list_player = move(player);
    }

    bool has_display_list_player() const { return m_display_list_player != nullptr; }

    void exit()
    {
        Threading::MutexLocker const locker { m_mutex };
        m_exit = true;
        m_command_ready.signal();
        m_ready_to_paint.signal();
    }

    void enqueue_command(CompositorCommand&& command)
    {
        Threading::MutexLocker const locker { m_mutex };
        m_command_queue.enqueue(move(command));
        m_command_ready.signal();
    }

    void set_needs_present(Gfx::IntRect viewport_rect)
    {
        Threading::MutexLocker const locker { m_mutex };
        m_needs_present = true;
        m_pending_viewport_rect = viewport_rect;
        m_command_ready.signal();
    }

    void compositor_loop()
    {
        while (true) {
            {
                Threading::MutexLocker const locker { m_mutex };
                while (m_command_queue.is_empty() && !m_needs_present && !m_exit) {
                    m_command_ready.wait();
                }
                if (m_exit)
                    break;
            }

            // Drain autoreleased Objective-C objects created by Metal/Skia each iteration,
            // since this background thread has no autorelease pool.
            Core::ScopedAutoreleasePool autorelease_pool;

            while (true) {
                auto command = [this]() -> Optional<CompositorCommand> {
                    Threading::MutexLocker const locker { m_mutex };
                    if (m_command_queue.is_empty())
                        return {};
                    return m_command_queue.dequeue();
                }();

                if (!command.has_value())
                    break;

                command->visit(
                    [this](UpdateDisplayListCommand& cmd) {
                        m_cached_display_list = move(cmd.display_list);
                        m_cached_scroll_state_snapshot = move(cmd.scroll_state_snapshot);
                    },
                    [this](UpdateBackingStoresCommand& cmd) {
                        m_backing_stores.front_store = move(cmd.front_store);
                        m_backing_stores.back_store = move(cmd.back_store);
                        m_backing_stores.front_bitmap_id = cmd.front_bitmap_id;
                        m_backing_stores.back_bitmap_id = cmd.back_bitmap_id;
                    },
                    [this](ScreenshotCommand& cmd) {
                        if (!m_cached_display_list)
                            return;
                        m_display_list_player->execute(*m_cached_display_list, Painting::ScrollStateSnapshotByDisplayList(m_cached_scroll_state_snapshot), *cmd.target_surface);
                        if (cmd.callback) {
                            invoke_on_main_thread([callback = move(cmd.callback)]() mutable {
                                callback();
                            });
                        }
                    });

                if (m_exit)
                    break;
            }

            if (m_exit)
                break;

            bool should_present = false;
            Gfx::IntRect viewport_rect;
            {
                Threading::MutexLocker const locker { m_mutex };
                if (m_needs_present) {
                    should_present = true;
                    viewport_rect = m_pending_viewport_rect;
                    m_needs_present = false;
                }
            }

            if (should_present) {
                // Block if we already have a frame queued (back pressure)
                {
                    Threading::MutexLocker const locker { m_mutex };
                    while (m_queued_rasterization_tasks > 1 && !m_exit) {
                        m_ready_to_paint.wait();
                    }
                    if (m_exit)
                        break;
                }

                if (m_cached_display_list && m_backing_stores.is_valid()) {
#ifdef AK_OS_RINOS
                    rt_serial("[RenderingThread] RASTERIZING frame\n");
#endif
                    m_display_list_player->execute(*m_cached_display_list, Painting::ScrollStateSnapshotByDisplayList(m_cached_scroll_state_snapshot), *m_backing_stores.back_store);
                    i32 rendered_bitmap_id = m_backing_stores.back_bitmap_id;
                    m_backing_stores.swap();

                    m_queued_rasterization_tasks++;

                    invoke_on_main_thread([this, viewport_rect, rendered_bitmap_id]() {
#ifdef AK_OS_RINOS
                        rt_serial("[RenderingThread] deferred_invoke FIRED on main thread\n");
#endif
                        m_presentation_callback(viewport_rect, rendered_bitmap_id);
                    });
                }
#ifdef AK_OS_RINOS
                else {
                    rt_serial("[RenderingThread] present SKIP: display_list=");
                    rt_serial(m_cached_display_list ? "yes" : "no");
                    rt_serial(" backing_valid=");
                    rt_serial(m_backing_stores.is_valid() ? "yes" : "no");
                    rt_serial("\n");
                }
#endif
            }
        }
    }

private:
    template<typename Invokee>
    void invoke_on_main_thread(Invokee invokee)
    {
        if (m_exit) {
#ifdef AK_OS_RINOS
            rt_serial("[RenderingThread] invoke_on_main_thread: m_exit=true, SKIPPING\n");
#endif
            return;
        }
        auto event_loop = m_main_thread_event_loop->take();
        if (!event_loop) {
#ifdef AK_OS_RINOS
            rt_serial("[RenderingThread] invoke_on_main_thread: event_loop=null, SKIPPING\n");
#endif
            return;
        }
#ifdef AK_OS_RINOS
        rt_serial("[RenderingThread] invoke_on_main_thread: scheduling deferred_invoke\n");
#endif
        event_loop->deferred_invoke([self = NonnullRefPtr(*this), invokee = move(invokee)]() mutable {
            invokee();
        });
    }

    NonnullRefPtr<Core::WeakEventLoopReference> m_main_thread_event_loop;
    RenderingThread::PresentationCallback m_presentation_callback;

    mutable Threading::Mutex m_mutex;
    mutable Threading::ConditionVariable m_command_ready { m_mutex };
    Atomic<bool> m_exit { false };

    Queue<CompositorCommand> m_command_queue;

#if defined(AK_OS_RINOS)
    OwnPtr<Painting::DisplayListPlayerAquamarine> m_display_list_player;
#else
    OwnPtr<Painting::DisplayListPlayerSkia> m_display_list_player;
#endif
    RefPtr<Painting::DisplayList> m_cached_display_list;
    Painting::ScrollStateSnapshotByDisplayList m_cached_scroll_state_snapshot;
    BackingStoreState m_backing_stores;

    Atomic<i32> m_queued_rasterization_tasks { 0 };
    mutable Threading::ConditionVariable m_ready_to_paint { m_mutex };

    bool m_needs_present { false };
    Gfx::IntRect m_pending_viewport_rect;

public:
    void decrement_queued_tasks()
    {
        Threading::MutexLocker const locker { m_mutex };
        VERIFY(m_queued_rasterization_tasks >= 1 && m_queued_rasterization_tasks <= 2);
        m_queued_rasterization_tasks--;
        m_ready_to_paint.signal();
    }
};

RenderingThread::RenderingThread(PresentationCallback presentation_callback)
    : m_thread_data(adopt_ref(*new ThreadData(Core::EventLoop::current_weak(), move(presentation_callback))))
{
}

RenderingThread::~RenderingThread()
{
    m_thread_data->exit();
}

void RenderingThread::start(DisplayListPlayerType)
{
    VERIFY(m_thread_data->has_display_list_player());
#ifdef AK_OS_RINOS
    rt_serial("[RenderingThread] start() called\n");
#endif
    m_thread = Threading::Thread::construct("Renderer"sv, [thread_data = m_thread_data] {
#ifdef AK_OS_RINOS
        rt_serial("[RenderingThread] compositor_loop ENTER\n");
#endif
        thread_data->compositor_loop();
        return static_cast<intptr_t>(0);
    });
    m_thread->start();
    m_thread->detach();
#ifdef AK_OS_RINOS
    rt_serial("[RenderingThread] thread started+detached\n");
#endif
}

void RenderingThread::set_display_list_player(
#if defined(AK_OS_RINOS)
    OwnPtr<Painting::DisplayListPlayerAquamarine>&& player
#else
    OwnPtr<Painting::DisplayListPlayerSkia>&& player
#endif
)
{
    m_thread_data->set_display_list_player(move(player));
}

void RenderingThread::update_display_list(NonnullRefPtr<Painting::DisplayList> display_list, Painting::ScrollStateSnapshotByDisplayList&& scroll_state_snapshot)
{
    m_thread_data->enqueue_command(UpdateDisplayListCommand { move(display_list), move(scroll_state_snapshot) });
}

void RenderingThread::update_backing_stores(RefPtr<Gfx::PaintingSurface> front, RefPtr<Gfx::PaintingSurface> back, i32 front_id, i32 back_id)
{
    m_thread_data->enqueue_command(UpdateBackingStoresCommand { move(front), move(back), front_id, back_id });
}

void RenderingThread::present_frame(Gfx::IntRect viewport_rect)
{
    m_thread_data->set_needs_present(viewport_rect);
}

void RenderingThread::request_screenshot(NonnullRefPtr<Gfx::PaintingSurface> target_surface, Function<void()>&& callback)
{
    m_thread_data->enqueue_command(ScreenshotCommand { move(target_surface), move(callback) });
}

void RenderingThread::ready_to_paint()
{
    m_thread_data->decrement_queued_tasks();
}

}
