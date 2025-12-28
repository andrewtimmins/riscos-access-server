// RISC OS Access Server - Admin GUI Main Frame Header

#ifndef MAINFRAME_H
#define MAINFRAME_H

#include <wx/wx.h>
#include <wx/notebook.h>
#include "ConfigIO.h"

class ServerPanel;
class SharesPanel;
class PrintersPanel;
class MimePanel;
class ControlPanel;

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title);
    
    void LoadConfig(const std::string& path);
    void SaveConfig();
    void RevertConfig();
    
    void SetModified(bool modified);
    bool IsModified() const { return m_modified; }
    RasConfig& GetConfig() { return m_config; }
    const std::string& GetConfigPath() const { return m_configPath; }
    
    // Access to control panel for restart
    ControlPanel* GetControlPanel() { return m_controlPanel; }

private:
    void OnApply(wxCommandEvent& event);
    void OnRevert(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnAbout(wxCommandEvent& event);
    
    void UpdateTitle();
    void CreateMenuBar();
    void CreateToolBar();
    
    wxNotebook* m_notebook;
    ServerPanel* m_serverPanel;
    SharesPanel* m_sharesPanel;
    PrintersPanel* m_printersPanel;
    MimePanel* m_mimePanel;
    ControlPanel* m_controlPanel;
    
    wxButton* m_applyBtn;
    wxButton* m_revertBtn;
    
    RasConfig m_config;
    std::string m_configPath;
    bool m_modified = false;
    
    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_APPLY = wxID_HIGHEST + 1,
    ID_REVERT
};

#endif // MAINFRAME_H
