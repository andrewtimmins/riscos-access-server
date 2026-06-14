// ShareFS Server - Admin GUI Control Panel

#include "ControlPanel.h"
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

enum {
  ID_START = wxID_HIGHEST + 400,
  ID_STOP,
  ID_RESTART,
  ID_CLEAR_LOG,
  ID_BROWSE_CONFIG,
  ID_TIMER
};

wxBEGIN_EVENT_TABLE(ControlPanel, wxPanel)
    EVT_BUTTON(ID_START, ControlPanel::OnStart) EVT_BUTTON(ID_STOP,
                                                           ControlPanel::OnStop)
        EVT_BUTTON(ID_RESTART, ControlPanel::OnRestart)
            EVT_BUTTON(ID_CLEAR_LOG, ControlPanel::OnClearLog)
                EVT_BUTTON(ID_BROWSE_CONFIG, ControlPanel::OnBrowseConfig)
                    EVT_END_PROCESS(wxID_ANY, ControlPanel::OnProcessTerminate)
                        EVT_TIMER(ID_TIMER, ControlPanel::OnTimer)
                            wxEND_EVENT_TABLE()

                                ControlPanel::ControlPanel(wxWindow *parent,
                                                           MainFrame *frame)
    : wxPanel(parent), m_frame(frame), m_timer(this, ID_TIMER) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *title = new wxStaticText(this, wxID_ANY, "Server Control");
  ui::StyleSectionTitle(title);
  mainSizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 15);
  mainSizer->AddSpacer(4);

  wxStaticBoxSizer *statusBox =
      new wxStaticBoxSizer(wxVERTICAL, this, "Status");

  wxBoxSizer *statusRow = new wxBoxSizer(wxHORIZONTAL);
  m_statusLabel = new wxStaticText(this, wxID_ANY, "Stopped");
  wxFont statusFont = m_statusLabel->GetFont();
  statusFont.SetPointSize(statusFont.GetPointSize() + 1);
  statusFont.SetWeight(wxFONTWEIGHT_BOLD);
  m_statusLabel->SetFont(statusFont);
  m_statusLabel->SetForegroundColour(*wxRED);
  m_statusLabel->SetMinSize(wxSize(280, -1));
  statusRow->Add(m_statusLabel, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

  wxBoxSizer *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
  m_startBtn = new wxButton(this, ID_START, "Start");
  m_stopBtn = new wxButton(this, ID_STOP, "Stop");
  m_restartBtn = new wxButton(this, ID_RESTART, "Restart");
  m_stopBtn->Disable();
  m_restartBtn->Disable();
  buttonSizer->Add(m_startBtn, 0, wxRIGHT, 5);
  buttonSizer->Add(m_stopBtn, 0, wxRIGHT, 5);
  buttonSizer->Add(m_restartBtn, 0);
  statusRow->Add(buttonSizer, 0, wxALIGN_CENTER_VERTICAL);
  statusBox->Add(statusRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

  m_pidLabel = new wxStaticText(this, wxID_ANY, "");
  m_pidLabel->SetForegroundColour(ui::MutedText(this));
  statusBox->Add(m_pidLabel, 0, wxALL, 10);

  mainSizer->Add(statusBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);

  // Config section
  wxFlexGridSizer *configGrid = new wxFlexGridSizer(2, 3, 8, 10);
  configGrid->AddGrowableCol(1);

  configGrid->Add(new wxStaticText(this, wxID_ANY, "Config File:"), 0,
                  wxALIGN_CENTER_VERTICAL);
  {
    wxString defaultConfig;
#ifdef __WXMSW__
    wxString pd;
    if (!wxGetEnv("ProgramData", &pd) || pd.empty()) pd = "C:";
    defaultConfig = pd + "\\ShareFS\\sharefs.conf";
#else
    defaultConfig = "/etc/sharefs.conf";
#endif
    m_configPath = new wxTextCtrl(this, wxID_ANY, defaultConfig);
  }
  configGrid->Add(m_configPath, 1, wxEXPAND);
  wxButton *browseBtn = new wxButton(this, ID_BROWSE_CONFIG, "Browse...");
  configGrid->Add(browseBtn, 0);

  mainSizer->Add(configGrid, 0, wxEXPAND | wxALL, 15);

  // Log section
  wxBoxSizer *logHeader = new wxBoxSizer(wxHORIZONTAL);
  logHeader->Add(new wxStaticText(this, wxID_ANY, "Server Log"), 1,
                 wxALIGN_CENTER_VERTICAL);
  wxButton *clearBtn = new wxButton(this, ID_CLEAR_LOG, "Clear");
  logHeader->Add(clearBtn, 0);
  mainSizer->Add(logHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);

  m_logView =
      new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                     wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH | wxHSCROLL);
  wxFont monoFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL,
                  wxFONTWEIGHT_NORMAL);
  m_logView->SetFont(monoFont);
  mainSizer->Add(m_logView, 1, wxEXPAND | wxALL, 15);

  SetSizer(mainSizer);

  AppendLog("[ADMIN] Ready. Click Start to launch the server.\n");

  // Initial status check
  UpdateStatus();
  m_timer.Start(1000); // Check status every second
}

