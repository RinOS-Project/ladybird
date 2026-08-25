/*
 * Copyright (c) 2023, Andrew Kaster <akaster@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Platform.h>
#include <errno.h>
#include <LibCore/EventLoop.h>
#include <WebWorker/ConnectionFromClient.h>
#include <WebWorker/PageHost.h>
#include <WebWorker/WorkerHost.h>

namespace WebWorker {

void ConnectionFromClient::connect_to_request_server(IPC::TransportHandle handle)
{
    if (on_request_server_connection)
        on_request_server_connection(handle);
}

void ConnectionFromClient::connect_to_image_decoder(IPC::TransportHandle handle)
{
    if (on_image_decoder_connection)
        on_image_decoder_connection(handle);
}

void ConnectionFromClient::close_worker()
{
    // RinOS launches one worker per helper process. Releasing the host and
    // quitting this process is therefore the concrete terminate-a-worker
    // operation, rather than a no-op acknowledgement.
    m_worker_host = nullptr;
    m_requested_files.clear();
    async_did_close_worker();

    die();
}

void ConnectionFromClient::die()
{
    // FIXME: When handling multiple workers in the same process,
    //     this logic needs to be smarter (only when all workers are dead, etc).
    Core::EventLoop::current().quit(0);
}

void ConnectionFromClient::request_file(Web::FileRequest request)
{
#if !defined(AK_OS_RINOS)
    auto path = request.path();
#endif
    auto request_id = ++last_id;

    m_requested_files.set(request_id, move(request));

#if defined(AK_OS_RINOS)
    // A WebWorker must not inherit ambient filesystem access. File URLs stay
    // unavailable until this request is mediated by the File Portal/FSAS.
    handle_file_return(EPERM, {}, request_id);
#else
    // FIXME: Route this to FSAS or browser process as appropriate instead of allowing
    //        the WebWorker process filesystem access
    auto file = Core::File::open(path, Core::File::OpenMode::Read);

    if (file.is_error())
        handle_file_return(file.error().code(), {}, request_id);
    else
        handle_file_return(0, IPC::File::adopt_file(file.release_value()), request_id);
#endif
}

ConnectionFromClient::ConnectionFromClient(NonnullOwnPtr<IPC::Transport> transport)
    : IPC::ConnectionFromClient<WebWorkerClientEndpoint, WebWorkerServerEndpoint>(*this, move(transport), 1)
    , m_page_host(PageHost::create(Web::Bindings::main_thread_vm(), *this))
{
}

ConnectionFromClient::~ConnectionFromClient() = default;

Web::Page& ConnectionFromClient::page()
{
    return m_page_host->page();
}

Web::Page const& ConnectionFromClient::page() const
{
    return m_page_host->page();
}

void ConnectionFromClient::start_worker(URL::URL url, Web::Bindings::WorkerType type, Web::Bindings::RequestCredentials credentials, String name, Web::HTML::TransferDataEncoder implicit_port, Web::HTML::SerializedEnvironmentSettingsObject outside_settings, Web::Bindings::AgentType agent_type)
{
    m_worker_host = make_ref_counted<WorkerHost>(move(url), type, move(name));

    bool const is_shared = agent_type == Web::Bindings::AgentType::SharedWorker;
    VERIFY(is_shared || agent_type == Web::Bindings::AgentType::DedicatedWorker);

    // FIXME: Add an assertion that the agent_type passed here is the same that was passed at process creation to initialize_main_thread_vm()

    m_worker_host->run(page(), move(implicit_port), outside_settings, credentials, is_shared);
}

void ConnectionFromClient::handle_file_return(i32 error, Optional<IPC::File> file, i32 request_id)
{
    auto file_request = m_requested_files.take(request_id);

    VERIFY(file_request.has_value());
    VERIFY(file_request.value().on_file_request_finish);

    file_request.value().on_file_request_finish(error != 0 ? Error::from_errno(error) : ErrorOr<i32> { file->take_fd() });
}

}
