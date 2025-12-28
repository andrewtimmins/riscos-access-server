// RISC OS Access Server - Admin GUI MIME Panel

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
    
    MainFrame* m_frame;
    wxListCtrl* m_list;
    
    wxPanel* m_detailPanel;
    wxTextCtrl* m_extCtrl;
    wxTextCtrl* m_typeCtrl;
    
    int m_currentIndex = -1;
    bool m_updating = false;
};

#endif // MIMEPANEL_H
