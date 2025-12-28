// RISC OS Access Server - Admin GUI Control Panel

#include "ControlPanel.h"
#include "MainFrame.h"
#include <wx/filedlg.h>
#include <wx/txtstrm.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>

enum {
    ID_START = wxID_HIGHEST + 400,
    ID_STOP,
    ID_RESTART,
    ID_CLEAR_LOG,
    ID_BROWSE_CONFIG,
    ID_TIMER
};

wxBEGIN_EVENT_TABLE(ControlPanel, wxPanel)
    EVT_BUTTON(ID_START, ControlPanel::OnStart)
    EVT_BUTTON(ID_STOP, ControlPanel::OnStop)
    EVT_BUTTON(ID_RESTART, ControlPanel::OnRestart)
    EVT_BUTTON(ID_CLEAR_LOG, ControlPanel::OnClearLog)
    EVT_BUTTON(ID_BROWSE_CONFIG, ControlPanel::OnBrowseConfig)
    EVT_END_PROCESS(wxID_ANY, ControlPanel::OnProcessTerminate)
    EVT_TIMER(ID_TIMER, ControlPanel::OnTimer)
wxEND_EVENT_TABLE()

ControlPanel::ControlPanel(wxWindow* parent, MainFrame* frame)
    : wxPanel(parent), m_frame(frame), m_timer(this, ID_TIMER)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Title
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Server Control");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(14);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    mainSizer->Add(title, 0, wxALL, 15);
    
    // Status section
    wxStaticBoxSizer* statusBox = new wxStaticBoxSizer(wxHORIZONTAL, this, "Status");
    
    wxBoxSizer* statusLeft = new wxBoxSizer(wxVERTICAL);
    m_statusLabel = new wxStaticText(this, wxID_ANY, "Stopped");
    wxFont statusFont = m_statusLabel->GetFont();
    statusFont.SetPointSize(12);
    statusFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_statusLabel->SetFont(statusFont);
    m_statusLabel->SetForegroundColour(*wxRED);
    statusLeft->Add(m_statusLabel, 0);
    
    m_pidLabel = new wxStaticText(this, wxID_ANY, "");
    m_pidLabel->SetForegroundColour(wxColour(100, 100, 100));
    statusLeft->Add(m_pidLabel, 0, wxTOP, 3);
    statusBox->Add(statusLeft, 1, wxALL, 10);
    
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    m_startBtn = new wxButton(this, ID_START, "Start");
    m_stopBtn = new wxButton(this, ID_STOP, "Stop");
    m_restartBtn = new wxButton(this, ID_RESTART, "Restart");
    m_stopBtn->Disable();
    m_restartBtn->Disable();
    buttonSizer->Add(m_startBtn, 0, wxRIGHT, 5);
    buttonSizer->Add(m_stopBtn, 0, wxRIGHT, 5);
    buttonSizer->Add(m_restartBtn, 0);
    statusBox->Add(buttonSizer, 0, wxALL | wxALIGN_CENTER_VERTICAL, 10);
    
    mainSizer->Add(statusBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);
    
    // Config section
    wxFlexGridSizer* configGrid = new wxFlexGridSizer(2, 3, 8, 10);
    configGrid->AddGrowableCol(1);
    
    configGrid->Add(new wxStaticText(this, wxID_ANY, "Config File:"), 0, wxALIGN_CENTER_VERTICAL);
    m_configPath = new wxTextCtrl(this, wxID_ANY, "access.conf");
    configGrid->Add(m_configPath, 1, wxEXPAND);
    wxButton* browseBtn = new wxButton(this, ID_BROWSE_CONFIG, "Browse...");
    configGrid->Add(browseBtn, 0);
    
    mainSizer->Add(configGrid, 0, wxEXPAND | wxALL, 15);
    
    // Log section
    wxBoxSizer* logHeader = new wxBoxSizer(wxHORIZONTAL);
    logHeader->Add(new wxStaticText(this, wxID_ANY, "Server Log"), 1, wxALIGN_CENTER_VERTICAL);
    wxButton* clearBtn = new wxButton(this, ID_CLEAR_LOG, "Clear");
    logHeader->Add(clearBtn, 0);
    mainSizer->Add(logHeader, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);
    
    m_logView = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH | wxHSCROLL);
    wxFont monoFont(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    m_logView->SetFont(monoFont);
    mainSizer->Add(m_logView, 1, wxEXPAND | wxALL, 15);
    
    SetSizer(mainSizer);
    
    AppendLog("[ADMIN] Ready. Click Start to launch the server.\n");
}

