/*
  ShareFS - MIME Panel

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

#include "MimePanel.h"
#include "MainFrame.h"
#include "UiHelpers.h"

enum { ID_ADD_MIME = wxID_HIGHEST + 300, ID_REMOVE_MIME, ID_MIME_LIST };

// Well-known RISC OS filetypes, shown in the list so the hex codes are
// readable at a glance rather than needing to be looked up.
static const struct {
  const char *type;
  const char *name;
} kFiletypeNames[] = {
    {"FFF", "Text"},      {"FFD", "Data"},     {"FFB", "BASIC"},
    {"FEB", "Obey"},      {"FAF", "HTML"},     {"AFF", "DrawFile"},
    {"FF9", "Sprite"},    {"ADF", "PDF"},      {"C85", "JPEG"},
    {"B60", "PNG"},       {"695", "GIF"},      {"69C", "BMP"},
    {"FF0", "TIFF"},      {"A91", "Zip"},      {"F79", "CSS"},
    {"F80", "XML"},       {"F81", "JavaScript"},{"DFE", "CSV"},
    {"1AD", "MP3"},       {"FB1", "WAV"},      {"FB2", "AVI"},
    {"BF8", "MPEG"},      {"102", "Source"},   {"FFE", "Object"},
    {nullptr, nullptr}};

static wxString FiletypeName(const wxString &hex) {
  wxString upper = hex.Upper();
  for (int i = 0; kFiletypeNames[i].type; ++i)
    if (upper == kFiletypeNames[i].type)
      return kFiletypeNames[i].name;
  return wxString();
}

// A filetype must be exactly three hex digits to be usable by the server.
static bool FiletypeIsValid(const wxString &hex) {
  if (hex.length() != 3)
    return false;
  for (size_t i = 0; i < hex.length(); ++i)
    if (!wxIsxdigit(hex[i]))
      return false;
  return true;
}

MimePanel::MimePanel(wxWindow *parent, MainFrame *frame)
    : wxPanel(parent), m_frame(frame) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  // Title and buttons
  wxBoxSizer *headerSizer = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText *title = new wxStaticText(this, wxID_ANY, "MIME Type Mappings");
  ui::StyleSectionTitle(title);
  headerSizer->Add(title, 1, wxALIGN_CENTER_VERTICAL | wxBOTTOM, 4);

  wxButton *addBtn = new wxButton(this, ID_ADD_MIME, "Add");
  wxButton *removeBtn = new wxButton(this, ID_REMOVE_MIME, "Remove");
  headerSizer->Add(addBtn, 0, wxLEFT, 5);
  headerSizer->Add(removeBtn, 0, wxLEFT, 5);
  mainSizer->Add(headerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 15);
  mainSizer->AddSpacer(4);

  wxStaticText *desc =
      new wxStaticText(this, wxID_ANY,
                       "Map file extensions to RISC OS filetypes (hex values "
                       "like FFF for Text)");
  desc->SetForegroundColour(ui::MutedText(this));
  mainSizer->Add(desc, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);

  wxBoxSizer *contentSizer = new wxBoxSizer(wxHORIZONTAL);

  m_list = new wxListCtrl(this, ID_MIME_LIST, wxDefaultPosition, wxDefaultSize,
                          wxLC_REPORT | wxLC_SINGLE_SEL);
  m_list->SetMinSize(wxSize(320, 200));
  m_list->InsertColumn(0, "Extension", wxLIST_FORMAT_LEFT, 120);
  m_list->InsertColumn(1, "Filetype", wxLIST_FORMAT_LEFT, 120);
  m_list->InsertColumn(2, "Description", wxLIST_FORMAT_LEFT, 160);
  m_list->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) {
    e.Skip();
    ui::ResizeListColumns(m_list, {2, 2, 3});
  });
  // Proportion 3 against the detail pane's 2: the list is the primary content
  // here, and previously it was pinned to its minimum width with two thirds of
  // the tab left empty.
  contentSizer->Add(m_list, 3, wxEXPAND | wxRIGHT, ui::kGroupGap);

  // Detail panel
  m_detailPanel = new wxPanel(this);
  m_detailPanel->Hide();
  wxBoxSizer *detailSizer = new wxBoxSizer(wxVERTICAL);

  // Columns only; see the note in PrintersPanel.
  wxFlexGridSizer *grid = new wxFlexGridSizer(2, ui::kRowGap, ui::kLabelGap);
  grid->AddGrowableCol(1);

  grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Extension:"), 0,
            wxALIGN_CENTER_VERTICAL);
  m_extCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition,
                             wxSize(100, -1));
  m_extCtrl->SetToolTip("File extension without leading dot (e.g. txt, html)");
  m_extCtrl->Bind(wxEVT_TEXT, &MimePanel::OnDetailChanged, this);
  grid->Add(m_extCtrl, 0);

  grid->Add(new wxStaticText(m_detailPanel, wxID_ANY, "Filetype (hex):"), 0,
            wxALIGN_CENTER_VERTICAL);
  m_typeCtrl = new wxTextCtrl(m_detailPanel, wxID_ANY, "", wxDefaultPosition,
                              wxDefaultSize);
  m_typeCtrl->SetMaxLength(3);
  m_typeCtrl->SetToolTip("Three-digit hexadecimal RISC OS filetype (e.g. FFF for Text)");
  m_typeCtrl->Bind(wxEVT_TEXT, &MimePanel::OnDetailChanged, this);
  grid->Add(m_typeCtrl, 0);

  grid->AddSpacer(0);
  m_typeHint = new wxStaticText(m_detailPanel, wxID_ANY, "");
  m_typeHint->Hide();
  grid->Add(m_typeHint, 0);

  detailSizer->Add(grid, 0, wxEXPAND);

  // Common filetypes hint
  wxStaticText *hint = new wxStaticText(m_detailPanel, wxID_ANY,
                                        "Common types:\n"
                                        "  FFF = Text\n"
                                        "  FFD = Data\n"
                                        "  FAF = HTML\n"
                                        "  AFF = DrawFile\n"
                                        "  FF9 = Sprite\n"
                                        "  C85 = JPEG\n"
                                        "  B60 = PNG");
  hint->SetForegroundColour(ui::MutedText(m_detailPanel));
  detailSizer->Add(hint, 0, wxTOP, 15);

  m_detailPanel->SetSizer(detailSizer);
  contentSizer->Add(m_detailPanel, 2, wxEXPAND);
  mainSizer->Add(contentSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);

  SetSizer(mainSizer);

  Bind(wxEVT_BUTTON, &MimePanel::OnAddEntry, this, ID_ADD_MIME);
  Bind(wxEVT_BUTTON, &MimePanel::OnRemoveEntry, this, ID_REMOVE_MIME);
  m_list->Bind(wxEVT_LIST_ITEM_SELECTED, &MimePanel::OnEntrySelected, this);
}

void MimePanel::RefreshFromConfig() {
  RefreshList();
  if (m_list->GetItemCount() > 0) {
    m_list->SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
    ShowDetails(0);
  } else {
    m_currentIndex = -1;
    m_detailPanel->Hide();
    Layout();
  }
}

void MimePanel::RefreshList() {
  m_list->DeleteAllItems();
  for (size_t i = 0; i < m_frame->GetConfig().MimeMap().size(); ++i) {
    MimeEntry &entry = m_frame->GetConfig().MimeMap()[i];
    long idx = m_list->InsertItem(i, entry.ext);
    m_list->SetItem(idx, 1, entry.filetype);
    m_list->SetItem(idx, 2, FiletypeName(entry.filetype));
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
  MimeEntry &entry = m_frame->GetConfig().MimeMap()[index];

  m_extCtrl->SetValue(entry.ext);
  m_typeCtrl->SetValue(entry.filetype);
  UpdateValidation();

  m_detailPanel->Show();
  Layout();
  m_updating = false;
}

void MimePanel::SaveCurrentDetails() {
  if (m_currentIndex < 0 || m_updating)
    return;

  MimeEntry &entry = m_frame->GetConfig().MimeMap()[m_currentIndex];
  std::string newExt = m_extCtrl->GetValue().ToStdString();
  std::string newType = m_typeCtrl->GetValue().ToStdString();

  if (entry.ext != newExt) {
    entry.ext = newExt;
    m_list->SetItem(m_currentIndex, 0, entry.ext);
  }

  if (entry.filetype != newType) {
    entry.filetype = newType;
    m_list->SetItem(m_currentIndex, 1, entry.filetype);
    m_list->SetItem(m_currentIndex, 2, FiletypeName(entry.filetype));
  }

  UpdateValidation();
}

void MimePanel::UpdateValidation() {
  const wxString type = m_typeCtrl->GetValue();
  const bool bad = !type.empty() && !FiletypeIsValid(type);
  ui::SetFieldError(m_typeCtrl, m_typeHint, bad,
                    "Needs exactly three hex digits");
  m_detailPanel->Layout();
}

void MimePanel::OnAddEntry(wxCommandEvent &event) {
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

void MimePanel::OnRemoveEntry(wxCommandEvent &event) {
  wxUnusedVar(event);

  if (m_currentIndex < 0 ||
      m_currentIndex >= (int)m_frame->GetConfig().MimeMap().size())
    return;

  const std::string& ext = m_frame->GetConfig().MimeMap()[m_currentIndex].ext;
  if (!ui::Ask(this, "Confirm Remove", wxString::Format("Remove mapping for '.%s'?", ext)))
    return;

  m_frame->GetConfig().MimeMap().erase(
      m_frame->GetConfig().MimeMap().begin() + m_currentIndex);
  RefreshList();
  m_currentIndex = -1;
  m_detailPanel->Hide();
  Layout();
  m_frame->SetModified(true);
}

void MimePanel::OnEntrySelected(wxListEvent &event) {
  ShowDetails(event.GetIndex());
}

void MimePanel::OnDetailChanged(wxCommandEvent &event) {
  if (m_updating || m_currentIndex < 0)
    return;

  // Sanitize extension: strip leading dot, lowercase, alphanumeric only
  if (event.GetEventObject() == m_extCtrl) {
    wxString val = m_extCtrl->GetValue();
    wxString clean;
    bool changed = false;
    long pos = m_extCtrl->GetInsertionPoint();

    for (wxString::iterator it = val.begin(); it != val.end(); ++it) {
      wxChar c = *it;
      if (c == '.' && clean.empty()) { changed = true; continue; } // strip leading dot
      if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
        clean += c;
      } else if (c >= 'A' && c <= 'Z') {
        clean += wxTolower(c);
        changed = true;
      } else {
        changed = true;
      }
    }

    if (changed) {
      m_updating = true;
      m_extCtrl->ChangeValue(clean);
      if (pos > (long)clean.length()) pos = clean.length();
      m_extCtrl->SetInsertionPoint(pos);
      m_updating = false;
    }
  }

  // Validate/Sanitize Filetype (Hex only, Uppercase)
  if (event.GetEventObject() == m_typeCtrl) {
    wxString val = m_typeCtrl->GetValue();
    wxString clean;
    bool changed = false;
    long pos = m_typeCtrl->GetInsertionPoint(); // Preserve cursor

    for (wxString::iterator it = val.begin(); it != val.end(); ++it) {
      wxChar c = *it;
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')) {
        clean += c;
      } else if (c >= 'a' && c <= 'f') {
        clean += wxToupper(c);
        changed = true;
      } else {
        changed = true; // Skip invalid char
      }
    }

    if (changed) {
      m_updating = true;
      m_typeCtrl->ChangeValue(clean);
      // Correct position adjustment (simple approach)
      if (pos > (long)clean.length())
        pos = clean.length();
      m_typeCtrl->SetInsertionPoint(pos);
      m_updating = false;
    }
  }

  SaveCurrentDetails();
  m_frame->SetModified(true);
}
