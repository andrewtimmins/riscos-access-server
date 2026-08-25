/*
  ShareFS Server - In-process server host for the admin GUI

  Copyright (C) 2025-2026 Andy Timmins

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "EmbeddedServer.h"

extern "C" {
#include "config.h"
#include "handle.h"
#include "log.h"
#include "net.h"
#include "platform.h"
#include "printer.h"
#include "server.h"
}

wxDEFINE_EVENT(EVT_EMBEDDED_SERVER_LOG, wxThreadEvent);
wxDEFINE_EVENT(EVT_EMBEDDED_SERVER_STOPPED, wxThreadEvent);

// The worker owns every piece of server state for the lifetime of one run:
// config, sockets and handle table are all created before the thread starts
// and torn down after it finishes, so nothing is shared with the GUI thread
// except the log sink, which only posts events.
class EmbeddedServerThread : public wxThread {
public:
  EmbeddedServerThread(EmbeddedServer *host, wxEvtHandler *owner)
      : wxThread(wxTHREAD_JOINABLE), m_host(host), m_owner(owner) {}

  // Called on the GUI thread before Run(). Anything that can fail in a way the
  // user should see immediately belongs here rather than in Entry().
  bool Prepare(const wxString &configPath, wxString &error) {
    if (sfs_config_load(configPath.utf8_str(), &m_config) != 0) {
      error = "Could not read " + configPath;
      return false;
    }
    m_configLoaded = true;

    if (sfs_config_validate(&m_config) != 0) {
      error = "The configuration is not valid. Check that every share has a "
              "name and a path, and that every printer has a definition and a "
              "command.";
      return false;
    }

    // Open the log where the configuration asks, matching the standalone
    // server. Re-initialised per run so a changed log_file takes effect.
    sfs_log_shutdown();
    sfs_log_set_path(m_config.server.log_file);
    sfs_log_init();
    sfs_log_set_level(sfs_log_level_from_string(m_config.server.log_level));

    if (sfs_net_open(&m_net, m_config.server.bind_ip) != 0) {
      error = "Could not bind the server sockets. Another copy of the server "
              "may already be running, or UDP ports 32770, 32771 and 49171 may "
              "be in use.";
      return false;
    }
    m_netOpen = true;

    sfs_handles_init(&m_handles);
    m_handlesInit = true;
    return true;
  }

  // Log lines arrive here on the server thread. Post to the owner rather than
  // touching any widget: wxWidgets GUI calls are main-thread only.
  static void LogSink(sfs_log_level level, const char *line, void *user) {
    auto *self = static_cast<EmbeddedServerThread *>(user);
    if (!self || !self->m_owner)
      return;

    auto *event = new wxThreadEvent(EVT_EMBEDDED_SERVER_LOG);
    event->SetString(wxString::FromUTF8(line));
    event->SetInt(static_cast<int>(level));
    wxQueueEvent(self->m_owner, event);
  }

  void Cleanup() {
    if (m_handlesInit) {
      sfs_handles_free(&m_handles);
      m_handlesInit = false;
    }
    if (m_netOpen) {
      sfs_net_close(&m_net);
      m_netOpen = false;
    }
    sfs_printers_shutdown();
    if (m_configLoaded) {
      sfs_config_unload(&m_config);
      m_configLoaded = false;
    }
  }

protected:
  ExitCode Entry() override {
    sfs_server_clear_stop();
    sfs_log_set_sink(&EmbeddedServerThread::LogSink, this);

    sfs_server_run(&m_config, &m_net, &m_handles);

    // Detach before tearing anything down, so a late log line cannot reach a
    // half-freed object.
    sfs_log_set_sink(nullptr, nullptr);
    Cleanup();

    if (m_owner)
      wxQueueEvent(m_owner, new wxThreadEvent(EVT_EMBEDDED_SERVER_STOPPED));
    return static_cast<ExitCode>(0);
  }

private:
  EmbeddedServer *m_host;
  wxEvtHandler *m_owner;

  sfs_config m_config{};
  sfs_net m_net{};
  sfs_handle_table m_handles{};

  bool m_configLoaded = false;
  bool m_netOpen = false;
  bool m_handlesInit = false;
};

EmbeddedServer::EmbeddedServer(wxEvtHandler *owner) : m_owner(owner) {
  // Winsock needs starting before any socket call, and the admin process is now
  // the one making them. Harmless no-op on POSIX.
  sfs_platform_init();
}

EmbeddedServer::~EmbeddedServer() {
  Stop();
  sfs_log_shutdown();
  sfs_platform_shutdown();
}

bool EmbeddedServer::Start(const wxString &configPath, wxString &error) {
  if (m_thread) {
    error = "The server is already running.";
    return false;
  }

  auto *thread = new EmbeddedServerThread(this, m_owner);
  if (!thread->Prepare(configPath, error)) {
    thread->Cleanup();
    delete thread;
    return false;
  }

  if (thread->Create() != wxTHREAD_NO_ERROR ||
      thread->Run() != wxTHREAD_NO_ERROR) {
    error = "Could not start the server thread.";
    thread->Cleanup();
    delete thread;
    return false;
  }

  m_thread = thread;
  return true;
}

void EmbeddedServer::Stop() {
  if (!m_thread)
    return;

  sfs_server_request_stop();

  // Joinable thread: Wait() blocks until Entry() returns, which it does within
  // one select() timeout. Delete afterwards, as wxWidgets does not do it for
  // joinable threads.
  m_thread->Wait();
  delete m_thread;
  m_thread = nullptr;
}

bool EmbeddedServer::IsRunning() const {
  return m_thread != nullptr && m_thread->IsRunning();
}