ControlPanel::~ControlPanel() {
    StopServer();
}

void ControlPanel::RefreshFromConfig() {
    // Update config path from main frame's loaded config
    const std::string& path = m_frame->GetConfigPath();
    if (!path.empty()) {
        m_configPath->SetValue(path);
    }
}

void ControlPanel::UpdateStatus() {
    if (m_running && m_pid > 0) {
        m_statusLabel->SetLabel("Running");
        m_statusLabel->SetForegroundColour(wxColour(0, 150, 0));
        m_pidLabel->SetLabel(wxString::Format("PID: %ld", m_pid));
        m_startBtn->Disable();
        m_stopBtn->Enable();
        m_restartBtn->Enable();
    } else {
        m_statusLabel->SetLabel("Stopped");
        m_statusLabel->SetForegroundColour(*wxRED);
        m_pidLabel->SetLabel("");
        m_startBtn->Enable();
        m_stopBtn->Disable();
        m_restartBtn->Disable();
    }
}

void ControlPanel::AppendLog(const wxString& text) {
    m_logView->AppendText(text);
    m_logView->ShowPosition(m_logView->GetLastPosition());
}

void ControlPanel::ReadProcessOutput() {
    if (!m_process) return;
    
    wxInputStream* in = m_process->GetInputStream();
    if (in && in->CanRead()) {
        wxTextInputStream tis(*in);
        while (in->CanRead()) {
            wxString line = tis.ReadLine();
            if (!line.empty()) {
                AppendLog(line + "\n");
            }
        }
    }
    
    wxInputStream* err = m_process->GetErrorStream();
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

void ControlPanel::OnStart(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    if (m_running) return;
    
    wxString config = m_configPath->GetValue();
    
    if (config.empty()) {
        AppendLog("[ERROR] No configuration file specified.\n");
        return;
    }
    
    // Find the access binary - look relative to this executable
    wxFileName exePath(wxStandardPaths::Get().GetExecutablePath());
    wxString accessPath = exePath.GetPath() + wxFileName::GetPathSeparator() + ".." + 
                          wxFileName::GetPathSeparator() + "src" + 
                          wxFileName::GetPathSeparator() + "access";
    
    // If that doesn't exist, try same directory
    if (!wxFileExists(accessPath)) {
        accessPath = exePath.GetPath() + wxFileName::GetPathSeparator() + "access";
    }
    
    // If still not found, try just "access" in PATH
    if (!wxFileExists(accessPath)) {
        accessPath = "access";
    }
    
    // Build command
    wxString cmd = accessPath;
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
    AppendLog(wxString::Format("[ADMIN] Server started (PID %ld)\n", m_pid));
    UpdateStatus();
    
    // Start timer to read output
    m_timer.Start(500);
}

void ControlPanel::OnStop(wxCommandEvent& event) {
    wxUnusedVar(event);
    StopServer();
}

void ControlPanel::StopServer() {
    if (!m_running || m_pid <= 0) return;
    
    m_timer.Stop();
    m_running = false;  // Set early to prevent race conditions
    
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

void ControlPanel::OnRestart(wxCommandEvent& event) {
    wxUnusedVar(event);
    RestartServer();
}

void ControlPanel::RestartServer() {
    StopServer();
    wxMilliSleep(500);
    wxCommandEvent dummy;
    OnStart(dummy);
}

void ControlPanel::OnClearLog(wxCommandEvent& event) {
    wxUnusedVar(event);
    m_logView->Clear();
}

void ControlPanel::OnBrowseConfig(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    wxFileDialog dlg(this, "Select Configuration File", "", "",
                     "Config files (*.conf)|*.conf|All files (*.*)|*.*",
                     wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    
    if (dlg.ShowModal() == wxID_OK) {
        m_configPath->SetValue(dlg.GetPath());
    }
}

void ControlPanel::OnProcessTerminate(wxProcessEvent& event) {
    wxUnusedVar(event);
    
    m_timer.Stop();
    
    if (m_process) {
        ReadProcessOutput();
        int exitCode = event.GetExitCode();
        AppendLog(wxString::Format("[ADMIN] Server exited with code %d\n", exitCode));
        delete m_process;
        m_process = nullptr;
    }
    
    m_running = false;
    m_pid = 0;
    UpdateStatus();
}

void ControlPanel::OnTimer(wxTimerEvent& event) {
    wxUnusedVar(event);
    ReadProcessOutput();
}
