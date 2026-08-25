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

#include "ControlPanel.h"
#include "EmbeddedServer.h"
#include "MainFrame.h"
#include "UiHelpers.h"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/txtstrm.h>

#ifdef _WIN32
#include <windows.h>
#define SHAREFS_SERVICE_NAME TEXT("ShareFSServer")
#endif

// Prefixed to avoid colliding with the toolbar ids declared in MainFrame.h.
enum {
  CP_ID_START = wxID_HIGHEST + 400,
  CP_ID_STOP,
  CP_ID_RESTART,
  CP_ID_CLEAR_LOG,
  CP_ID_TIMER
};

wxBEGIN_EVENT_TABLE(ControlPanel, wxPanel)
    EVT_BUTTON(CP_ID_START, ControlPanel::OnStart) EVT_BUTTON(CP_ID_STOP,
                                                           ControlPanel::OnStop)
        EVT_BUTTON(CP_ID_RESTART, ControlPanel::OnRestart)
            EVT_BUTTON(CP_ID_CLEAR_LOG, ControlPanel::OnClearLog)
                        EVT_TIMER(CP_ID_TIMER, ControlPanel::OnTimer)
                            wxEND_EVENT_TABLE()

                                ControlPanel::ControlPanel(wxWindow *parent,
                                                           MainFrame *frame)
    : wxPanel(parent), m_frame(frame), m_timer(this, CP_ID_TIMER) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *title = new wxStaticText(this, wxID_ANY, "Server Activity");
  ui::StyleSectionTitle(title);
  mainSizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, ui::kPagePad);
  mainSizer->AddSpacer(4);

  wxStaticText *desc = new wxStaticText(
      this, wxID_ANY,
      "Live output from the server. Start and stop it from the toolbar.");
  desc->SetForegroundColour(ui::MutedText(this));
  mainSizer->Add(desc, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::kPagePad);

  wxBoxSizer *logHeader = new wxBoxSizer(wxHORIZONTAL);
  logHeader->Add(new wxStaticText(this, wxID_ANY, "Server Log"), 1,
                 wxALIGN_CENTER_VERTICAL);
  wxButton *clearBtn = new wxButton(this, CP_ID_CLEAR_LOG, "Clear");
  logHeader->Add(clearBtn, 0);
  mainSizer->Add(logHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  mainSizer->AddSpacer(ui::kTightGap);

  m_logView =
      new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH | wxHSCROLL);
  wxFont monoFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL);
  m_logView->SetFont(monoFont);
  mainSizer->Add(m_logView, 1,
                 wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, ui::kPagePad);

  SetSizer(mainSizer);

  // Log lines and the stop notification arrive from the server thread as
  // queued events; see EmbeddedServer.h.
  Bind(EVT_EMBEDDED_SERVER_LOG, &ControlPanel::OnEmbeddedLog, this);
  Bind(EVT_EMBEDDED_SERVER_STOPPED, &ControlPanel::OnEmbeddedStopped, this);

  AppendLog("[ADMIN] Ready. Click Start to launch the server.\n");

  // Initial status check
  UpdateStatus();
  m_timer.Start(1000); // Check status every second
}

ControlPanel::~ControlPanel() { StopServer(); }

void ControlPanel::RefreshFromConfig() { UpdateStatus(); }

void ControlPanel::UpdateStatus() {
  const bool localRunning = (m_embedded && m_embedded->IsRunning());
  const bool systemdRunning = CheckSystemdStatus();
#ifdef _WIN32
  const bool windowsServiceRunning = CheckWindowsServiceStatus();
#else
  const bool windowsServiceRunning = false;
#endif

  wxString label;
  if (localRunning) {
    label = "Running";
    m_running = true;
    m_isSystemd = false;
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  } else if (systemdRunning) {
    label = "Running (System Service)";
    m_running = true;
    m_isSystemd = true;
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  } else if (windowsServiceRunning) {
    label = "Running (Windows Service)";
    m_running = true;
    m_isSystemd = false;
#ifdef _WIN32
    m_isWindowsService = true;
#endif
  } else {
    label = "Stopped";
    m_running = false;
    m_isSystemd = false;
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  }

  // The toolbar and status bar are the only places state is shown now.
  if (m_frame) {
    m_frame->SetServerStatus(label, m_running);
    m_frame->SetTransportState(m_running);
  }
}

