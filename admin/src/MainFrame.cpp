/*
  ShareFS - Main Frame Implementation

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

#include "MainFrame.h"
#include "ServerPanel.h"
#include "SharesPanel.h"
#include "PrintersPanel.h"
#include "MimePanel.h"
#include "ControlPanel.h"
#include "AboutDialog.h"
#include "FirstRunDialog.h"
#include "Icons.h"
#include "UiHelpers.h"
#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/utils.h>

extern "C" {
#include "autostart.h"
#include "paths.h"
}

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_SAVE, MainFrame::OnSave)
    EVT_MENU(ID_APPLY, MainFrame::OnApply)
    EVT_MENU(ID_REVERT, MainFrame::OnRevert)
    EVT_MENU(wxID_EXIT, MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_MENU(ID_REVEAL_CONFIG, MainFrame::OnRevealConfig)
    EVT_TOOL(ID_START, MainFrame::OnStart)
    EVT_TOOL(ID_STOP, MainFrame::OnStop)
    EVT_BUTTON(ID_SAVE, MainFrame::OnSave)
    EVT_BUTTON(ID_APPLY, MainFrame::OnApply)
    EVT_BUTTON(ID_REVERT, MainFrame::OnRevert)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(960, 720))
{
    SetMinSize(wxSize(820, 600));
    BuildMenuBar();
    BuildToolBar();

    wxStatusBar* bar = CreateStatusBar(3);
    const int widths[3] = { -5, -3, -3 };
    SetStatusWidths(3, widths);
    SetStatusText("No configuration loaded", 0);

    // The server indicator lives here rather than in the toolbar: wxToolBar on
    // macOS wraps NSToolbar, which pushes embedded controls to the window edge
    // and clips them. In the status bar it is always visible and always legible.
    m_statusDot = new ui::StatusDot(bar, "Stopped");
    m_statusDot->SetAlignRight(true);
    m_statusDot->Set("Stopped", ui::StatusStopped());
    bar->Bind(wxEVT_SIZE, &MainFrame::OnStatusBarSize, this);
    PositionStatusDot();

    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

    m_notebook = new wxNotebook(this, wxID_ANY);

    m_serverPanel = new ServerPanel(m_notebook, this);
    m_sharesPanel = new SharesPanel(m_notebook, this);
    m_printersPanel = new PrintersPanel(m_notebook, this);
    m_mimePanel = new MimePanel(m_notebook, this);
    m_controlPanel = new ControlPanel(m_notebook, this);

    m_notebook->AddPage(m_serverPanel, "Server");
    m_notebook->AddPage(m_sharesPanel, "Shares");
    m_notebook->AddPage(m_printersPanel, "Printers");
    m_notebook->AddPage(m_mimePanel, "MIME Map");
    m_notebook->AddPage(m_controlPanel, "Sharing");

    // Without this the tab strip sits flush against the toolbar.
    mainSizer->Add(m_notebook, 1, wxEXPAND | wxTOP, ui::kTightGap);

    SetSizer(mainSizer);
    UpdateActionState();
    Centre();
}

void MainFrame::BuildToolBar() {
    m_toolbar = wxFrame::CreateToolBar(wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT);

    const int kIcon = 22;
    const wxColour tint = ui::IconTint();
    m_toolbar->SetToolBitmapSize(wxSize(kIcon, kIcon));

    // Configuration actions.
    m_toolbar->AddTool(ID_SAVE, "Save", ui::Icon(icons::kFloppy, kIcon, tint),
                       "Save configuration (Ctrl+S)");
    m_toolbar->AddTool(ID_REVERT, "Revert", ui::Icon(icons::kReset, kIcon, tint),
                       "Discard unsaved changes");

    m_toolbar->AddSeparator();

    // Server transport, so the server can be driven without leaving the tab
    // you are working in.
    m_toolbar->AddTool(ID_START, "Start", ui::Icon(icons::kStart, kIcon, tint),
                       "Start the server");
    m_toolbar->AddTool(ID_STOP, "Stop", ui::Icon(icons::kStop, kIcon, tint),
                       "Stop the server");
    m_toolbar->AddTool(ID_APPLY, "Apply && Restart",
                       ui::Icon(icons::kPower, kIcon, tint),
                       "Save the configuration and restart the server");

    m_toolbar->Realize();
}

void MainFrame::SetTransportState(bool running) {
    if (!m_toolbar)
        return;
    m_toolbar->EnableTool(ID_START, !running);
    m_toolbar->EnableTool(ID_STOP, running);
}

void MainFrame::OnStart(wxCommandEvent& event) {
    wxUnusedVar(event);
    m_controlPanel->StartServer();
}

void MainFrame::OnStop(wxCommandEvent& event) {
    wxUnusedVar(event);
    m_controlPanel->StopServer();
}

void MainFrame::SetServerStatus(const wxString& label, bool running) {
    if (!m_statusDot)
        return;
    m_statusDot->Set(label, running ? ui::StatusRunning() : ui::StatusStopped());
    PositionStatusDot();
}

void MainFrame::OnStatusBarSize(wxSizeEvent& event) {
    event.Skip();
    PositionStatusDot();
}

// Park the indicator inside the status bar's third field. wxStatusBar has no
// notion of child widgets, so the position is recomputed on every resize.
void MainFrame::PositionStatusDot() {
    wxStatusBar* bar = GetStatusBar();
    if (!bar || !m_statusDot)
        return;

    wxRect field;
    if (!bar->GetFieldRect(2, field))
        return;

    // Span the whole field and let the control draw itself right-aligned, so
    // the indicator sits against the window edge rather than floating at the
    // start of the field. The trailing pad clears the macOS resize grip.
    const wxSize best = m_statusDot->GetBestSize();
    const int pad = ui::kPagePad;
    m_statusDot->SetSize(field.x, field.y + (field.height - best.y) / 2,
                         wxMax(best.x, field.width - pad), best.y);
}

void MainFrame::UpdateSummary() {
    // FromUTF8, because this file is UTF-8 and the separator is not ASCII.
    // A narrow literal is decoded with the current locale, which on Windows is
    // an ANSI codepage, so the two bytes of the middle dot arrived in the
    // status bar as two characters.
    SetStatusText(wxString::Format(wxString::FromUTF8(
                                       "%zu shares  ·  %zu printers  ·  %zu MIME rules"),
                                   m_config.Shares().size(),
                                   m_config.Printers().size(),
                                   m_config.MimeMap().size()),
                  1);
}

void MainFrame::UpdateActionState() {
    if (!m_toolbar)
        return;
    m_toolbar->EnableTool(ID_SAVE, m_modified);
    m_toolbar->EnableTool(ID_APPLY, m_modified);
    m_toolbar->EnableTool(ID_REVERT, m_modified);
}

void MainFrame::BuildMenuBar() {
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_SAVE, "&Save\tCtrl+S", "Save configuration");
    fileMenu->Append(ID_APPLY, "&Apply && Restart\tCtrl+Shift+R", "Save configuration and restart server");
    fileMenu->Append(ID_REVERT, "&Revert Changes", "Discard unsaved changes");
    fileMenu->AppendSeparator();
    // Where the settings live is ShareFS's business, not the user's, but it
    // should never be a mystery either.
    fileMenu->Append(ID_REVEAL_CONFIG, "Reveal Configuration &File",
                     "Show sharefs.conf in the file manager");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tAlt+F4", "Exit application");
    
    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, "&About ShareFS", "About this application");
    
    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(helpMenu, "&Help");
    SetMenuBar(menuBar);
}

void MainFrame::UpdateTitle() {
    wxString title = "ShareFS";
    if (!m_configPath.empty()) {
        wxFileName fn(m_configPath);
        title += " - " + fn.GetFullName();
    }
    if (m_modified) {
        title += " *";
    }
    SetTitle(title);
}

void MainFrame::SetModified(bool modified) {
    if (m_modified != modified) {
        m_modified = modified;
        UpdateActionState();
        UpdateTitle();
    }
    UpdateSummary();
}

void MainFrame::LoadConfig(const std::string& path) {
    std::string error;
    if (m_config.Load(path, error)) {
        m_configPath = path;
        m_modified = false;
        UpdateActionState();
        UpdateTitle();

        // Refresh all panels
        m_serverPanel->RefreshFromConfig();
        m_sharesPanel->RefreshFromConfig();
        m_printersPanel->RefreshFromConfig();
        m_mimePanel->RefreshFromConfig();
        m_controlPanel->RefreshFromConfig();
        
        SetStatusText(wxString(path), 0);
        UpdateSummary();
    } else {
        ui::Notify(this, "Could not load configuration", error);
    }
}

void MainFrame::SaveConfig() {
    if (m_configPath.empty()) {
        // Not "./sharefs.conf": that is wherever the app was launched from,
        // which is not a place the server goes looking. See src/paths.h.
        char fallback[SFS_PATH_MAX];
        if (sfs_paths_default_config(fallback, sizeof(fallback)) == 0) {
            wxFileName fn(wxString::FromUTF8(fallback));
            sfs_paths_mkdir_p(fn.GetPath().utf8_str());
            m_configPath = fallback;
        } else {
            m_configPath = "sharefs.conf";
        }
    }

    std::string error;
    if (m_config.Save(m_configPath, error)) {
        m_modified = false;
        UpdateActionState();
        UpdateTitle();
        SetStatusText(wxString(m_configPath), 0);
        UpdateSummary();
    } else {
        ui::Notify(this, "Could not save configuration", error);
    }
}

void MainFrame::RevertConfig() {
    if (!m_configPath.empty()) {
        LoadConfig(m_configPath);
    }
}

void MainFrame::OnSave(wxCommandEvent& event) {
    wxUnusedVar(event);
    SaveConfig();
}

void MainFrame::OnApply(wxCommandEvent& event) {
    wxUnusedVar(event);
    SaveConfig();
    m_controlPanel->RestartServer();
}

void MainFrame::OnRevert(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    if (ui::Ask(this, "Revert Changes", "Discard all unsaved changes?")) {
        RevertConfig();
    }
}

void MainFrame::OnExit(wxCommandEvent& event) {
    wxUnusedVar(event);
    Close();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (m_modified && event.CanVeto()) {
        if (!ui::Ask(this, "Unsaved Changes",
                     "You have unsaved changes. Exit anyway?")) {
            event.Veto();
            return;
        }
    }
    
    // Only the copy running inside this process goes away with the window. A
    // background one was set up precisely so that it would not, and stopping
    // it here made the tick box a lie. ~ControlPanel stops the in-process one.
    Destroy();
}

#ifndef SHAREFS_VERSION
#define SHAREFS_VERSION "0.0.0"
#endif
#ifndef SHAREFS_HOMEPAGE
#define SHAREFS_HOMEPAGE "https://github.com/andrewtimmins/riscos-access-server"
#endif

// Nothing configured yet. One folder is the whole of setting up a file server,
// so ask for that and get out of the way.
void MainFrame::RunFirstRun() {
    char configPath[SFS_PATH_MAX];
    if (sfs_paths_default_config(configPath, sizeof(configPath)) != 0) {
        ui::Notify(this, "ShareFS",
                   "Could not work out where to keep the settings.");
        return;
    }

    char shareDefault[SFS_PATH_MAX];
    if (sfs_paths_default_share(shareDefault, sizeof(shareDefault)) != 0)
        shareDefault[0] = '\0';

    FirstRunDialog dlg(this, wxString::FromUTF8(shareDefault),
                       wxString::FromUTF8(configPath));
    const bool accepted = (dlg.ShowModal() == wxID_OK);

    // Cancelling still leaves a working configuration behind: the window is
    // useless without one, and an empty first tab is not an answer to
    // anything. It simply does not start sharing.
    wxString folder = accepted ? dlg.Folder() : wxString::FromUTF8(shareDefault);
    wxString name = accepted ? dlg.ShareName() : wxString("Public");
    if (folder.empty())
        folder = wxString::FromUTF8(shareDefault);

    char err[512];
    if (sfs_paths_write_default_config(configPath, name.utf8_str(),
                                       folder.utf8_str(), err,
                                       sizeof(err)) != 0) {
        ui::Notify(this, "Could not create the configuration",
                   wxString::FromUTF8(err));
        return;
    }

    LoadConfig(configPath);
    if (!accepted)
        return;

    if (dlg.KeepSharing())
        m_controlPanel->SetKeepSharing(true);
    m_controlPanel->StartServer();
}

void MainFrame::OnRevealConfig(wxCommandEvent& event) {
    wxUnusedVar(event);

    const wxString path = wxString::FromUTF8(m_configPath.c_str());
    if (path.empty() || !wxFileExists(path)) {
        ui::Notify(this, "Configuration file",
                   "There is no configuration file yet. Save your settings "
                   "first and it will be created.");
        return;
    }

    // Selecting the file itself where the platform can, so it is obvious which
    // of several files is the one in use.
#ifdef __WXOSX__
    wxExecute("/usr/bin/open -R \"" + path + "\"");
#elif defined(__WXMSW__)
    wxExecute("explorer.exe /select,\"" + path + "\"");
#else
    wxFileName fn(path);
    wxExecute("xdg-open \"" + fn.GetPath() + "\"");
#endif
}

void MainFrame::OnAbout(wxCommandEvent& event) {
    wxUnusedVar(event);

    AboutDialog dlg(this);
    dlg.ShowModal();
}
