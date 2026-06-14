// ShareFS Server - Admin GUI Main
// wxWidgets Application Entry Point

#include "MainFrame.h"
#include <wx/wx.h>


class ShareFsAdminApp : public wxApp {
public:
  virtual bool OnInit() override {
    if (!wxApp::OnInit())
      return false;

    MainFrame *frame = new MainFrame("ShareFS Admin");
    frame->Show();

    if (argc > 1) {
      frame->LoadConfig(argv[1].ToStdString());
    } else {
#ifdef __WXMSW__
      wxString progData;
      if (!wxGetEnv("ProgramData", &progData) || progData.empty())
        progData = "C:";
      wxString pdPath = progData + "\\ShareFS\\sharefs.conf";
      if (wxFileExists(pdPath)) {
        frame->LoadConfig(pdPath.ToStdString());
      } else if (wxFileExists("C:/ShareFS/sharefs.conf")) {
        frame->LoadConfig("C:/ShareFS/sharefs.conf");
      } else if (wxFileExists("sharefs.conf")) {
        frame->LoadConfig("sharefs.conf");
      }
#else
      wxString xdgConfigHome;
      if (!wxGetEnv("XDG_CONFIG_HOME", &xdgConfigHome) || xdgConfigHome.empty())
        xdgConfigHome = wxGetHomeDir() + "/.config";
      wxString xdgPath = xdgConfigHome + "/sharefs/sharefs.conf";
      if (wxFileExists(xdgPath)) {
        frame->LoadConfig(xdgPath.ToStdString());
      } else if (wxFileExists("/etc/sharefs.conf")) {
        frame->LoadConfig("/etc/sharefs.conf");
      } else if (wxFileExists("sharefs.conf")) {
        frame->LoadConfig("sharefs.conf");
      }
#endif
    }

    return true;
  }
};

wxIMPLEMENT_APP(ShareFsAdminApp);
