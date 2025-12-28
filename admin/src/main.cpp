// RISC OS Access Server - Admin GUI Main
// wxWidgets Application Entry Point

#include <wx/wx.h>
#include "MainFrame.h"

class AccessAdminApp : public wxApp {
public:
    virtual bool OnInit() override {
        if (!wxApp::OnInit())
            return false;
        
        MainFrame* frame = new MainFrame("Access/ShareFS Admin");
        frame->Show();
        
        // Load config: from command line if provided, otherwise try access.conf
        if (argc > 1) {
            frame->LoadConfig(argv[1].ToStdString());
        } else if (wxFileExists("access.conf")) {
            frame->LoadConfig("access.conf");
        }
        
        return true;
    }
};

wxIMPLEMENT_APP(AccessAdminApp);
