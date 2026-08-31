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

// This panel used to report which mechanism was running the server - a systemd
// unit, a Windows service, or a thread in this process - and had a copy of the
// code for driving each of them. The user was being shown a deployment
// mechanism and asked to care about it.
//
// Now there is one question, "keep sharing when this window is closed", and
// the platform-specific half lives in src/autostart.c behind an interface that
// answers it. What is left here is a status line, a tick box, and the log.

#include "ControlPanel.h"
#include "EmbeddedServer.h"
#include "MainFrame.h"
#include "UiHelpers.h"

extern "C" {
#include "autostart.h"
}

// Prefixed to avoid colliding with the toolbar ids declared in MainFrame.h.
enum {
  CP_ID_CLEAR_LOG = wxID_HIGHEST + 400,
  CP_ID_KEEP_SHARING,
  CP_ID_TIMER
};

wxBEGIN_EVENT_TABLE(ControlPanel, wxPanel)
    EVT_BUTTON(CP_ID_CLEAR_LOG, ControlPanel::OnClearLog)
    EVT_CHECKBOX(CP_ID_KEEP_SHARING, ControlPanel::OnKeepSharing)
    EVT_TIMER(CP_ID_TIMER, ControlPanel::OnTimer)
wxEND_EVENT_TABLE()

ControlPanel::ControlPanel(wxWindow *parent, MainFrame *frame)
    : wxPanel(parent), m_frame(frame), m_timer(this, CP_ID_TIMER) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *title = new wxStaticText(this, wxID_ANY, "Sharing");
  ui::StyleSectionTitle(title);
  mainSizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, ui::kPagePad);
  mainSizer->AddSpacer(4);

  m_statusLine = new wxStaticText(this, wxID_ANY, "Not sharing");
  wxFont statusFont = m_statusLine->GetFont();
  statusFont.MakeBold();
  m_statusLine->SetFont(statusFont);
  mainSizer->Add(m_statusLine, 0, wxLEFT | wxRIGHT, ui::kPagePad);

  m_statusDetail = new wxStaticText(this, wxID_ANY, "");
  m_statusDetail->SetForegroundColour(ui::MutedText(this));
  mainSizer->Add(m_statusDetail, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  mainSizer->AddSpacer(ui::kTightGap);

  m_keepSharing =
      new wxCheckBox(this, CP_ID_KEEP_SHARING,
                     "Keep sharing when this window is closed");
  m_keepSharing->SetToolTip(
      wxString::Format("ShareFS sets this up using %s.",
                       sfs_autostart_mechanism()));
  mainSizer->Add(m_keepSharing, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 ui::kPagePad);

  if (sfs_autostart_query() == SFS_AUTOSTART_UNSUPPORTED) {
    m_autostartSupported = false;
    m_keepSharing->Enable(false);
    m_keepSharing->SetToolTip("ShareFS cannot manage background sharing on "
                              "this system.");
  }

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
  mainSizer->Add(m_logView, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
                 ui::kPagePad);

  SetSizer(mainSizer);

  // Log lines and the stop notification arrive from the server thread as
  // queued events; see EmbeddedServer.h.
  Bind(EVT_EMBEDDED_SERVER_LOG, &ControlPanel::OnEmbeddedLog, this);
  Bind(EVT_EMBEDDED_SERVER_STOPPED, &ControlPanel::OnEmbeddedStopped, this);

  // If the login item points at a copy of ShareFS that has moved or gone,
  // put it right before reporting anything; see sfs_autostart_repair.
  char repairErr[512];
  const int repaired = sfs_autostart_repair(repairErr, sizeof(repairErr));
  if (repaired > 0)
    AppendLog("[SHAREFS] Background sharing was pointing at an older copy of "
              "ShareFS; it now points at this one.\n");
  else if (repaired < 0)
    AppendLog(wxString("[ERROR] ") + repairErr + "\n");

  PollBackground();
  UpdateStatus();
  m_timer.Start(1000);
}

// Only the in-process server is ours to shut down. A background one is
// supposed to outlive the window, which is the whole point of it.
ControlPanel::~ControlPanel() { StopInApp(); }

void ControlPanel::RefreshFromConfig() { UpdateStatus(); }

bool ControlPanel::KeepSharingEnabled() const { return m_keepEnabled; }

void ControlPanel::PollBackground() {
  const sfs_autostart_state state = sfs_autostart_query();
  m_autostartSupported = (state != SFS_AUTOSTART_UNSUPPORTED);
  m_keepEnabled = (state == SFS_AUTOSTART_ENABLED);
  m_backgroundRunning = m_autostartSupported ? (sfs_autostart_is_running() != 0)
                                             : false;
}

