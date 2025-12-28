// RISC OS Access Server - Admin GUI Printers Panel

#include "PrintersPanel.h"
#include "MainFrame.h"

enum {
    ID_ADD_PRINTER = wxID_HIGHEST + 200,
    ID_REMOVE_PRINTER,
    ID_PRINTER_LIST
};

PrintersPanel::PrintersPanel(wxWindow* parent, MainFrame* frame)
    : wxPanel(parent), m_frame(frame)
{
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Title and buttons
    wxBoxSizer* headerSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Printers");
    wxFont titleFont = title->GetFont();
    titleFont.SetPointSize(14);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    title->SetFont(titleFont);
    headerSizer->Add(title, 1, wxALIGN_CENTER_VERTICAL);
    
    wxButton* addBtn = new wxButton(this, ID_ADD_PRINTER, "Add");
    wxButton* removeBtn = new wxButton(this, ID_REMOVE_PRINTER, "Remove");
    headerSizer->Add(addBtn, 0, wxLEFT, 5);
    headerSizer->Add(removeBtn, 0, wxLEFT, 5);
    mainSizer->Add(headerSizer, 0, wxEXPAND | wxALL, 15);
    
    // Split view
    wxBoxSizer* contentSizer = new wxBoxSizer(wxHORIZONTAL);
    
    m_list = new wxListCtrl(this, ID_PRINTER_LIST, wxDefaultPosition, wxSize(200, -1),
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->InsertColumn(0, "Printer Name", wxLIST_FORMAT_LEFT, 180);
    contentSizer->Add(m_list, 0, wxEXPAND | wxRIGHT, 10);
    
    // Detail panel
    m_detailPanel = new wxPanel(this);
    m_detailPanel->Hide();
    wxBoxSizer* detailSizer = new wxBoxSizer(wxVERTICAL);
    
    wxFlexGridSizer* grid = new wxFlexGridSizer(6, 2, 8, 10);
    grid->AddGrowableCol(1);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Name:"), 0, wxALIGN_CENTER_VERTICAL);
    m_nameCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_nameCtrl->Bind(wxEVT_TEXT, &PrintersPanel::OnDetailChanged, this);
    grid->Add(m_nameCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Spool Path:"), 0, wxALIGN_CENTER_VERTICAL);
    m_pathCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_pathCtrl->Bind(wxEVT_TEXT, &PrintersPanel::OnDetailChanged, this);
    grid->Add(m_pathCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Definition:"), 0, wxALIGN_CENTER_VERTICAL);
    m_definitionCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_definitionCtrl->SetHint("/path/to/printer.fc6");
    m_definitionCtrl->Bind(wxEVT_TEXT, &PrintersPanel::OnDetailChanged, this);
    grid->Add(m_definitionCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Description:"), 0, wxALIGN_CENTER_VERTICAL);
    m_descriptionCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_descriptionCtrl->Bind(wxEVT_TEXT, &PrintersPanel::OnDetailChanged, this);
    grid->Add(m_descriptionCtrl, 1, wxEXPAND);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Poll Interval:"), 0, wxALIGN_CENTER_VERTICAL);
    wxBoxSizer* pollSizer = new wxBoxSizer(wxHORIZONTAL);
    m_pollCtrl = new wxSpinCtrl(m_detailPanel, wxID_ANY, "5", wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 1, 60, 5);
    m_pollCtrl->Bind(wxEVT_SPINCTRL, &PrintersPanel::OnPollChanged, this);
    pollSizer->Add(m_pollCtrl, 0);
    pollSizer->Add(new wxStaticText(m_detailPanel, wxID_ANY, " seconds"), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    grid->Add(pollSizer, 0);
    
    grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Print Command:"), 0, wxALIGN_CENTER_VERTICAL);
    m_commandCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY);
    m_commandCtrl->SetHint("lpr -P printer %f");
    m_commandCtrl->Bind(wxEVT_TEXT, &PrintersPanel::OnDetailChanged, this);
    grid->Add(m_commandCtrl, 1, wxEXPAND);
    
    detailSizer->Add(grid, 0, wxEXPAND);
    
    wxStaticText* hint = new wxStaticText(m_detailPanel, wxID_ANY, "Use %f as placeholder for the filename to print");
    hint->SetForegroundColour(wxColour(128, 128, 128));
    detailSizer->Add(hint, 0, wxTOP, 10);
    
    m_detailPanel->SetSizer(detailSizer);
    contentSizer->Add(m_detailPanel, 1, wxEXPAND);
    mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);
    
    SetSizer(mainSizer);
    
    Bind(wxEVT_BUTTON, &PrintersPanel::OnAddPrinter, this, ID_ADD_PRINTER);
    Bind(wxEVT_BUTTON, &PrintersPanel::OnRemovePrinter, this, ID_REMOVE_PRINTER);
    m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &PrintersPanel::OnPrinterSelected, this);
}

void PrintersPanel::RefreshFromConfig() {
    RefreshList();
    m_currentIndex = -1;
    m_detailPanel->Hide();
    Layout();
}

void PrintersPanel::RefreshList() {
    m_list->DeleteAllItems();
    for (size_t i = 0; i < m_frame->GetConfig().Printers().size(); ++i) {
        m_list->InsertItem(i, m_frame->GetConfig().Printers()[i].name);
    }
}

void PrintersPanel::ShowDetails(int index) {
    if (index < 0 || index >= (int)m_frame->GetConfig().Printers().size()) {
        m_detailPanel->Hide();
        m_currentIndex = -1;
        Layout();
        return;
    }
    
    m_updating = true;
    m_currentIndex = index;
    PrinterConfig& printer = m_frame->GetConfig().Printers()[index];
    
    m_nameCtrl->SetValue(printer.name);
    m_pathCtrl->SetValue(printer.path);
    m_definitionCtrl->SetValue(printer.definition);
    m_descriptionCtrl->SetValue(printer.description);
    m_pollCtrl->SetValue(printer.poll_interval);
    m_commandCtrl->SetValue(printer.command);
    
    m_detailPanel->Show();
    Layout();
    m_updating = false;
}

void PrintersPanel::SaveCurrentDetails() {
    if (m_currentIndex < 0 || m_updating) return;
    
    PrinterConfig& printer = m_frame->GetConfig().Printers()[m_currentIndex];
    printer.name = m_nameCtrl->GetValue().ToStdString();
    printer.path = m_pathCtrl->GetValue().ToStdString();
    printer.definition = m_definitionCtrl->GetValue().ToStdString();
    printer.description = m_descriptionCtrl->GetValue().ToStdString();
    printer.poll_interval = m_pollCtrl->GetValue();
    printer.command = m_commandCtrl->GetValue().ToStdString();
    
    m_list->SetItemText(m_currentIndex, printer.name);
}

void PrintersPanel::OnAddPrinter(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    PrinterConfig printer;
    printer.name = "New Printer";
    printer.poll_interval = 5;
    m_frame->GetConfig().Printers().push_back(printer);
    
    RefreshList();
    int newIndex = m_frame->GetConfig().Printers().size() - 1;
    m_list->SetItemState(newIndex, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    ShowDetails(newIndex);
    m_frame->SetModified(true);
}

void PrintersPanel::OnRemovePrinter(wxCommandEvent& event) {
    wxUnusedVar(event);
    
    if (m_currentIndex >= 0 && m_currentIndex < (int)m_frame->GetConfig().Printers().size()) {
        m_frame->GetConfig().Printers().erase(m_frame->GetConfig().Printers().begin() + m_currentIndex);
        RefreshList();
        m_currentIndex = -1;
        m_detailPanel->Hide();
        Layout();
        m_frame->SetModified(true);
    }
}

void PrintersPanel::OnPrinterSelected(wxListEvent& event) {
    ShowDetails(event.GetIndex());
}

void PrintersPanel::OnDetailChanged(wxCommandEvent& event) {
    wxUnusedVar(event);
    if (m_updating || m_currentIndex < 0) return;
    SaveCurrentDetails();
    m_frame->SetModified(true);
}

void PrintersPanel::OnPollChanged(wxSpinEvent& event) {
    wxUnusedVar(event);
    if (m_updating || m_currentIndex < 0) return;
    SaveCurrentDetails();
    m_frame->SetModified(true);
}
