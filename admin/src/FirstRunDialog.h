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

// The one screen a new installation shows.
//
// Before this, a fresh install had no configuration file, so the server exited
// with "No configuration file found" and the window opened with every tab
// empty and nothing to say what to do next. The whole of setting up a file
// server is choosing a folder, so that is what this asks.

#ifndef FIRSTRUNDIALOG_H
#define FIRSTRUNDIALOG_H

#include <wx/wx.h>

class wxDirPickerCtrl;

class FirstRunDialog : public wxDialog {
public:
  // `defaultFolder` is the folder offered, and `configPath` is shown so it is
  // never a mystery where the settings went.
  FirstRunDialog(wxWindow *parent, const wxString &defaultFolder,
                 const wxString &configPath);

  wxString Folder() const;
  wxString ShareName() const;
  bool KeepSharing() const;

private:
  wxDirPickerCtrl *m_folder;
  wxTextCtrl *m_name;
  wxCheckBox *m_keepSharing;
};

#endif
