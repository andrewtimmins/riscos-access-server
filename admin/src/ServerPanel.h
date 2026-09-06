/*
  ShareFS - Server Panel

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

#ifndef SERVERPANEL_H
#define SERVERPANEL_H

#include <wx/spinctrl.h>
#include <wx/wx.h>


class MainFrame;

class ServerPanel : public wxPanel {
public:
  ServerPanel(wxWindow *parent, MainFrame *frame);
  void RefreshFromConfig();

private:
  void OnBrowseConfig(wxCommandEvent &event);

public:

private:
  void OnLogLevelChanged(wxCommandEvent &event);
  void OnBroadcastChanged(wxSpinEvent &event);
  void OnAccessPlusChanged(wxCommandEvent &event);
  void OnBindIpChanged(wxCommandEvent &event);
  void OnRefreshInterfaces(wxCommandEvent &event);
  void PopulateNetworkInterfaces();
  void UpdateLogPath();

  MainFrame *m_frame;
  wxChoice *m_logLevel;
  wxSpinCtrl *m_broadcast;
  wxComboBox *m_bindIp;
  wxCheckBox *m_accessPlus;
  wxStaticText *m_logPathLabel;
  wxTextCtrl *m_configPath;
  bool m_updating = false;
};

#endif // SERVERPANEL_H
