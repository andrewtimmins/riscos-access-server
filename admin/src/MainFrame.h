/*
  ShareFS Server - Admin GUI Main Frame Header

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

#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/toolbar.h>
#include "ConfigIO.h"
#include "UiHelpers.h"

class ServerPanel;
class SharesPanel;
class PrintersPanel;
class MimePanel;
class ControlPanel;

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title);
    
    void LoadConfig(const std::string& path);
    void SaveConfig();
    void RevertConfig();
    
    void SetModified(bool modified);
    bool IsModified() const { return m_modified; }
    SfsConfig& GetConfig() { return m_config; }
    const std::string& GetConfigPath() const { return m_configPath; }
    
    // Access to control panel for restart
    ControlPanel* GetControlPanel() { return m_controlPanel; }

    // Reflect server state in the toolbar indicator (called by ControlPanel).
    void SetServerStatus(const wxString& label, bool running);

    // Enable/disable the toolbar Start and Stop tools to match server state.
    void SetTransportState(bool running);

    // Refresh the right-hand status bar summary (share/printer counts).
    void UpdateSummary();

private:
    void OnSave(wxCommandEvent& event);
    void OnStart(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);
    void OnRevert(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnStatusBarSize(wxSizeEvent& event);
    void PositionStatusDot();
    void OnAbout(wxCommandEvent& event);
    
    void UpdateTitle();
    void BuildMenuBar();
    void BuildToolBar();
    void UpdateActionState();

    wxNotebook* m_notebook;
    ServerPanel* m_serverPanel;
    SharesPanel* m_sharesPanel;
    PrintersPanel* m_printersPanel;
    MimePanel* m_mimePanel;
    ControlPanel* m_controlPanel;

    wxToolBar* m_toolbar = nullptr;
    ui::StatusDot* m_statusDot = nullptr;

    SfsConfig m_config;
    std::string m_configPath;
    bool m_modified = false;
    
    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_SAVE = wxID_HIGHEST + 1,
    ID_APPLY,
    ID_REVERT,
    ID_START,
    ID_STOP
};

#endif // MAINFRAME_H
