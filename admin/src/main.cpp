// RISC OS Access Server - Admin GUI Main
// wxWidgets Application Entry Point

#include "MainFrame.h"
#include <wx/wx.h>


class AccessAdminApp : public wxApp {
public:
  virtual bool OnInit() override {
    if (!wxApp::OnInit())
      return false;

    MainFrame *frame = new MainFrame("Access/ShareFS Admin");
    frame->Show();

    // Load config: from command line if provided, or search standard paths
    if (argc > 1) {
      frame->LoadConfig(argv[1].ToStdString());
    } else {
#ifdef __WXMSW__
      // Windows: check current directory
      if (wxFileExists("access.conf")) {
        frame->LoadConfig("access.conf");
      }
#else
      // Linux: check /etc/access.conf then current directory
      if (wxFileExists("/etc/access.conf")) {
        frame->LoadConfig("/etc/access.conf");
      } else if (wxFileExists("access.conf")) {
        frame->LoadConfig("access.conf");
      }
#endif
    }

    return true;
  }
};

wxIMPLEMENT_APP(AccessAdminApp);
