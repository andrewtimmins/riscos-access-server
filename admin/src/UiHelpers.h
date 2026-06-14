// ShareFS Server - Admin GUI layout helpers

#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include <wx/listctrl.h>
#include <wx/wx.h>

namespace ui {

inline void StyleSectionTitle(wxStaticText *title) {
  wxFont f = title->GetFont();
  if (f.GetPointSize() > 0)
    f.SetPointSize(f.GetPointSize() + 2);
  else
    f.SetPointSize(12);
  f.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(f);

  wxSize ext = title->GetTextExtent(title->GetLabel());
  title->SetMinSize(wxSize(ext.x + 4, ext.y + 8));
}

inline wxColour MutedText(const wxWindow *w) {
  return w->GetForegroundColour().ChangeLightness(150);
}

inline void ResizeListColumns(wxListCtrl *list) {
  if (!list || list->GetColumnCount() == 0)
    return;

  const int w = list->GetClientSize().GetWidth();
  if (w < 20)
    return;

  const int cols = list->GetColumnCount();
  if (cols == 1) {
    list->SetColumnWidth(0, w);
    return;
  }

  const int c0 = w * 55 / 100;
  list->SetColumnWidth(0, c0);
  list->SetColumnWidth(1, w - c0);
}

} // namespace ui

#endif
