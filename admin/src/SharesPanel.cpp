// RISC OS Access Server - Admin GUI Shares Panel

#include "SharesPanel.h"
#include "MainFrame.h"
#include <wx/dirdlg.h>
#include <wx/statbox.h>

enum {
    ID_ADD_SHARE = wxID_HIGHEST + 100,
    ID_REMOVE_SHARE,
    ID_BROWSE_PATH,
    ID_SHARE_LIST
};

SharesPanel::SharesPanel(wxWindow* parent, MainFrame* frame)
    : wxPanel(parent), m_frame(frame)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Title and buttons
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Network Shares");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(14);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    headerSizer->Add(title, 1, wxALIGN_CENTER_VERTICAL);
    
    wxButton* addBtn = new wxButton(this, ID_ADD_SHARE, "Add");
    wxButton* removeBtn = new wxButton(this, ID_REMOVE_SHARE, "Remove");
    headerSizer->Add(addBtn, 0, wxLEFT, 5);
    headerSizer->Add(removeBtn, 0, wxLEFT, 5);
    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 15);
    
    // Split view: list + details
    wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);
    
    // List
    m_list = new wxListCtrl(this, ID_SHARE_LIST, wxDefaultPosition, wxSize(200, -1),
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->InsertColumn(0, "Share Name", wxLIST_FORMAT_LEFT, 180);
    contentSizer->Add(m_list, 0, wxEXPAND | wxRIGHT, 10);
    
    // Detail panel
    m_detailPanel = new wxPanel(this);
    m_detailPanel->Hide();
    wxBoxSizer* detailSizer = new wxBoxSizer(wxVERTICAL);
    
    wxFlexGridSizer* grid = new wxFlexGridSizer(4, 2, 8, 10);
    grid->AddGrowableCol(1);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
    m_nameCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_nameCtrl->Bind(wxEVT_TEXT, &SharesPanel::OnDetailChanged, this);
    grid->Add(m_nameCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Path:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* pathSizer = new wxBoxSizer(wxHORIZONTAL);
    m_pathCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_pathCtrl->Bind(wxEVT_TEXT, &SharesPanel::OnDetailChanged, this);
    pathSizer->Add(m_pathCtrl, 1, wxEXPAND);
    wxButton* browseBtn = new wxButton(m_detailPanel, ID_BROWSE_PATH, "Browse...");
    pathSizer->Add(browseBtn, 0, wxLEFT, 5);
    grid->Add(pathSizer, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Password:"), 0, wxALIGN_CENTER_VERTICAL);
    m_passwordCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    m_passwordCtrl->Bind(wxEVT_TEXT, &SharesPanel::OnDetailChanged, this);
    grid->Add(m_passwordCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Default Filetype:"), 0, wxALIGN_CENTER_VERTICAL);
    m_defaultTypeCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition, wxSize(60, -1));
    m_defaultTypeCtrl->SetMaxLength(3);
    m_defaultTypeCtrl->Bind(wxEVT_TEXT, &SharesPanel::OnDetailChanged, this);
    grid->Add(m_defaultTypeCtrl, 0);
    
    detailSizer->Add(grid, 0, wxEXPAND | wxBOTTOM, 15);
    
    // Attributes
    wxStaticBoxSizer* attrBox = new wxStaticBoxSizer(wxVERTICAL, m_detailPanel, "Attributes");
    m_attrProtected = new wxCheckBox(m_detailPanel, wxID_ANY, "Protected (require password)");
    m_attrProtected->Bind(wxEVT_CHECKBOX, &SharesPanel::OnAttrChanged, this);
    attrBox->Add(m_attrProtected, 0, wxALL, 5);
    
    m_attrReadonly = new wxCheckBox(m_detailPanel, wxID_ANY, "Read-only");
    m_attrReadonly->Bind(wxEVT_CHECKBOX, &SharesPanel::OnAttrChanged, this);
    attrBox->Add(m_attrReadonly, 0, wxALL, 5);
    
    m_attrHidden = new wxCheckBox(m_detailPanel, wxID_ANY, "Hidden from browser");
    m_attrHidden->Bind(wxEVT_CHECKBOX, &SharesPanel::OnAttrChanged, this);
    attrBox->Add(m_attrHidden, 0, wxALL, 5);
    
    m_attrCdrom = new wxCheckBox(m_detailPanel, wxID_ANY, "CD-ROM share");
    m_attrCdrom->Bind(wxEVT_CHECKBOX, &SharesPanel::OnAttrChanged, this);
    attrBox->Add(m_attrCdrom, 0, wxALL, 5);
    
    detailSizer->Add(attrBox, 0, wxEXPAND);
    m_detailPanel->SetSizer(detailSizer);
    
    contentSizer->Add(m_detailPanel, 1, wxEXPAND);
    mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);
    
    SetSizer(mainSizer);
    
    // Bind events
    Bind(wxEVT_BUTTON, &SharesPanel::OnAddShare, this, ID_ADD_SHARE);
    Bind(wxEVT_BUTTON, &SharesPanel::OnRemoveShare, this, ID_REMOVE_SHARE);
    Bind(wxEVT_BUTTON, &SharesPanel::OnBrowsePath, this, ID_BROWSE_PATH);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &SharesPanel::OnShareSelected, this);
}

