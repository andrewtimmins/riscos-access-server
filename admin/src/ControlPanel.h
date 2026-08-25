/*
  ShareFS Server - Admin GUI Control Panel

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

#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <wx/thread.h>
#include <wx/wx.h>

#include <memory>

class MainFrame;
class EmbeddedServer;

class ControlPanel : public wxPanel {
public:
  ControlPanel(wxWindow *parent, MainFrame *frame);
  ~ControlPanel();

  void RefreshFromConfig();
  void StartServer();
  void StopServer();
  void RestartServer();

  // Whether the server is currently running, so the toolbar can enable or
  // disable its transport controls to match.
  bool IsRunning() const { return m_running; }

private:
  void OnStart(wxCommandEvent &event);
  void OnStop(wxCommandEvent &event);
  void OnRestart(wxCommandEvent &event);
  void OnClearLog(wxCommandEvent &event);
  void OnEmbeddedLog(wxThreadEvent &event);
  void OnEmbeddedStopped(wxThreadEvent &event);
  void OnTimer(wxTimerEvent &event);

  void UpdateStatus();
  void AppendLog(const wxString &text);
  bool CheckSystemdStatus();
  bool IsSystemdActive();
#ifdef _WIN32
  bool CheckWindowsServiceStatus();
  bool IsWindowsServiceInstalled();
  bool StartWindowsService();
  bool StopWindowsService();
#endif

  MainFrame *m_frame;

  // State is shown on the toolbar and status bar; this panel is the log.
  wxTextCtrl *m_logView;

  // The server runs on a worker thread inside this process; see
  // EmbeddedServer.h for why it is not a child process.
  std::unique_ptr<EmbeddedServer> m_embedded;

  wxTimer m_timer;
  bool m_running = false;
  bool m_isSystemd = false;
#ifdef _WIN32
  bool m_isWindowsService = false;
#endif

  wxDECLARE_EVENT_TABLE();
};

#endif // CONTROLPANEL_H
