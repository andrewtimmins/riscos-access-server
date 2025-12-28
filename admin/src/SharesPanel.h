// RISC OS Access Server - Admin GUI Shares Panel

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
    
    MainFrame* m_frame;
    wxListCtrl* m_list;
    
    // Detail panel
    wxPanel* m_detailPanel;
    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_pathCtrl;
    wxTextCtrl* m_passwordCtrl;
    wxTextCtrl* m_defaultTypeCtrl;
    wxCheckBox* m_attrProtected;
    wxCheckBox* m_attrReadonly;
    wxCheckBox* m_attrHidden;
    wxCheckBox* m_attrCdrom;
    
    int m_currentIndex = -1;
    bool m_updating = false;
};

#endif // SHARESPANEL_H
