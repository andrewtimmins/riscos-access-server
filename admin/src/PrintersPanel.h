// RISC OS Access Server - Admin GUI Printers Panel

#ifndef PRINTERSPANEL_H
#define PRINTERSPANEL_H

#include <wx/wx.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>

class MainFrame;

class PrintersPanel : public wxPanel {
public:
    PrintersPanel(wxWindow* parent, MainFrame* frame);
    void RefreshFromConfig();

private:
    void OnAddPrinter(wxCommandEvent& event);
    void OnRemovePrinter(wxCommandEvent& event);
    void OnPrinterSelected(wxListEvent& event);
    void OnDetailChanged(wxCommandEvent& event);
    void OnPollChanged(wxSpinEvent& event);
    
    void RefreshList();
    void ShowDetails(int index);
    void SaveCurrentDetails();
    
    MainFrame* m_frame;
    wxListCtrl* m_list;
    
    wxPanel* m_detailPanel;
    wxTextCtrl* m_nameCtrl;
    wxTextCtrl* m_pathCtrl;
    wxTextCtrl* m_definitionCtrl;
    wxTextCtrl* m_descriptionCtrl;
    wxSpinCtrl* m_pollCtrl;
    wxTextCtrl* m_commandCtrl;
    
    int m_currentIndex = -1;
    bool m_updating = false;
};

#endif // PRINTERSPANEL_H