void ControlPanel::UpdateStatus() {
  const bool background = m_backgroundRunning;
  const bool inApp = (m_embedded && m_embedded->IsRunning());

  if (background)
    m_mode = Mode::Background;
  else if (inApp)
    m_mode = Mode::InApp;
  else
    m_mode = Mode::Stopped;

  wxString line, detail;
  switch (m_mode) {
  case Mode::Background:
    line = "Sharing";
    detail = "Running in the background, so it keeps going when this window is "
             "closed.";
    break;
  case Mode::InApp:
    line = "Sharing";
    detail = "Running in this window. Sharing stops when you close it.";
    break;
  case Mode::Stopped:
    line = "Not sharing";
    detail = "Press Start on the toolbar to share the folders on the Shares "
             "tab.";
    break;
  }

  m_statusLine->SetLabel(line);
  m_statusLine->SetForegroundColour(m_mode == Mode::Stopped
                                        ? ui::MutedText(this)
                                        : ui::StatusRunning());
  m_statusDetail->SetLabel(detail);
  m_statusDetail->Wrap(GetClientSize().GetWidth() - 2 * ui::kPagePad);

  // The tick box shows what the system is actually set to, not what was last
  // clicked, so an enable that silently failed cannot leave it lying.
  if (m_keepSharing->IsEnabled())
    m_keepSharing->SetValue(KeepSharingEnabled());

  Layout();

  if (m_frame) {
    m_frame->SetServerStatus(line, m_mode != Mode::Stopped);
    m_frame->SetTransportState(m_mode != Mode::Stopped);
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
  if (line.StartsWith("[SHAREFS]"))
    return ui::Neutral();
  return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

void ControlPanel::AppendLog(const wxString &text) {
  // Trim oldest lines when the log grows too large
  if (m_logView->GetNumberOfLines() > 5000) {
    long trimPos = m_logView->XYToPosition(0, 1000);
    if (trimPos > 0)
      m_logView->Remove(0, trimPos);
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

bool ControlPanel::StartInApp() {
  const wxString config = m_frame ? m_frame->GetConfigPath() : wxString();
  if (config.empty()) {
    AppendLog("[ERROR] No configuration file loaded.\n");
    return false;
  }

  // Run the server inside this process rather than spawning a second binary.
  // The status is then a fact rather than a guess, log lines arrive directly,
  // and there is no executable to go looking for.
  if (!m_embedded)
    m_embedded.reset(new EmbeddedServer(this));

  wxString error;
  if (!m_embedded->Start(config, error)) {
    AppendLog("[ERROR] " + error + "\n");
    return false;
  }
  AppendLog("[SHAREFS] Sharing started.\n");
  return true;
}

void ControlPanel::StopInApp() {
  if (!m_embedded || !m_embedded->IsRunning())
    return;
  // Blocks until the server thread has unwound and released its sockets, so a
  // restart immediately afterwards can bind them again.
  m_embedded->Stop();
}

void ControlPanel::StartServer() {
  PollBackground();
  UpdateStatus();
  if (m_mode != Mode::Stopped)
    return;

  // Where sharing is meant to survive the window, start it there, so the
  // toolbar's Start does the same thing the tick box promises.
  if (KeepSharingEnabled()) {
    char err[512];
    if (sfs_autostart_start_now(err, sizeof(err)) == 0) {
      AppendLog("[SHAREFS] Sharing started in the background.\n");
      PollBackground();
      UpdateStatus();
      return;
    }
    AppendLog(wxString("[ERROR] ") + err + "\n");
    AppendLog("[SHAREFS] Sharing in this window instead.\n");
  }

  StartInApp();
  UpdateStatus();
}

void ControlPanel::StopServer() {
  PollBackground();
  UpdateStatus();

  if (m_mode == Mode::Background) {
    char err[512];
    AppendLog("[SHAREFS] Stopping the background copy...\n");
    if (sfs_autostart_stop_now(err, sizeof(err)) != 0)
      AppendLog(wxString("[ERROR] ") + err + "\n");
    PollBackground();
    UpdateStatus();
    return;
  }

  if (m_mode == Mode::InApp) {
    AppendLog("[SHAREFS] Stopping...\n");
    StopInApp();
    AppendLog("[SHAREFS] Sharing stopped.\n");
    UpdateStatus();
  }
}

void ControlPanel::RestartServer() {
  const bool wasRunning = (m_mode != Mode::Stopped);
  StopServer();
  if (!wasRunning)
    return;
  // The sockets are released synchronously in both paths, but a background
  // service takes a moment to report itself stopped.
  wxMilliSleep(500);
  StartServer();
}

void ControlPanel::SetKeepSharing(bool enabled) {
  if (!m_keepSharing->IsEnabled())
    return;

  // Both copies would bind the same UDP ports, so the one that is running has
  // to let go before the other starts. Doing that here is what lets the user
  // treat this as a single tick box.
  const bool wasSharing = (m_mode != Mode::Stopped);

  if (enabled)
    StopInApp();
  else if (m_backgroundRunning) {
    char stopErr[512];
    sfs_autostart_stop_now(stopErr, sizeof(stopErr));
  }

  char err[512];
  if (sfs_autostart_set(enabled ? 1 : 0, err, sizeof(err)) != 0) {
    AppendLog(wxString("[ERROR] ") + err + "\n");
    ui::Notify(this, "Background sharing", err);
    PollBackground();
    UpdateStatus();
    return;
  }

  AppendLog(enabled ? "[SHAREFS] Sharing will now keep going in the "
                      "background.\n"
                    : "[SHAREFS] Sharing now stops when this window closes.\n");

  // Enabling starts the background copy itself; disabling leaves nothing
  // running, so put sharing back the way the user had it.
  if (!enabled && wasSharing)
    StartInApp();

  PollBackground();
  UpdateStatus();
}

void ControlPanel::OnKeepSharing(wxCommandEvent &event) {
  SetKeepSharing(event.IsChecked());
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
  UpdateStatus();
}

void ControlPanel::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  // Asking the operating system costs a process, so do that every third tick
  // and repaint from the cache in between. This is what notices a background
  // copy being started or stopped from outside this window.
  if (++m_tick % 3 == 0)
    PollBackground();
  UpdateStatus();
}