ControlPanel::~ControlPanel() { StopServer(); }

void ControlPanel::RefreshFromConfig() {
  // Update config path from main frame's loaded config
  const std::string &path = m_frame->GetConfigPath();
  if (!path.empty()) {
    m_configPath->SetValue(path);
  }
}

void ControlPanel::UpdateStatus() {
  bool localRunning = (m_running && m_pid > 0);
  bool systemdRunning = CheckSystemdStatus();
#ifdef _WIN32
  bool windowsServiceRunning = CheckWindowsServiceStatus();
#else
  bool windowsServiceRunning = false;
#endif

  if (localRunning) {
    m_statusLabel->SetLabel("Running (Local)");
    m_statusLabel->SetForegroundColour(wxColour(0, 150, 0));
    m_pidLabel->SetLabel(wxString::Format("PID: %ld", m_pid));
    m_startBtn->Disable();
    m_stopBtn->Enable();
    m_restartBtn->Enable();
    m_isSystemd = false;
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  } else if (systemdRunning) {
    m_statusLabel->SetLabel("Running (System Service)");
    m_statusLabel->SetForegroundColour(wxColour(0, 150, 0));
    m_pidLabel->SetLabel("Managed by systemd");
    m_startBtn->Disable();
    m_stopBtn->Enable();
    m_restartBtn->Enable();
    m_isSystemd = true;
    m_running = true; // Mark as running for logical checks
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  } else if (windowsServiceRunning) {
    m_statusLabel->SetLabel("Running (Windows Service)");
    m_statusLabel->SetForegroundColour(wxColour(0, 150, 0));
    m_pidLabel->SetLabel("Managed by Service Control Manager");
    m_startBtn->Disable();
    m_stopBtn->Enable();
    m_restartBtn->Enable();
#ifdef _WIN32
    m_isWindowsService = true;
#endif
    m_running = true; // Mark as running for logical checks
    m_isSystemd = false;
  } else {
    m_statusLabel->SetLabel("Stopped");
    m_statusLabel->SetForegroundColour(*wxRED);
    m_pidLabel->SetLabel("");
    m_startBtn->Enable();
    m_stopBtn->Disable();
    m_restartBtn->Disable();
    m_running = false;
    m_isSystemd = false;
#ifdef _WIN32
    m_isWindowsService = false;
#endif
  }
}

void ControlPanel::AppendLog(const wxString &text) {
  // Trim oldest lines when the log grows too large
  if (m_logView->GetNumberOfLines() > 5000) {
    long trimPos = m_logView->XYToPosition(0, 1000);
    if (trimPos > 0) m_logView->Remove(0, trimPos);
  }
  m_logView->AppendText(text);
  m_logView->ShowPosition(m_logView->GetLastPosition());
}

