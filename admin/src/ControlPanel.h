/*
  ShareFS Server - Activity Panel

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

  // Whether ShareFS is sharing at all, however it happens to be doing it, so
  // the toolbar can enable or disable its transport controls to match.
  bool IsRunning() const { return m_mode != Mode::Stopped; }

  // Turn background sharing on or off from somewhere other than the tick box,
  // which is what the first-run dialog does. Reports failure to the log and to
  // the user, and leaves the tick box showing the truth either way.
  void SetKeepSharing(bool enabled);

  // Whether background sharing is set up, for the first-run dialog and the
  // window's close handler.
  bool KeepSharingEnabled() const;

private:
  // How sharing is happening. The user is never asked to choose between these:
  // they choose whether sharing survives the window closing, and that decides
  // it. See src/autostart.h.
  enum class Mode {
    Stopped,
    // The server core is running on a thread inside this process, and stops
    // when the window does.
    InApp,
    // A service or launchd agent is running it, and keeps going without us.
    Background
  };

  void OnClearLog(wxCommandEvent &event);
  void OnKeepSharing(wxCommandEvent &event);
  void OnEmbeddedLog(wxThreadEvent &event);
  void OnEmbeddedStopped(wxThreadEvent &event);
  void OnTimer(wxTimerEvent &event);

  void UpdateStatus();
  void AppendLog(const wxString &text);
  bool StartInApp();
  void StopInApp();

  // Ask the operating system what the background copy is doing. Both answers
  // cost a process on Linux and macOS, so they are cached rather than asked
  // for on every repaint; PollBackground refreshes them.
  void PollBackground();

  MainFrame *m_frame;

  wxStaticText *m_statusLine;
  wxStaticText *m_statusDetail;
  wxCheckBox *m_keepSharing;
  wxTextCtrl *m_logView;

  // The server runs on a worker thread inside this process; see
  // EmbeddedServer.h for why it is not a child process.
  std::unique_ptr<EmbeddedServer> m_embedded;

  wxTimer m_timer;
  Mode m_mode = Mode::Stopped;
  bool m_backgroundRunning = false;
  bool m_keepEnabled = false;
  bool m_autostartSupported = true;
  int m_tick = 0;

  wxDECLARE_EVENT_TABLE();
};

#endif // CONTROLPANEL_H
