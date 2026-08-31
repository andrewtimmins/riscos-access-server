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

#include "ServerPanel.h"
#include "MainFrame.h"
#include "UiHelpers.h"
#include <wx/filedlg.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>

extern "C" {
#include "log.h"
#include "paths.h"
}

#ifdef _WIN32
#include <iphlpapi.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "iphlpapi.lib")
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>

#endif

// Asks src/log.c where the log goes rather than keeping a second copy of the
// rules here. The two used to be maintained separately, and the tab could
// name a file the server had never written to.
static wxString DefaultLogPath() {
  char preferred[SFS_PATH_MAX];
  char fallback[SFS_PATH_MAX];
  sfs_log_default_paths(preferred, sizeof(preferred), fallback,
                        sizeof(fallback));

  // The preferred path needs root on most systems, so report it only when it
  // is actually there; otherwise the log is in the fallback.
  const wxString first = wxString::FromUTF8(preferred);
  if (wxFileExists(first))
    return first;
  return wxString::FromUTF8(fallback);
}

enum { ID_BROWSE_CONFIG = wxID_HIGHEST + 500 };

ServerPanel::ServerPanel(wxWindow *parent, MainFrame *frame)
    : wxPanel(parent), m_frame(frame) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *title = new wxStaticText(this, wxID_ANY, "Server Settings");
  ui::StyleSectionTitle(title);
  mainSizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 15);
  mainSizer->AddSpacer(4);

  // Which file these settings came from, and a way to open a different one.
  wxBoxSizer *configRow = new wxBoxSizer(wxHORIZONTAL);
  wxStaticText *configLabel =
      new wxStaticText(this, wxID_ANY, "Configuration file:");
  ui::StyleFieldLabel(configLabel);
  configRow->Add(configLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT,
                 ui::kLabelGap);

  m_configPath = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
                                wxDefaultSize, wxTE_READONLY);
  m_configPath->SetHint("No configuration loaded");
  configRow->Add(m_configPath, 1, wxALIGN_CENTER_VERTICAL);

  wxButton *browseBtn = new wxButton(this, ID_BROWSE_CONFIG, "Open...");
  browseBtn->Bind(wxEVT_BUTTON, &ServerPanel::OnBrowseConfig, this);
  configRow->Add(browseBtn, 0, wxLEFT, ui::kTightGap);

  mainSizer->Add(configRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 15);

  // Settings group
  wxStaticBoxSizer *settingsBox =
      new wxStaticBoxSizer(wxVERTICAL, this, "Configuration");
  wxFlexGridSizer *grid = new wxFlexGridSizer(4, 2, 10, 15);
  grid->AddGrowableCol(1);

  // Bind IP with interface dropdown
  grid->Add(new wxStaticText(this, wxID_ANY, "Bind IP Address:"), 0,
            wxALIGN_CENTER_VERTICAL);

  wxBoxSizer *bindSizer = new wxBoxSizer(wxHORIZONTAL);
  m_bindIp = new wxComboBox(this, wxID_ANY, "", wxDefaultPosition,
                            wxDefaultSize, 0, nullptr, wxCB_DROPDOWN);
  m_bindIp->SetHint("0.0.0.0 (All interfaces)");
  m_bindIp->Bind(wxEVT_TEXT, &ServerPanel::OnBindIpChanged, this);
  m_bindIp->Bind(wxEVT_COMBOBOX, &ServerPanel::OnBindIpChanged, this);
  bindSizer->Add(m_bindIp, 1, wxEXPAND);

  wxButton *refreshBtn = new wxButton(this, wxID_ANY, "Refresh",
                                      wxDefaultPosition, wxSize(80, -1));
  refreshBtn->Bind(wxEVT_BUTTON, &ServerPanel::OnRefreshInterfaces, this);
  bindSizer->Add(refreshBtn, 0, wxLEFT, 5);

  grid->Add(bindSizer, 1, wxEXPAND);

  // Populate network interfaces when config loads (not during construction)
  // PopulateNetworkInterfaces();

  // Log level
  grid->Add(new wxStaticText(this, wxID_ANY, "Log Level:"), 0,
            wxALIGN_CENTER_VERTICAL);
  m_logLevel = new wxChoice(this, wxID_ANY);
  // These must match sfs_log_level_from_string() in src/log.c exactly.
  m_logLevel->Append("none");
  m_logLevel->Append("error");
  m_logLevel->Append("info");
  m_logLevel->Append("debug");
  m_logLevel->Append("protocol");
  m_logLevel->SetSelection(2); // Default: info
  m_logLevel->Bind(wxEVT_CHOICE, &ServerPanel::OnLogLevelChanged, this);
  grid->Add(m_logLevel, 1, wxEXPAND);

  // Broadcast interval
  grid->Add(new wxStaticText(this, wxID_ANY, "Broadcast Interval:"), 0,
            wxALIGN_CENTER_VERTICAL);
  wxBoxSizer *broadcastSizer = new wxBoxSizer(wxHORIZONTAL);
  m_broadcast = new wxSpinCtrl(this, wxID_ANY, "3", wxDefaultPosition,
                               wxDefaultSize, wxSP_ARROW_KEYS, 0, 3600, 3);
  m_broadcast->Bind(wxEVT_SPINCTRL, &ServerPanel::OnBroadcastChanged, this);
  broadcastSizer->Add(m_broadcast, 0);
  broadcastSizer->Add(
      new wxStaticText(this, wxID_ANY, " seconds (0 = disabled)"), 0,
      wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
  grid->Add(broadcastSizer, 1);

  // Access+ authentication
  grid->Add(new wxStaticText(this, wxID_ANY, "Access+ Authentication:"), 0,
            wxALIGN_CENTER_VERTICAL);
  m_accessPlus =
      new wxCheckBox(this, wxID_ANY, "Enable password protection for shares");
  m_accessPlus->Bind(wxEVT_CHECKBOX, &ServerPanel::OnAccessPlusChanged, this);
  grid->Add(m_accessPlus, 1);

  settingsBox->Add(grid, 1, wxEXPAND | wxALL, 10);
  mainSizer->Add(settingsBox, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);

  wxStaticText *info = new wxStaticText(
      this, wxID_ANY,
      "Changes take effect when the server is restarted.");
  info->SetForegroundColour(ui::MutedText(this));
  mainSizer->Add(info, 0, wxLEFT | wxRIGHT | wxTOP, 15);

  // Reference card. The lower half of this tab was previously empty; the
  // ports and log location are the two things most often needed when a client
  // cannot see the server.
  wxStaticBoxSizer *refBox =
      new wxStaticBoxSizer(wxVERTICAL, this, "Network and files");
  wxFlexGridSizer *refGrid =
      new wxFlexGridSizer(2, ui::kRowGap, ui::kLabelGap);
  refGrid->AddGrowableCol(1);

  auto addRefRow = [&](const wxString &label, const wxString &value) {
    wxStaticText *l = new wxStaticText(this, wxID_ANY, label);
    ui::StyleFieldLabel(l);
    refGrid->Add(l, 0, wxALIGN_CENTER_VERTICAL);
    refGrid->Add(new wxStaticText(this, wxID_ANY, value), 1, wxEXPAND);
  };

  addRefRow("Freeway discovery:", "UDP 32770");
  addRefRow("Access+ authentication:", "UDP 32771");
  addRefRow("File transfer:", "UDP 49171");

  m_logPathLabel = new wxStaticText(this, wxID_ANY, wxString());
  wxStaticText *logLabel = new wxStaticText(this, wxID_ANY, "Server log:");
  ui::StyleFieldLabel(logLabel);
  refGrid->Add(logLabel, 0, wxALIGN_CENTER_VERTICAL);
  refGrid->Add(m_logPathLabel, 1, wxEXPAND);

  refBox->Add(refGrid, 1, wxEXPAND | wxALL, 10);
  mainSizer->Add(refBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 15);

  // Reflect an explicit log_file when the configuration sets one.
  const std::string &configured = m_frame->GetConfig().Server().log_file;
  m_logPathLabel->SetLabel(configured.empty() ? DefaultLogPath()
                                              : wxString(configured));

  mainSizer->AddStretchSpacer();

  SetSizer(mainSizer);
}