void SharesPanel::RefreshFromConfig() {
    RefreshList();
    m_currentIndex = -1;
    m_detailPanel->Hide();
    Layout();
}

void SharesPanel::RefreshList() {
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_frame->GetConfig().Shares().size(); ++i) {
        m_list->InsertItem(i, m_frame->GetConfig().Shares()[i].name);
    }
}

void SharesPanel::ShowDetails(int index) {
    if (index < 0 || index >= (int)m_frame->GetConfig().Shares().size()) {
        m_detailPanel->Hide();
        m_currentIndex = -1;
        Layout();
        return;
    }
    
    m_updating = true;
    m_currentIndex = index;
    ShareConfig& share = m_frame->GetConfig().Shares()[index];
    
    m_nameCtrl->SetValue(share.name);
    m_pathCtrl->SetValue(share.path);
    m_passwordCtrl->SetValue(share.password);
    m_defaultTypeCtrl->SetValue(share.default_type);
    
    m_attrProtected->SetValue(share.attributes & RAS_ATTR_PROTECTED);
    m_attrReadonly->SetValue(share.attributes & RAS_ATTR_READONLY);
    m_attrHidden->SetValue(share.attributes & RAS_ATTR_HIDDEN);
    m_attrCdrom->SetValue(share.attributes & RAS_ATTR_CDROM);
    
    m_detailPanel->Show();
    Layout();
    m_updating = false;
}

void SharesPanel::SaveCurrentDetails() {
    if (m_currentIndex < 0 || m_updating) return;
    
    ShareConfig& share = m_frame->GetConfig().Shares()[m_currentIndex];
    share.name = m_nameCtrl->GetValue().ToStdString();
    share.path = m_pathCtrl->GetValue().ToStdString();
    share.password = m_passwordCtrl->GetValue().ToStdString();
    share.default_type = m_defaultTypeCtrl->GetValue().ToStdString();
    
    share.attributes = 0;
    if (m_attrProtected->GetValue()) share.attributes |= RAS_ATTR_PROTECTED;
    if (m_attrReadonly->GetValue()) share.attributes |= RAS_ATTR_READONLY;
    if (m_attrHidden->GetValue()) share.attributes |= RAS_ATTR_HIDDEN;
    if (m_attrCdrom->GetValue()) share.attributes |= RAS_ATTR_CDROM;
    
    // Update list display
    m_list->SetItemText(m_currentIndex, share.name);
}

void SharesPanel::OnAddShare(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    ShareConfig share;
    share.name = "New Share";
    share.path = "";
    m_frame->GetConfig().Shares().push_back(share);
    
    RefreshList();
    int newIndex = m_frame->GetConfig().Shares().size() - 1;
    m_list->SetItemState(newIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    ShowDetails(newIndex);
    m_frame->SetModified(true);
}

void SharesPanel::OnRemoveShare(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    if (m_currentIndex >= 0 && m_currentIndex < (int)m_frame->GetConfig().Shares().size()) {
        m_frame->GetConfig().Shares().erase(m_frame->GetConfig().Shares().begin() + m_currentIndex);
        RefreshList();
        m_currentIndex = -1;
        m_detailPanel->Hide();
        Layout();
        m_frame->SetModified(true);
    }
}

void SharesPanel::OnShareSelected(wxListEvent& event) {
    ShowDetails(event.GetIndex());
}

void SharesPanel::OnBrowsePath(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    wxDirDialog dlg(this, "Select Share Directory", m_pathCtrl->GetValue(),
                    wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
    if (dlg.ShowModal() == wxID_OK) {
        m_pathCtrl->SetValue(dlg.GetPath());
    }
}

void SharesPanel::OnDetailChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating || m_currentIndex < 0) return;
    
    SaveCurrentDetails();
    m_frame->SetModified(true);
}

void SharesPanel::OnAttrChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating || m_currentIndex < 0) return;
    
    SaveCurrentDetails();
    m_frame->SetModified(true);
}
