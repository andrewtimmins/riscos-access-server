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
      // Windows: %ProgramData%\AccessServer, then legacy C:\AccessServer, then CWD
      wxString progData;
      if (!wxGetEnv("ProgramData", &progData) || progData.empty())
        progData = "C:";
      wxString pdPath = progData + "\\AccessServer\\access.conf";
      if (wxFileExists(pdPath)) {
        frame->LoadConfig(pdPath.ToStdString());
      } else if (wxFileExists("C:/AccessServer/access.conf")) {
        frame->LoadConfig("C:/AccessServer/access.conf");
      } else if (wxFileExists("access.conf")) {
        frame->LoadConfig("access.conf");
      }
#else
      // Linux: XDG_CONFIG_HOME, ~/.config, /etc/riscos-access-server, /etc, CWD
      wxString xdgConfigHome;
      if (!wxGetEnv("XDG_CONFIG_HOME", &xdgConfigHome) || xdgConfigHome.empty())
        xdgConfigHome = wxGetHomeDir() + "/.config";
      wxString xdgPath = xdgConfigHome + "/riscos-access-server/access.conf";
      if (wxFileExists(xdgPath)) {
        frame->LoadConfig(xdgPath.ToStdString());
      } else if (wxFileExists("/etc/riscos-access-server/access.conf")) {
        frame->LoadConfig("/etc/riscos-access-server/access.conf");
      } else if (wxFileExists("/etc/access.conf")) {
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
