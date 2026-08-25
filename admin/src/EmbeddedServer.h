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

// Runs the server core on a worker thread inside the admin process, rather
// than launching sharefs-server as a child. That removes the binary-discovery
// problem entirely, makes the reported status authoritative instead of probed,
// and delivers log lines straight from the server rather than through a pipe.
//
// The system service paths (systemd, the Windows SCM) are unaffected: when the
// server is managed by one of those, ControlPanel drives it that way instead.

#ifndef EMBEDDEDSERVER_H
#define EMBEDDEDSERVER_H

#include <wx/event.h>
#include <wx/string.h>
#include <wx/thread.h>

#include <memory>

// Emitted on the owner's thread for each log line the embedded server writes,
// and once more when the server thread has finished.
wxDECLARE_EVENT(EVT_EMBEDDED_SERVER_LOG, wxThreadEvent);
wxDECLARE_EVENT(EVT_EMBEDDED_SERVER_STOPPED, wxThreadEvent);

class EmbeddedServerThread;

class EmbeddedServer {
public:
  explicit EmbeddedServer(wxEvtHandler *owner);
  ~EmbeddedServer();

  EmbeddedServer(const EmbeddedServer &) = delete;
  EmbeddedServer &operator=(const EmbeddedServer &) = delete;

  // Load configPath, bind the sockets and start the server thread. On failure
  // returns false and fills error with something worth showing the user; the
  // sockets are bound here, on the caller's thread, so that "port already in
  // use" is reported synchronously rather than arriving as a log line.
  bool Start(const wxString &configPath, wxString &error);

  // Ask the thread to exit and wait for it. Safe to call when not running.
  void Stop();

  bool IsRunning() const;

private:
  friend class EmbeddedServerThread;

  wxEvtHandler *m_owner;
  EmbeddedServerThread *m_thread = nullptr;
};

#endif // EMBEDDEDSERVER_H
