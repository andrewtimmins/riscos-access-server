// RISC OS Access Server - Admin GUI MIME Panel

#include "MimePanel.h"
#include "MainFrame.h"

enum {
    ID_ADD_MIME = wxID_HIGHEST + 300,
    ID_REMOVE_MIME,
    ID_MIME_LIST
};

MimePanel::MimePanel(wxWindow* parent, MainFrame* frame)
    : wxPanel(parent), m_frame(frame)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Title and buttons
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "MIME Type Mappings");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(14);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    headerSizer->Add(title, 1, wxALIGN_CENTER_VERTICAL);
    
    wxButton* addBtn = new wxButton(this, ID_ADD_MIME, "Add");
    wxButton* removeBtn = new wxButton(this, ID_REMOVE_MIME, "Remove");
    headerSizer->Add(addBtn, 0, wxLEFT, 5);
    headerSizer->Add(removeBtn, 0, wxLEFT, 5);
    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 15);
    
    // Description
    wxStaticText* desc = new wxStaticText(this, wxID_ANY, 
        "Map file extensions to RISC OS filetypes (hex values like FFF for Text)");
    desc->SetForegroundColour(wxColour(100, 100, 100));
    mainSizer->Add(desc, 0, wxLEFT | wxRIGHT | wxBOTTOM, 15);
    
    // Split view
    wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_list = new wxListCtrl(this, ID_MIME_LIST, wxDefaultPosition, wxSize(250, -1),
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->InsertColumn(0, "Extension", wxLIST_FORMAT_LEFT, 100);
    m_list->InsertColumn(1, "Filetype", wxLIST_FORMAT_LEFT, 100);
    contentSizer->Add(m_list, 0, wxEXPAND | wxRIGHT, 10);
    
    // Detail panel
    m_detailPanel = new wxPanel(this);
    m_detailPanel->Hide();
    wxBoxSizer* detailSizer = new wxBoxSizer(wxVERTICAL);
    
    wxFlexGridSizer* grid = new wxFlexGridSizer(2, 2, 8, 10);
    grid->AddGrowableCol(1);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Extension:"), 0, wxALIGN_CENTER_VERTICAL);
    m_extCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition, wxSize(100, -1));
    m_extCtrl->Bind(wxEVT_TEXT, &MimePanel::OnDetailChanged, this);
    grid->Add(m_extCtrl, 0);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Filetype (hex):"), 0, wxALIGN_CENTER_VERTICAL);
    m_typeCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition, wxSize(80, -1));
    m_typeCtrl->SetMaxLength(3);
    m_typeCtrl->Bind(wxEVT_TEXT, &MimePanel::OnDetailChanged, this);
    grid->Add(m_typeCtrl, 0);
    
    detailSizer->Add(grid, 0, wxEXPAND);
    
    // Common filetypes hint
    wxStaticText* hint = new wxStaticText(m_detailPanel, wxID_ANY, 
        "Common types:\n"
        "  FFF = Text\n"
        "  FFD = Data\n"
        "  FAF = HTML\n"
        "  AFF = DrawFile\n"
        "  FF9 = Sprite\n"
        "  C85 = JPEG\n"
        "  B60 = PNG");
    hint->SetForegroundColour(wxColour(100, 100, 100));
    detailSizer->Add(hint, 0, wxTOP, 15);
    
    m_detailPanel->SetSizer(detailSizer);
    contentSizer->Add(m_detailPanel, 1, wxEXPAND);
    mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);
    
    SetSizer(mainSizer);
    
    Bind(wxEVT_BUTTON, &MimePanel::OnAddEntry, this, ID_ADD_MIME);
    Bind(wxEVT_BUTTON, &MimePanel::OnRemoveEntry, this, ID_REMOVE_MIME);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &MimePanel::OnEntrySelected, this);
}

void MimePanel::RefreshFromConfig() {
    RefreshList();
    m_currentIndex = -1;
    m_detailPanel->Hide();
    Layout();
}

void MimePanel::RefreshList() {
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_frame->GetConfig().MimeMap().size(); ++i) {
        MimeEntry& entry = m_frame->GetConfig().MimeMap()[i];
        long idx = m_list->InsertItem(i, entry.ext);
        m_list->SetItem(idx, 1, entry.filetype);
    }
}

void MimePanel::ShowDetails(int index) {
    if (index < 0 || index >= (int)m_frame->GetConfig().MimeMap().size()) {
        m_detailPanel->Hide();
        m_currentIndex = -1;
        Layout();
        return;
    }
    
    m_updating = true;
    m_currentIndex = index;
    MimeEntry& entry = m_frame->GetConfig().MimeMap()[index];
    
    m_extCtrl->SetValue(entry.ext);
    m_typeCtrl->SetValue(entry.filetype);
    
    m_detailPanel->Show();
    Layout();
    m_updating = false;
}

void MimePanel::SaveCurrentDetails() {
    if (m_currentIndex < 0 || m_updating) return;
    
    MimeEntry& entry = m_frame->GetConfig().MimeMap()[m_currentIndex];
    entry.ext = m_extCtrl->GetValue().ToStdString();
    entry.filetype = m_typeCtrl->GetValue().ToStdString();
    
    m_list->SetItem(m_currentIndex, 0, entry.ext);
    m_list->SetItem(m_currentIndex, 1, entry.filetype);
}

void MimePanel::OnAddEntry(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    MimeEntry entry;
    entry.ext = "txt";
    entry.filetype = "FFF";
    m_frame->GetConfig().MimeMap().push_back(entry);
    
    RefreshList();
    int newIndex = m_frame->GetConfig().MimeMap().size() - 1;
    m_list->SetItemState(newIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    ShowDetails(newIndex);
    m_frame->SetModified(true);
}

void MimePanel::OnRemoveEntry(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    if (m_currentIndex >= 0 && m_currentIndex < (int)m_frame->GetConfig().MimeMap().size()) {
        m_frame->GetConfig().MimeMap().erase(m_frame->GetConfig().MimeMap().begin() + m_currentIndex);
        RefreshList();
        m_currentIndex = -1;
        m_detailPanel->Hide();
        Layout();
        m_frame->SetModified(true);
    }
}

void MimePanel::OnEntrySelected(wxListEvent& event) {
    ShowDetails(event.GetIndex());
}

void MimePanel::OnDetailChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating || m_currentIndex < 0) return;
    SaveCurrentDetails();
    m_frame->SetModified(true);
}