void ServerPanel::OnBrowseConfig(wxCommandEvent &event) {
  wxUnusedVar(event);

  wxFileDialog dlg(this, "Open configuration file", "", "",
                   "Configuration files (*.conf)|*.conf|All files (*.*)|*.*",
                   wxFD_OPEN | wxFD_FILE_MUST_EXIST);
  if (dlg.ShowModal() != wxID_OK)
    return;

  m_frame->LoadConfig(dlg.GetPath().ToStdString());
}

void ServerPanel::RefreshFromConfig() {
  m_updating = true;

  m_configPath->ChangeValue(m_frame->GetConfigPath());

  ServerConfig &cfg = m_frame->GetConfig().Server();

  // Populate network interfaces first
  PopulateNetworkInterfaces();

  m_bindIp->ChangeValue(cfg.bind_ip);

  // Log level
  int idx = m_logLevel->FindString(cfg.log_level);
  if (idx != wxNOT_FOUND) {
    m_logLevel->SetSelection(idx);
  }

  int val = cfg.broadcast_interval;
  if (val < 0)
    val = 0;
  if (val > 3600)
    val = 3600;
  m_broadcast->SetValue(val);
  m_accessPlus->SetValue(cfg.access_plus);

  m_updating = false;
}

void ServerPanel::OnLogLevelChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_updating)
    return;

  m_frame->GetConfig().Server().log_level =
      m_logLevel->GetStringSelection().ToStdString();
  m_frame->SetModified(true);
}

