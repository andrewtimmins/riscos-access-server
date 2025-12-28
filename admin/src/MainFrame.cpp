// RISC OS Access Server - Admin GUI Main Frame Implementation

#include "MainFrame.h"
#include "ServerPanel.h"
#include "SharesPanel.h"
#include "PrintersPanel.h"
#include "MimePanel.h"
#include "ControlPanel.h"
#include <wx/filename.h>
#include <wx/msgdlg.h>

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_APPLY, MainFrame::OnApply)
    EVT_MENU(ID_REVERT, MainFrame::OnRevert)
    EVT_MENU(wxID_EXIT, MainFrame::OnExit)
    EVT_MENU(wxID_ABOUT, MainFrame::OnAbout)
    EVT_BUTTON(ID_APPLY, MainFrame::OnApply)
    EVT_BUTTON(ID_REVERT, MainFrame::OnRevert)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title)
    : wxFrame(nullptr, wxID_ANY, title, wxDefaultPosition, wxSize(900, 650))
{
    CreateMenuBar();
    CreateToolBar();
    
    // Main layout: toolbar at bottom, notebook in center
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Create notebook for tabbed interface
    m_notebook = new wxNotebook(this, wxID_ANY);
    
    // Create panels
    m_serverPanel = new ServerPanel(m_notebook, this);
    m_sharesPanel = new SharesPanel(m_notebook, this);
    m_printersPanel = new PrintersPanel(m_notebook, this);
    m_mimePanel = new MimePanel(m_notebook, this);
    m_controlPanel = new ControlPanel(m_notebook, this);
    
    m_notebook->AddPage(m_serverPanel, "Server");
    m_notebook->AddPage(m_sharesPanel, "Shares");
    m_notebook->AddPage(m_printersPanel, "Printers");
    m_notebook->AddPage(m_mimePanel, "MIME Map");
    m_notebook->AddPage(m_controlPanel, "Control");
    
    mainSizer->Add(m_notebook, 1, wxEXPAND);
    
    // Bottom button bar
    wxPanel* buttonBar = new wxPanel(this);
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    buttonSizer->AddStretchSpacer();
    
    m_revertBtn = new wxButton(buttonBar, ID_REVERT, "Revert Changes");
    m_revertBtn->Disable();
    buttonSizer->Add(m_revertBtn, 0, wxALL, 8);
    
    m_applyBtn = new wxButton(buttonBar, ID_APPLY, "Apply && Restart");
    m_applyBtn->Disable();
    buttonSizer->Add(m_applyBtn, 0, wxALL, 8);
    
    buttonBar->SetSizer(buttonSizer);
    mainSizer->Add(buttonBar, 0, wxEXPAND);
    
    SetSizer(mainSizer);
    Centre();
}

void MainFrame::CreateMenuBar() {
    wxMenu* fileMenu = new wxMenu;
    fileMenu->Append(ID_APPLY, "&Apply && Restart\tCtrl+S", "Save configuration and restart server");
    fileMenu->Append(ID_REVERT, "&Revert Changes", "Discard unsaved changes");
    fileMenu->AppendSeparator();
    fileMenu->Append(wxID_EXIT, "E&xit\tAlt+F4", "Exit application");
    
    wxMenu* helpMenu = new wxMenu;
    helpMenu->Append(wxID_ABOUT, "&About", "About this application");
    
    wxMenuBar* menuBar = new wxMenuBar;
    menuBar->Append(fileMenu, "&File");
    menuBar->Append(helpMenu, "&Help");
    SetMenuBar(menuBar);
}

void MainFrame::CreateToolBar() {
    // Status bar
    CreateStatusBar(1);
    SetStatusText("Ready");
}

void MainFrame::UpdateTitle() {
    wxString title = "Access/ShareFS Admin";
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
        m_applyBtn->Enable(modified);
        m_revertBtn->Enable(modified);
        UpdateTitle();
    }
}

void MainFrame::LoadConfig(const std::string& path) {
    std::string error;
    if (m_config.Load(path, error)) {
        m_configPath = path;
        m_modified = false;
        m_applyBtn->Disable();
        m_revertBtn->Disable();
        UpdateTitle();
        
        // Refresh all panels
        m_serverPanel->RefreshFromConfig();
        m_sharesPanel->RefreshFromConfig();
        m_printersPanel->RefreshFromConfig();
        m_mimePanel->RefreshFromConfig();
        m_controlPanel->RefreshFromConfig();
        
        SetStatusText("Loaded: " + wxString(path));
    } else {
        wxMessageBox("Failed to load config: " + error, "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::SaveConfig() {
    if (m_configPath.empty()) {
        m_configPath = "access.conf";
    }
    
    std::string error;
    if (m_config.Save(m_configPath, error)) {
        m_modified = false;
        m_applyBtn->Disable();
        m_revertBtn->Disable();
        UpdateTitle();
        SetStatusText("Saved: " + wxString(m_configPath));
    } else {
        wxMessageBox("Failed to save: " + error, "Error", wxOK | wxICON_ERROR);
    }
}

void MainFrame::RevertConfig() {
    if (!m_configPath.empty()) {
        LoadConfig(m_configPath);
        SetStatusText("Reverted to saved configuration");
    }
}

void MainFrame::OnApply(wxCommandEvent& event) {
    wxUnusedVar(event);
    SaveConfig();
    
    // Restart the server with new config
    m_controlPanel->RestartServer();
    SetStatusText("Configuration saved and server restarted");
}

void MainFrame::OnRevert(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    int result = wxMessageBox("Discard all unsaved changes?",
                              "Revert Changes", wxYES_NO | wxICON_QUESTION);
    if (result == wxYES) {
        RevertConfig();
    }
}

void MainFrame::OnExit(wxCommandEvent& event) {
    wxUnusedVar(event);
    Close();
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (m_modified && event.CanVeto()) {
        int result = wxMessageBox("You have unsaved changes. Exit anyway?",
                                  "Unsaved Changes", wxYES_NO | wxICON_QUESTION);
        if (result == wxNO) {
            event.Veto();
            return;
        }
    }
    
    // Stop server if running before exit
    m_controlPanel->StopServer();
    
    Destroy();
}

void MainFrame::OnAbout(wxCommandEvent& event) {
    wxUnusedVar(event);
    wxMessageBox(wxT("Access/ShareFS Server Admin\n\n")
                 wxT("Administration and control utility for\n")
                 wxT("the Access/ShareFS server.\n\n")
                 wxT("Copyright © Andrew Timmins, 2025.\n\n")
                 wxT("Licensed under the GNU General Public License v3.0\n")
                 wxT("https://www.gnu.org/licenses/gpl-3.0.html"),
                 wxT("About"), wxOK | wxICON_INFORMATION);
}
