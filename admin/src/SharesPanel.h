/*
  ShareFS Server - Admin GUI Shares Panel

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

#ifndef SHARESPANEL_H
#define SHARESPANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>

class MainFrame;

class SharesPanel : public wxPanel {
public:
    SharesPanel(wxWindow* parent, MainFrame* frame);
    void RefreshFromConfig();

private:
    void OnAddShare(wxCommandEvent& event);
    void OnRemoveShare(wxCommandEvent& event);
    void OnShareSelected(wxListEvent& event);
    void OnBrowsePath(wxCommandEvent& event);
    void OnDetailChanged(wxCommandEvent& event);
    void OnAttrChanged(wxCommandEvent& event);
    
    void RefreshList();
    void ShowDetails(int index);
    void SaveCurrentDetails();
    void UpdateValidation();
    
    MainFrame* m_frame;
    wxListCtrl* m_list;
    
    // Detail panel
    wxPanel* m_detailPanel;
    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_pathCtrl;
    wxStaticText* m_pathHint;
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_defaultTypeCtrl;
    wxCheckBox* m_attrProtected;
    wxCheckBox* m_attrReadonly;
    wxCheckBox* m_attrHidden;
    wxCheckBox* m_attrSubdir;
    wxCheckBox* m_attrCdrom;
    
    int m_currentIndex = -1;
    bool m_updating = false;
};

#endif // SHARESPANEL_H
