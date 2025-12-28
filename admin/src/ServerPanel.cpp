// RISC OS Access Server - Admin GUI Server Panel

#include "ServerPanel.h"
#include "MainFrame.h"
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/statbox.h>

ServerPanel::ServerPanel(wxWindow* parent, MainFrame* frame)
    : wxPanel(parent), m_frame(frame)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Title
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Server Settings");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(14);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    mainSizer->Add(title, 0, wxALL, 15);
    
    // Settings group
    wxStaticBoxSizer* settingsBox = new wxStaticBoxSizer(wxVERTICAL, this, "Configuration");
    wxFlexGridSizer* grid = new wxFlexGridSizer(3, 2, 10, 15);
    grid->AddGrowableCol(1);
    
    // Log level
    grid->Add(new wxStaticText(this, wxID_ANY, "Log Level:"), 0, wxALIGN_CENTER_VERTICAL);
    m_logLevel = new wxChoice(this, wxID_ANY);
    m_logLevel->Append("error");
    m_logLevel->Append("warn");
    m_logLevel->Append("info");
    m_logLevel->Append("debug");
    m_logLevel->Append("protocol");
    m_logLevel->SetSelection(2);  // Default: info
    m_logLevel->Bind(wxEVT_CHOICE, &ServerPanel::OnLogLevelChanged, this);
    grid->Add(m_logLevel, 1, wxEXPAND);
    
    // Broadcast interval
    grid->Add(new wxStaticText(this, wxID_ANY, "Broadcast Interval:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* broadcastSizer = new wxBoxSizer(wxHORIZONTAL);
    m_broadcast = new wxSpinCtrl(this, wxID_ANY, "60", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, 3600, 60);
    m_broadcast->Bind(wxEVT_SPINCTRL, &ServerPanel::OnBroadcastChanged, this);
    broadcastSizer->Add(m_broadcast, 0);
    broadcastSizer->Add(new wxStaticText(this, wxID_ANY, " seconds (0 = disabled)"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    grid->Add(broadcastSizer, 1);
    
    // Access+ authentication
    grid->Add(new wxStaticText(this, wxID_ANY, "Access+ Authentication:"), 0, wxALIGN_CENTER_VERTICAL);
    m_accessPlus = new wxCheckBox(this, wxID_ANY, "Enable password protection for shares");
    m_accessPlus->Bind(wxEVT_CHECKBOX, &ServerPanel::OnAccessPlusChanged, this);
    grid->Add(m_accessPlus, 1);
    
    settingsBox->Add(grid, 1, wxEXPAND | wxALL, 10);
    mainSizer->Add(settingsBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);
    
    // Info text
    wxStaticText* info = new wxStaticText(this, wxID_ANY, 
        "Note: Changes take effect when the server is restarted.");
    info->SetForegroundColour(wxColour(128, 128, 128));
    mainSizer->Add(info, 0, wxALL, 15);
    
    SetSizer(mainSizer);
}

void ServerPanel::RefreshFromConfig() {
    m_updating = true;
    
    ServerConfig& cfg = m_frame->GetConfig().Server();
    
    // Log level
    int idx = m_logLevel->FindString(cfg.log_level);
    if (idx != wxNOT_FOUND) {
        m_logLevel->SetSelection(idx);
    }
    
    m_broadcast->SetValue(cfg.broadcast_interval);
    m_accessPlus->SetValue(cfg.access_plus);
    
    m_updating = false;
}

void ServerPanel::OnLogLevelChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating) return;
    
    m_frame->GetConfig().Server().log_level = m_logLevel->GetStringSelection().ToStdString();
    m_frame->SetModified(true);
}

void ServerPanel::OnBroadcastChanged(wxSpinEvent& event) {
    wxUnusedVar(event);
    if (m_updating) return;
    
    m_frame->GetConfig().Server().broadcast_interval = m_broadcast->GetValue();
    m_frame->SetModified(true);
}

void ServerPanel::OnAccessPlusChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating) return;
    
    m_frame->GetConfig().Server().access_plus = m_accessPlus->GetValue();
    m_frame->SetModified(true);
}
