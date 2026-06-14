// ShareFS Server - Admin GUI Server Panel

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
  void OnLogLevelChanged(wxCommandEvent &event);
  void OnBroadcastChanged(wxSpinEvent &event);
  void OnAccessPlusChanged(wxCommandEvent &event);
  void OnBindIpChanged(wxCommandEvent &event);
  void OnRefreshInterfaces(wxCommandEvent &event);
  void PopulateNetworkInterfaces();

  MainFrame *m_frame;
  wxChoice *m_logLevel;
  wxSpinCtrl *m_broadcast;
  wxComboBox *m_bindIp;
  wxCheckBox *m_accessPlus;
  bool m_updating = false;
};

#endif // SERVERPANEL_H