void ServerPanel::OnBroadcastChanged(wxSpinEvent &event) {
  wxUnusedVar(event);
  if (m_updating)
    return;

  m_frame->GetConfig().Server().broadcast_interval = m_broadcast->GetValue();
  m_frame->SetModified(true);
}

void ServerPanel::OnAccessPlusChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_updating)
    return;

  m_frame->GetConfig().Server().access_plus = m_accessPlus->GetValue();
  m_frame->SetModified(true);
}

void ServerPanel::OnBindIpChanged(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_updating)
    return;

  m_frame->GetConfig().Server().bind_ip = m_bindIp->GetValue().ToStdString();
  m_frame->SetModified(true);
}

void ServerPanel::OnRefreshInterfaces(wxCommandEvent &event) {
  wxUnusedVar(event);
  PopulateNetworkInterfaces();
}

void ServerPanel::PopulateNetworkInterfaces() {
  wxString currentValue = m_bindIp->GetValue();
  m_bindIp->Clear();

  // Always add the "all interfaces" option
  m_bindIp->Append("0.0.0.0 (All interfaces)");

#ifdef _WIN32
  // Windows: Use GetAdaptersAddresses
  ULONG bufferSize = 15000;
  PIP_ADAPTER_ADDRESSES addresses = (PIP_ADAPTER_ADDRESSES)malloc(bufferSize);

  if (GetAdaptersAddresses(AF_INET,
                           GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
                           NULL, addresses, &bufferSize) == NO_ERROR) {
    PIP_ADAPTER_ADDRESSES current = addresses;
    while (current) {
      if (current->OperStatus == IfOperStatusUp) {
        PIP_ADAPTER_UNICAST_ADDRESS unicast = current->FirstUnicastAddress;
        while (unicast) {
          if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
            struct sockaddr_in *addr =
                (struct sockaddr_in *)unicast->Address.lpSockaddr;
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr->sin_addr), ipStr, INET_ADDRSTRLEN);

            // Convert adapter name from wide string
            wxString adapterName(current->FriendlyName);
            wxString entry = wxString::Format("%s (%s)", ipStr, adapterName);
            m_bindIp->Append(entry);
          }
          unicast = unicast->Next;
        }
      }
      current = current->Next;
    }
  }
  free(addresses);

#else
  // Linux/Unix: Use getifaddrs
  struct ifaddrs *ifaddr, *ifa;

  if (getifaddrs(&ifaddr) == 0) {
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL)
        continue;

      if (ifa->ifa_addr->sa_family == AF_INET) {
        struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(addr->sin_addr), ipStr, INET_ADDRSTRLEN);

        // Skip loopback unless it's the only interface
        if (strcmp(ipStr, "127.0.0.1") != 0) {
          wxString entry = wxString::Format("%s (%s)", ipStr, ifa->ifa_name);
          m_bindIp->Append(entry);
        }
      }
    }
    freeifaddrs(ifaddr);
  }
#endif

  // Restore the previous value if it exists
  if (!currentValue.IsEmpty()) {
    // If it's just an IP, try to find it in the list
    bool found = false;
    for (unsigned int i = 0; i < m_bindIp->GetCount(); i++) {
      wxString item = m_bindIp->GetString(i);
      if (item.StartsWith(currentValue + " ") || item == currentValue) {
        m_bindIp->SetSelection(i);
        found = true;
        break;
      }
    }

    // If not found in list, set the value directly (allows manual entry)
    if (!found) {
      m_bindIp->SetValue(currentValue);
    }
  } else {
    m_bindIp->SetSelection(0); // Select "All interfaces"
  }
}
