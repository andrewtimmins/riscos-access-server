/*
  ShareFS Server - First Run

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

#include "FirstRunDialog.h"
#include "UiHelpers.h"

#include <wx/filepicker.h>

extern "C" {
#include "autostart.h"
}

FirstRunDialog::FirstRunDialog(wxWindow *parent, const wxString &defaultFolder,
                               const wxString &configPath)
    : wxDialog(parent, wxID_ANY, "Welcome to ShareFS", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE) {
  wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *heading =
      new wxStaticText(this, wxID_ANY, "Share a folder with RISC OS");
  ui::StyleSectionTitle(heading);
  sizer->Add(heading, 0, wxLEFT | wxRIGHT | wxTOP, ui::kPagePad);
  sizer->AddSpacer(4);

  wxStaticText *blurb = new wxStaticText(
      this, wxID_ANY,
      "Choose a folder and ShareFS will offer it to RISC OS machines on this "
      "network. You can add more folders, and change any of this, on the "
      "Shares tab afterwards.");
  blurb->SetForegroundColour(ui::MutedText(this));
  blurb->Wrap(FromDIP(400));
  sizer->Add(blurb, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  sizer->AddSpacer(ui::kPagePad);

  wxFlexGridSizer *grid = new wxFlexGridSizer(2, ui::kTightGap, ui::kTightGap);
  grid->AddGrowableCol(1, 1);

  wxStaticText *folderLabel = new wxStaticText(this, wxID_ANY, "Folder");
  ui::StyleFieldLabel(folderLabel);
  grid->Add(folderLabel, 0, wxALIGN_CENTER_VERTICAL);
  m_folder = new wxDirPickerCtrl(this, wxID_ANY, defaultFolder,
                                 "Choose a folder to share", wxDefaultPosition,
                                 wxDefaultSize,
                                 wxDIRP_USE_TEXTCTRL | wxDIRP_DIR_MUST_EXIST);
  grid->Add(m_folder, 1, wxEXPAND);

  wxStaticText *nameLabel = new wxStaticText(this, wxID_ANY, "Called");
  ui::StyleFieldLabel(nameLabel);
  grid->Add(nameLabel, 0, wxALIGN_CENTER_VERTICAL);
  // The name RISC OS shows in its filer window. Kept short because it appears
  // as the first component of every path on the client.
  m_name = new wxTextCtrl(this, wxID_ANY, "Public");
  grid->Add(m_name, 1, wxEXPAND);

  sizer->Add(grid, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  sizer->AddSpacer(ui::kPagePad);

  m_keepSharing = new wxCheckBox(this, wxID_ANY,
                                 "Keep sharing when this window is closed");
  m_keepSharing->SetValue(true);
  m_keepSharing->SetToolTip(wxString::Format(
      "ShareFS sets this up using %s.", sfs_autostart_mechanism()));
  if (sfs_autostart_query() == SFS_AUTOSTART_UNSUPPORTED) {
    m_keepSharing->SetValue(false);
    m_keepSharing->Enable(false);
  }
  sizer->Add(m_keepSharing, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  sizer->AddSpacer(ui::kTightGap);

  wxStaticText *where = new wxStaticText(
      this, wxID_ANY, "Settings are kept in " + configPath);
  where->SetForegroundColour(ui::MutedText(this));
  where->Wrap(FromDIP(400));
  sizer->Add(where, 0, wxEXPAND | wxLEFT | wxRIGHT, ui::kPagePad);
  sizer->AddSpacer(ui::kPagePad);

  if (wxSizer *buttons = CreateStdDialogButtonSizer(wxOK | wxCANCEL)) {
    if (wxWindow *ok = FindWindow(wxID_OK))
      static_cast<wxButton *>(ok)->SetLabel("Start Sharing");
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
               ui::kPagePad);
  }

  SetSizerAndFit(sizer);
  CentreOnParent();
}

wxString FirstRunDialog::Folder() const { return m_folder->GetPath(); }

wxString FirstRunDialog::ShareName() const {
  wxString name = m_name->GetValue().Trim().Trim(false);
  return name.empty() ? wxString("Public") : name;
}

bool FirstRunDialog::KeepSharing() const { return m_keepSharing->GetValue(); }
