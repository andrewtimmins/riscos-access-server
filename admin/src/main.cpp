/*
  ShareFS - Entry Point

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

// One binary, with or without a window.
//
// This is the entry point of `sharefs` in a build with the GUI. It is not
// wxIMPLEMENT_APP, because the window is one of several things this program
// does: main() hands the arguments to the shared command line in src/cli.c,
// which serves, manages the background service, or calls back into wxEntry
// when there is a window to show. A build without wxWidgets uses src/main.c
// instead and everything else is identical.

#include "MainFrame.h"
#include <wx/cmdline.h>
#include <wx/wx.h>

extern "C" {
#include "cli.h"
#include "paths.h"
}

#ifdef _WIN32
// __argc and __argv, for the WinMain entry point below.
#include <stdlib.h>
#endif

// The words src/cli.c treats as commands. They are skipped when looking for a
// configuration file below, because on Windows wx parses the real command line
// and will see them.
static bool IsCommandWord(const wxString &word) {
  return word == "gui" || word == "serve" || word == "status" ||
         word == "config" || word == "autostart" || word == "service";
}

class ShareFsApp : public wxApp {
public:
  // wx parses the command line itself, and on Windows it takes it from the
  // operating system rather than from the argv this program filtered (see
  // run_gui). So the parser here has to accept everything `sharefs` accepts,
  // or wx exits with a usage message for an option src/cli.c has already
  // handled.
  void OnInitCmdLine(wxCmdLineParser &parser) override {
    wxApp::OnInitCmdLine(parser);
    parser.AddOption("c", "config", "configuration file",
                     wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddSwitch("", "no-ui", "share without opening the window",
                     wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddParam("configuration file", wxCMD_LINE_VAL_STRING,
                    wxCMD_LINE_PARAM_OPTIONAL | wxCMD_LINE_PARAM_MULTIPLE);
  }

  bool OnCmdLineParsed(wxCmdLineParser &parser) override {
    if (!wxApp::OnCmdLineParsed(parser))
      return false;

    wxString value;
    if (parser.Found("config", &value)) {
      m_configArg = value;
      return true;
    }

    for (size_t i = 0; i < parser.GetParamCount(); ++i) {
      const wxString param = parser.GetParam(i);
      if (IsCommandWord(param))
        continue;
      m_configArg = param;
      break;
    }
    return true;
  }

  bool OnInit() override {
    if (!wxApp::OnInit())
      return false;

    // macOS otherwise builds the app menu entries from the executable name,
    // producing "Hide Sharefs" and "Quit Sharefs".
    SetAppDisplayName("ShareFS");

    MainFrame *frame = new MainFrame("ShareFS");
    frame->Show();

    if (!m_configArg.empty()) {
      frame->LoadConfig(m_configArg.ToStdString());
      return true;
    }

    // The search order is shared with the server and the command line, so the
    // file shown here is the file that gets served. It used to be a second
    // list in this file that disagreed with the one in main.c.
    char found[SFS_PATH_MAX];
    if (sfs_paths_find_config(found, sizeof(found)) == 0) {
      frame->LoadConfig(found);
      return true;
    }

    frame->RunFirstRun();
    return true;
  }

private:
  wxString m_configArg;
};

wxIMPLEMENT_APP_NO_MAIN(ShareFsApp);

#ifdef _WIN32
// Set by WinMain, read by run_gui. Null when the console entry point was used,
// in which case wx falls back to the running module itself.
static HINSTANCE g_instance = nullptr;
#endif

// Runs the window. Passed to the command line as its fallback for "no command
// was given", so `sharefs` on its own opens the window and `sharefs serve`
// never builds one. Nothing here runs for a subcommand, which matters on Linux:
// wxWidgets needs a display, and `sharefs serve` is started by systemd where
// there is none.
static int run_gui(int argc, char **argv) {
#ifdef __WXMSW__
  (void)argc;
  (void)argv;
  // wx's own WinMain-compatible entry point, which is the only documented way
  // to hand it a module handle: wxSetInstance lives in wx/msw/private.h and is
  // not ours to call. It takes the command line from the operating system
  // rather than from argv, which is why the parser above accepts every option
  // this program does. GetModuleHandle covers the console entry point, where
  // there was no WinMain to give us one.
  return wxEntry(g_instance ? g_instance : ::GetModuleHandle(NULL));
#else
  return wxEntry(argc, argv);
#endif
}

#ifdef _WIN32
// A Windows executable declares one subsystem at link time, and this one is
// GUI so that double-clicking it does not flash a console. That leaves printf
// with nowhere to go when the same binary is run from a command prompt, so
// borrow the console that launched us. Nothing is created when there is none:
// a shortcut or the service must not sprout a console window.
static void attach_parent_console(void) {
  if (!AttachConsole(ATTACH_PARENT_PROCESS))
    return;
  // freopen rather than freopen_s: the latter is not declared by every MinGW
  // runtime this project is built with, and the return value adds nothing
  // here - if the console cannot be reopened there is nowhere to say so.
  (void)freopen("CONOUT$", "w", stdout);
  (void)freopen("CONOUT$", "w", stderr);
  (void)freopen("CONIN$", "r", stdin);
}
#endif

static int shared_main(int argc, char **argv) {
#ifdef _WIN32
  // Arguments mean somebody typed this, so there is output worth showing.
  if (argc > 1)
    attach_parent_console();
#endif
  return sfs_cli_main(argc, argv, run_gui);
}

#ifdef _WIN32
// Both entry points are defined because which one the C runtime calls depends
// on the subsystem the linker chose: a GUI subsystem executable starts at
// WinMainCRTStartup, which calls WinMain, while a console one starts at
// mainCRTStartup and calls main. Defining only one of them links against some
// toolchains and not others. They do the same thing.
// extern "C", because the C runtime's startup code refers to WinMain by its
// unmangled name; a C++-mangled definition does not satisfy it.
extern "C" int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
  g_instance = hInstance;
  return shared_main(__argc, __argv);
}
#endif

int main(int argc, char **argv) { return shared_main(argc, argv); }
