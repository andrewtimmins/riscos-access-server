// RISC OS Access Server - Admin GUI Control Panel

#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include <wx/wx.h>
#include <wx/process.h>

class MainFrame;

class ControlPanel : public wxPanel {
public:
    ControlPanel(wxWindow* parent, MainFrame* frame);
    ~ControlPanel();
    
    void RefreshFromConfig();
    void StopServer();
    void RestartServer();

private:
    void OnStart(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnRestart(wxCommandEvent& event);
    void OnClearLog(wxCommandEvent& event);
    void OnBrowseConfig(wxCommandEvent& event);
    void OnProcessTerminate(wxProcessEvent& event);
    void OnTimer(wxTimerEvent& event);
    
    void UpdateStatus();
    void AppendLog(const wxString& text);
    void ReadProcessOutput();
    
    MainFrame* m_frame;
    
    wxStaticText* m_statusLabel;
    wxStaticText* m_pidLabel;
    wxButton* m_startBtn;
    wxButton* m_stopBtn;
    wxButton* m_restartBtn;
    wxTextCtrl* m_configPath;
    wxTextCtrl* m_bindAddr;
    wxTextCtrl* m_logView;
    
    wxProcess* m_process = nullptr;
    long m_pid = 0;
    wxTimer m_timer;
    bool m_running = false;
    
    wxDECLARE_EVENT_TABLE();
};

#endif // CONTROLPANEL_H