// Pick a colour for one log line from its severity, so problems stand out
// instead of being buried in a wall of monospace.
static wxColour LogLineColour(const wxString &line) {
  if (line.Contains("ERROR") || line.Contains("Error") ||
      line.Contains("[ERROR]") || line.Contains("failed") ||
      line.Contains("Failed"))
    return ui::Danger();
  if (line.Contains("Warning") || line.Contains("WARN"))
    return ui::Warning();
  if (line.StartsWith("[ADMIN]"))
    return ui::Neutral();
  return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

void ControlPanel::AppendLog(const wxString &text) {
  // Trim oldest lines when the log grows too large
  if (m_logView->GetNumberOfLines() > 5000) {
    long trimPos = m_logView->XYToPosition(0, 1000);
    if (trimPos > 0) m_logView->Remove(0, trimPos);
  }

  // Style each line individually; a single write may carry several.
  size_t start = 0;
  while (start < text.length()) {
    size_t nl = text.find('\n', start);
    bool hasNewline = (nl != wxString::npos);
    size_t end = hasNewline ? nl + 1 : text.length();
    wxString line = text.Mid(start, end - start);

    m_logView->SetDefaultStyle(wxTextAttr(LogLineColour(line)));
    m_logView->AppendText(line);

    start = end;
  }

  m_logView->SetDefaultStyle(
      wxTextAttr(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)));
  m_logView->ShowPosition(m_logView->GetLastPosition());
}

bool ControlPanel::CheckSystemdStatus() {
#ifdef __WXGTK__
  // silent check, returns 0 if active
  return wxExecute("systemctl is-active --quiet sharefs",
                   wxEXEC_SYNC | wxEXEC_NOEVENTS) == 0;
#else
  return false;
#endif
}

