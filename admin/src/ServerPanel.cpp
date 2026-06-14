// ShareFS Server - Admin GUI Server Panel

#include "ServerPanel.h"
#include "MainFrame.h"
#include "UiHelpers.h"
#include <wx/spinctrl.h>
#include <wx/statbox.h>

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

ServerPanel::ServerPanel(wxWindow *parent, MainFrame *frame)
    : wxPanel(parent), m_frame(frame) {
  wxBoxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);

  wxStaticText *title = new wxStaticText(this, wxID_ANY, "Server Settings");
  ui::StyleSectionTitle(title);
  mainSizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, 15);
  mainSizer->AddSpacer(4);

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
  m_logLevel->Append("error");
  m_logLevel->Append("warn");
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

  // Info text
  wxStaticText *info = new wxStaticText(
      this, wxID_ANY,
      "Note: Changes take effect when the server is restarted.");
  info->SetForegroundColour(wxColour(128, 128, 128));
  mainSizer->Add(info, 0, wxALL, 15);

  SetSizer(mainSizer);
}

void ServerPanel::RefreshFromConfig() {
  m_updating = true;

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
