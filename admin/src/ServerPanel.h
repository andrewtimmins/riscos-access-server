// RISC OS Access Server - Admin GUI Server Panel

#ifndef SERVERPANEL_H
#define SERVERPANEL_H

#include <wx/wx.h>
#include <wx/spinctrl.h>

class MainFrame;

class ServerPanel : public wxPanel {
public:
    ServerPanel(wxWindow* parent, MainFrame* frame);
    void RefreshFromConfig();

private:
    void OnLogLevelChanged(wxCommandEvent& event);
    void OnBroadcastChanged(wxSpinEvent& event);
    void OnAccessPlusChanged(wxCommandEvent& event);
    
    MainFrame* m_frame;
    wxChoice* m_logLevel;
    wxSpinCtrl* m_broadcast;
    wxCheckBox* m_accessPlus;
    bool m_updating = false;
};

#endif // SERVERPANEL_H