#ifdef _WIN32
bool ControlPanel::CheckWindowsServiceStatus() {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return false;

  SC_HANDLE service =
      OpenService(scm, SHAREFS_SERVICE_NAME, SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return false;
  }

  SERVICE_STATUS status;
  bool isRunning = false;
  if (QueryServiceStatus(service, &status)) {
    isRunning = (status.dwCurrentState == SERVICE_RUNNING);
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return isRunning;
}

bool ControlPanel::IsWindowsServiceInstalled() {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return false;

  SC_HANDLE service =
      OpenService(scm, SHAREFS_SERVICE_NAME, SERVICE_QUERY_STATUS);
  bool installed = (service != NULL);

  if (service)
    CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return installed;
}

bool ControlPanel::StartWindowsService() {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return false;

  SC_HANDLE service =
      OpenService(scm, SHAREFS_SERVICE_NAME, SERVICE_START | SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return false;
  }

  bool success = false;
  if (StartService(service, 0, NULL) || GetLastError() == ERROR_SERVICE_ALREADY_RUNNING) {
    // Poll for running state, yielding to keep the UI responsive
    SERVICE_STATUS status;
    for (int i = 0; i < 50; ++i) { // up to ~10s
      if (QueryServiceStatus(service, &status) &&
          status.dwCurrentState == SERVICE_RUNNING) {
        success = true;
        break;
      }
      wxSafeYield();
      Sleep(200);
    }
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return success;
}

bool ControlPanel::StopWindowsService() {
  SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
  if (!scm)
    return false;

  SC_HANDLE service =
      OpenService(scm, SHAREFS_SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
  if (!service) {
    CloseServiceHandle(scm);
    return false;
  }

  SERVICE_STATUS status;
  bool success = false;

  if (ControlService(service, SERVICE_CONTROL_STOP, &status) ||
      GetLastError() == ERROR_SERVICE_NOT_ACTIVE) {
    // Poll for stopped state, yielding to keep the UI responsive
    for (int i = 0; i < 50; ++i) { // up to ~10s
      if (QueryServiceStatus(service, &status) &&
          status.dwCurrentState == SERVICE_STOPPED) {
        success = true;
        break;
      }
      wxSafeYield();
      Sleep(200);
    }
  }

  CloseServiceHandle(service);
  CloseServiceHandle(scm);
  return success;
}
#endif

bool ControlPanel::IsSystemdActive() { return m_isSystemd; }

void ControlPanel::OnStart(wxCommandEvent &event) {
  wxUnusedVar(event);
  StartServer();
}

void ControlPanel::StartServer() {
  if (m_running)
    return;

    // Check if Windows service is available and try to start that way first
#ifdef _WIN32
  if (IsWindowsServiceInstalled()) {
    AppendLog("[ADMIN] Attempting to start Windows service...\n");
    if (StartWindowsService()) {
      AppendLog("[ADMIN] Windows service started.\n");
      UpdateStatus();
      return;
    }
    AppendLog("[ADMIN] Windows service start failed. Falling back to local "
              "process.\n");
  }
#endif

  // Check if systemd is available and try to start that way first on Linux
#ifdef __WXGTK__
  if (wxExecute("systemctl list-unit-files sharefs.service",
                wxEXEC_SYNC | wxEXEC_NOEVENTS) == 0) {
    AppendLog("[ADMIN] Attempting to start system service...\n");
    wxString display, waylandDisplay;
    if ((!wxGetEnv("DISPLAY", &display) || display.empty()) &&
        (!wxGetEnv("WAYLAND_DISPLAY", &waylandDisplay) || waylandDisplay.empty())) {
      AppendLog("[WARN] No graphical display detected; pkexec elevation may fail.\n");
    }
    long ret =
        wxExecute("pkexec systemctl start sharefs", wxEXEC_SYNC);
    if (ret == 0 || CheckSystemdStatus()) {
      AppendLog("[ADMIN] System service started.\n");
      UpdateStatus();
      return;
    }
    AppendLog("[ADMIN] System service start failed. Falling back to local "
              "process.\n");
  }
#endif

  const wxString config = m_frame->GetConfigPath();

  if (config.empty()) {
    AppendLog("[ERROR] No configuration file specified.\n");
    return;
  }

  // Run the server inside this process rather than spawning sharefs-server.
  // The status is then a fact rather than a guess, log lines arrive directly,
  // and there is no binary to go looking for.
  if (!m_embedded)
    m_embedded.reset(new EmbeddedServer(this));

  wxString error;
  if (!m_embedded->Start(config, error)) {
    AppendLog("[ERROR] " + error + "\n");
    UpdateStatus();
    return;
  }

  m_running = true;
  m_isSystemd = false;
#ifdef _WIN32
  m_isWindowsService = false;
#endif
  AppendLog("[ADMIN] Server started (in-process).\n");
  UpdateStatus();
}

void ControlPanel::OnStop(wxCommandEvent &event) {
  wxUnusedVar(event);
  StopServer();
}

void ControlPanel::StopServer() {
  if (!m_running)
    return;

#ifdef _WIN32
  if (m_isWindowsService) {
    AppendLog("[ADMIN] Stopping Windows service...\n");
    if (StopWindowsService()) {
      AppendLog("[ADMIN] Windows service stopped.\n");
    } else {
      AppendLog("[ADMIN] Failed to stop Windows service.\n");
    }
    UpdateStatus();
    return;
  }
#endif

  if (m_isSystemd) {
    AppendLog("[ADMIN] Stopping system service...\n");
#ifdef __WXGTK__
    long ret =
        wxExecute("pkexec systemctl stop sharefs", wxEXEC_SYNC);
    if (ret == 0) {
      AppendLog("[ADMIN] System service stopped.\n");
    } else {
      AppendLog("[ADMIN] Failed to stop system service.\n");
    }
#endif
    UpdateStatus();
    return;
  }

  if (!m_embedded || !m_embedded->IsRunning())
    return;

  AppendLog("[ADMIN] Stopping server...\n");

  // Blocks until the server thread has unwound and released its sockets, so a
  // restart immediately afterwards can bind them again.
  m_embedded->Stop();

  m_running = false;
  AppendLog("[ADMIN] Server stopped.\n");
  UpdateStatus();
}

void ControlPanel::OnRestart(wxCommandEvent &event) {
  wxUnusedVar(event);
  RestartServer();
}

void ControlPanel::RestartServer() {
#ifdef _WIN32
  if (m_isWindowsService) {
    AppendLog("[ADMIN] Restarting Windows service...\n");
    StopWindowsService();
    wxMilliSleep(500);
    if (StartWindowsService()) {
      wxMilliSleep(500);
      UpdateStatus();
    } else {
      AppendLog("[ADMIN] Failed to restart Windows service.\n");
      UpdateStatus();
    }
    return;
  }
#endif

  if (m_isSystemd) {
    AppendLog("[ADMIN] Restarting system service...\n");
#ifdef __WXGTK__
    wxExecute("pkexec systemctl restart sharefs", wxEXEC_SYNC);
#endif
    UpdateStatus(); // timer will confirm state shortly
  } else {
    StopServer();
    wxMilliSleep(500);
    wxCommandEvent dummy;
    OnStart(dummy);
  }
}

void ControlPanel::OnClearLog(wxCommandEvent &event) {
  wxUnusedVar(event);
  m_logView->Clear();
}

// A log line from the embedded server, marshalled onto the GUI thread.
void ControlPanel::OnEmbeddedLog(wxThreadEvent &event) {
  AppendLog(event.GetString() + "\n");
}

// The server thread has finished, whether we asked it to or not.
void ControlPanel::OnEmbeddedStopped(wxThreadEvent &event) {
  wxUnusedVar(event);
  m_running = false;
  UpdateStatus();
}

void ControlPanel::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  if (m_running && m_isSystemd) {
    // Periodically verify systemd is still running
    if (!CheckSystemdStatus()) {
      UpdateStatus(); // Will detect stopped state
    }
  } else if (!m_running) {
    // Periodically check whether a system service started it behind our back.
    UpdateStatus();
  }
  // Note: UpdateStatus calls CheckSystemdStatus appropriately
}
