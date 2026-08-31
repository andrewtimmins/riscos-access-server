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

#ifndef MIMEPANEL_H
#define MIMEPANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>

class MainFrame;

class MimePanel : public wxPanel {
public:
    MimePanel(wxWindow* parent, MainFrame* frame);
    void RefreshFromConfig();

private:
    void OnAddEntry(wxCommandEvent& event);
    void OnRemoveEntry(wxCommandEvent& event);
    void OnEntrySelected(wxListEvent& event);
    void OnDetailChanged(wxCommandEvent& event);
    
    void RefreshList();
    void ShowDetails(int index);
    void SaveCurrentDetails();
    void UpdateValidation();
    
    MainFrame* m_frame;
    wxListCtrl* m_list;
    
    wxPanel* m_detailPanel;
    wxTextCtrl* m_extCtrl;
    wxTextCtrl* m_typeCtrl;
    wxStaticText* m_typeHint;
    
    int m_currentIndex = -1;
    bool m_updating = false;
};

#endif // MIMEPANEL_H
