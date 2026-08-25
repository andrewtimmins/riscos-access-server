/*
  ShareFS Server - Admin GUI Main

  Copyright (C) 2025-2026 Andy Timmins

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// wxWidgets Application Entry Point

#include "MainFrame.h"
#include <wx/cmdline.h>
#include <wx/wx.h>

class ShareFsAdminApp : public wxApp {
public:
  // Accept an optional configuration file path. Without this wxApp's default
  // parser rejects any positional argument and the application exits with a
  // usage message.
  void OnInitCmdLine(wxCmdLineParser &parser) override {
    wxApp::OnInitCmdLine(parser);
    parser.AddParam("configuration file", wxCMD_LINE_VAL_STRING,
                    wxCMD_LINE_PARAM_OPTIONAL);
  }

  bool OnCmdLineParsed(wxCmdLineParser &parser) override {
    if (!wxApp::OnCmdLineParsed(parser))
      return false;
    if (parser.GetParamCount() > 0)
      m_configArg = parser.GetParam(0);
    return true;
  }

  bool OnInit() override {
    if (!wxApp::OnInit())
      return false;

    // macOS otherwise builds the app menu entries from the executable name,
    // producing "Hide Sharefs-admin" and "Quit Sharefs-admin".
    SetAppDisplayName("ShareFS Admin");

    MainFrame *frame = new MainFrame("ShareFS Admin");
    frame->Show();

    if (!m_configArg.empty()) {
      frame->LoadConfig(m_configArg.ToStdString());
      return true;
    }

    for (const wxString &candidate : DefaultConfigPaths()) {
      if (wxFileExists(candidate)) {
        frame->LoadConfig(candidate.ToStdString());
        break;
      }
    }

    return true;
  }

private:
  // Locations searched when no path is given, most specific first.
  static wxArrayString DefaultConfigPaths() {
    wxArrayString paths;
#ifdef __WXMSW__
    wxString progData;
    if (!wxGetEnv("ProgramData", &progData) || progData.empty())
      progData = "C:";
    paths.Add(progData + "\\ShareFS\\sharefs.conf");
    paths.Add("C:/ShareFS/sharefs.conf");
#else
    wxString xdgConfigHome;
    if (!wxGetEnv("XDG_CONFIG_HOME", &xdgConfigHome) || xdgConfigHome.empty())
      xdgConfigHome = wxGetHomeDir() + "/.config";
    paths.Add(xdgConfigHome + "/sharefs/sharefs.conf");
#ifdef __WXOSX__
    paths.Add("/opt/homebrew/etc/sharefs.conf");
    paths.Add("/usr/local/etc/sharefs.conf");
#endif
    paths.Add("/etc/sharefs.conf");
#endif
    paths.Add("sharefs.conf");
    return paths;
  }

  wxString m_configArg;
};

wxIMPLEMENT_APP(ShareFsAdminApp);