void ControlPanel::ReadProcessOutput() {
  if (!m_process)
    return;

  wxInputStream *in = m_process->GetInputStream();
  if (in && in->CanRead()) {
    wxTextInputStream tis(*in);
    while (in->CanRead()) {
      wxString line = tis.ReadLine();
      if (!line.empty()) {
        AppendLog(line + "\n");
      }
    }
  }

  wxInputStream *err = m_process->GetErrorStream();
  if (err && err->CanRead()) {
    wxTextInputStream tis(*err);
    while (err->CanRead()) {
      wxString line = tis.ReadLine();
      if (!line.empty()) {
        AppendLog(line + "\n");
      }
    }
  }
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

  wxString config = m_configPath->GetValue();

  if (config.empty()) {
    AppendLog("[ERROR] No configuration file specified.\n");
    return;
  }

  // Find the ShareFS Server binary; search common locations in priority order
  wxFileName exePath(wxStandardPaths::Get().GetExecutablePath());
  wxString exeDir = exePath.GetPath();
  wxString sep = wxFileName::GetPathSeparator();

  wxArrayString candidates;
  candidates.Add(exeDir + sep + ".." + sep + "src" + sep + "sharefs-server");
  candidates.Add(exeDir + sep + "sharefs-server");
#ifndef _WIN32
  candidates.Add("/usr/local/bin/sharefs-server");
  candidates.Add("/usr/bin/sharefs-server");
#else
  candidates.Add("C:\\ShareFS\\sharefs-server.exe");
#endif

  wxString serverPath = "sharefs-server"; // fallback: search PATH
  for (size_t i = 0; i < candidates.GetCount(); ++i) {
    if (wxFileExists(candidates[i])) {
      serverPath = candidates[i];
      break;
    }
  }

  // Build command
  wxString cmd = serverPath;
  cmd += " " + config;

  // Create process
  m_process = new wxProcess(this);
  m_process->Redirect();

  m_pid = wxExecute(cmd, wxEXEC_ASYNC | wxEXEC_MAKE_GROUP_LEADER, m_process);

  if (m_pid <= 0) {
    AppendLog("[ERROR] Failed to start server.\n");
    delete m_process;
    m_process = nullptr;
    return;
  }

  m_running = true;
  m_isSystemd = false;
#ifdef _WIN32
  m_isWindowsService = false;
#endif
  AppendLog(wxString::Format("[ADMIN] Server started (PID %ld)\n", m_pid));
  UpdateStatus();

  // Start timer to read output
  m_timer.Start(500);
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

  if (m_pid <= 0)
    return;

  m_timer.Stop();
  m_running = false; // Set early to prevent race conditions

  AppendLog("[ADMIN] Stopping server...\n");

  // Send termination signal
  if (m_pid > 0) {
    wxKill(m_pid, wxSIGTERM);
  }

  // Give it a moment to terminate
  wxMilliSleep(300);

  // Clean up process object if still exists
  if (m_process) {
    // Detach to avoid crash - let the event handler clean up
    m_process->Detach();
    m_process = nullptr;
  }

  m_pid = 0;
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

void ControlPanel::OnBrowseConfig(wxCommandEvent &event) {
  wxUnusedVar(event);

  wxFileDialog dlg(this, "Select Configuration File", "", "",
                   "Config files (*.conf)|*.conf|All files (*.*)|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);

  if (dlg.ShowModal() == wxID_OK) {
    m_configPath->SetValue(dlg.GetPath());
  }
}

void ControlPanel::OnProcessTerminate(wxProcessEvent &event) {
  wxUnusedVar(event);

  m_timer.Stop();

  if (m_process) {
    ReadProcessOutput();
    int exitCode = event.GetExitCode();
    AppendLog(
        wxString::Format("[ADMIN] Server exited with code %d\n", exitCode));
    delete m_process;
    m_process = nullptr;
  }

  m_running = false;
  m_pid = 0;
  UpdateStatus();
}

void ControlPanel::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  if (!m_isSystemd && m_pid > 0) {
    ReadProcessOutput();
  }
  if (m_running && m_isSystemd) {
    // Periodically verify systemd is still running
    if (!CheckSystemdStatus()) {
      UpdateStatus(); // Will detect stopped state
    }
  } else if (!m_running && !m_pid) {
    // Periodically checking if it started externally
    UpdateStatus();
  }
  // Note: UpdateStatus calls CheckSystemdStatus appropriately
}
