/*
  ShareFS - layout helpers

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

// Small shared vocabulary for the admin panels: consistent spacing, semantic
// colours that follow the system light/dark appearance, section headers,
// inline field validation and a status indicator widget.

#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include <wx/bmpbndl.h>
#include <wx/control.h>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/settings.h>
#include <wx/wx.h>

#include <vector>

namespace ui {

// ---------------------------------------------------------------------------
// Spacing scale. Used everywhere instead of ad-hoc magic numbers so the panels
// line up with each other.
// ---------------------------------------------------------------------------
enum {
  kPagePad = 16, // outer margin around a panel's contents
  kGroupGap = 14, // between major groups
  kRowGap = 9,   // between rows in a form
  kLabelGap = 12, // between a label and its field
  kTightGap = 6  // between closely related controls
};

// ---------------------------------------------------------------------------
// Semantic colours. Each returns a shade picked for the current appearance so
// text stays legible in both light and dark mode.
// ---------------------------------------------------------------------------
inline bool IsDark() {
#if wxCHECK_VERSION(3, 2, 0)
  return wxSystemSettings::GetAppearance().IsDark();
#else
  wxColour bg = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
  return (bg.Red() + bg.Green() + bg.Blue()) < 384;
#endif
}

inline wxColour Success() {
  return IsDark() ? wxColour(0x4C, 0xD9, 0x64) : wxColour(0x1D, 0x8A, 0x3A);
}

inline wxColour Danger() {
  return IsDark() ? wxColour(0xFF, 0x6B, 0x63) : wxColour(0xC0, 0x2A, 0x22);
}

inline wxColour Warning() {
  return IsDark() ? wxColour(0xFF, 0xB1, 0x3C) : wxColour(0xA6, 0x63, 0x00);
}

inline wxColour Neutral() {
  return IsDark() ? wxColour(0x8E, 0x8E, 0x93) : wxColour(0x8A, 0x8A, 0x8F);
}

// Server state colours, matching the bundled running/stopped status discs so
// the toolbar indicator and the Control tab agree.
inline wxColour StatusRunning() { return wxColour(0x78, 0xB1, 0x59); }
inline wxColour StatusStopped() { return wxColour(0xDD, 0x2E, 0x44); }

// Muted body text, derived from the parent's own foreground so it tracks the
// platform theme rather than being hard-coded.
inline wxColour MutedText(const wxWindow *w) {
  return w->GetForegroundColour().ChangeLightness(IsDark() ? 70 : 150);
}

// A faint surface tint for grouping panels, again derived from the theme.
inline wxColour SurfaceTint(const wxWindow *w) {
  wxColour base = w->GetBackgroundColour();
  return base.ChangeLightness(IsDark() ? 115 : 97);
}

// ---------------------------------------------------------------------------
// Icons. The embedded SVG sources (see Icons.h) all carry the same placeholder
// stroke colour; swapping it for a theme colour at render time keeps the
// toolbar looking native in both light and dark mode, and rendering from SVG
// keeps the result crisp on HiDPI displays.
// ---------------------------------------------------------------------------
inline wxColour IconTint() {
  return wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

inline wxBitmapBundle Icon(const char *svg, int size,
                           const wxColour &tint = wxNullColour) {
  wxString data = wxString::FromUTF8(svg);
  if (tint.IsOk())
    data.Replace("#00A100", tint.GetAsString(wxC2S_HTML_SYNTAX));

  const wxScopedCharBuffer utf8 = data.ToUTF8();
  return wxBitmapBundle::FromSVG(utf8.data(), wxSize(size, size));
}

// ---------------------------------------------------------------------------
// Typography
// ---------------------------------------------------------------------------
inline void StyleSectionTitle(wxStaticText *title) {
  wxFont f = title->GetFont();
  if (f.GetPointSize() > 0)
    f.SetPointSize(f.GetPointSize() + 3);
  else
    f.SetPointSize(13);
  f.SetWeight(wxFONTWEIGHT_BOLD);
  title->SetFont(f);

  wxSize ext = title->GetTextExtent(title->GetLabel());
  title->SetMinSize(wxSize(ext.x + 4, ext.y + 8));
}

inline void StyleFieldLabel(wxStaticText *label) {
  label->SetForegroundColour(MutedText(label));
}

// ---------------------------------------------------------------------------
// List columns: distribute the available width across columns by weight.
// Passing no weights spreads the columns evenly.
// ---------------------------------------------------------------------------
inline void ResizeListColumns(wxListCtrl *list,
                              const std::vector<int> &weights = {}) {
  if (!list || list->GetColumnCount() == 0)
    return;

  const int cols = list->GetColumnCount();
  int avail = list->GetClientSize().GetWidth();
  if (avail < 40)
    return;

  // Leave room for a scrollbar so the last column is not clipped.
  avail -= 4;

  std::vector<int> w = weights;
  w.resize((size_t)cols, 1);
  int total = 0;
  for (int i = 0; i < cols; ++i) {
    if (w[(size_t)i] < 1)
      w[(size_t)i] = 1;
    total += w[(size_t)i];
  }
  if (total <= 0)
    return;

  int used = 0;
  for (int i = 0; i < cols - 1; ++i) {
    int cw = avail * w[(size_t)i] / total;
    list->SetColumnWidth(i, cw);
    used += cw;
  }
  list->SetColumnWidth(cols - 1, avail - used);
}

// ---------------------------------------------------------------------------
// Inline validation. Marks a field as problematic and shows the reason next to
// it, rather than failing silently or waiting for the server to complain.
// ---------------------------------------------------------------------------
inline void SetFieldError(wxTextCtrl *field, wxStaticText *hint, bool bad,
                          const wxString &message) {
  if (field) {
    // Tint the text itself; background tinting is unreliable across themes.
    field->SetForegroundColour(bad ? Danger()
                                   : wxSystemSettings::GetColour(
                                         wxSYS_COLOUR_WINDOWTEXT));
    field->Refresh();
  }
  if (hint) {
    hint->SetLabel(bad ? message : wxString());
    hint->SetForegroundColour(Warning());
    hint->Show(bad);
  }
}

// ---------------------------------------------------------------------------
// Plain modal prompts.
//
// wxMessageBox is not used here: on macOS it maps to NSAlert, which always
// draws the application icon whether or not a wxICON_* style is given, leaving
// an icon well that says nothing. These are the same dialogs without it.
// ---------------------------------------------------------------------------
inline int ShowPrompt(wxWindow *parent, const wxString &title,
                      const wxString &message, long buttons) {
  wxDialog dlg(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE);

  auto *text = new wxStaticText(&dlg, wxID_ANY, message);
  text->Wrap(dlg.FromDIP(360));

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(text, 1, wxEXPAND | wxALL, kPagePad + 4);

  if (wxSizer *buttonSizer = dlg.CreateStdDialogButtonSizer(buttons))
    sizer->Add(buttonSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,
               kPagePad - 4);

  dlg.SetSizerAndFit(sizer);
  dlg.CentreOnParent();
  return dlg.ShowModal();
}

// Yes/No question. Returns true for Yes.
inline bool Ask(wxWindow *parent, const wxString &title,
                const wxString &message) {
  return ShowPrompt(parent, title, message, wxYES_NO) == wxID_YES;
}

// Acknowledgement only.
inline void Notify(wxWindow *parent, const wxString &title,
                   const wxString &message) {
  ShowPrompt(parent, title, message, wxOK);
}

// ---------------------------------------------------------------------------
// StatusDot: a filled circle plus a label, used for server state.
// ---------------------------------------------------------------------------
// Derives from wxControl so it can be dropped straight into a wxToolBar.
class StatusDot : public wxControl {
public:
  StatusDot(wxWindow *parent, const wxString &label = wxString())
      : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                  wxBORDER_NONE),
        m_label(label), m_colour(Neutral()) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Bind(wxEVT_PAINT, &StatusDot::OnPaint, this);
    Recalc();
  }

  void Set(const wxString &label, const wxColour &colour) {
    m_label = label;
    m_colour = colour;
    Recalc();
    Refresh();
  }

  // Draw the dot and label flush with the right-hand edge of whatever width
  // the control is given, rather than at its left. Used in the status bar,
  // where the control spans a whole field but should sit against the window
  // edge instead of floating at the start of the field.
  void SetAlignRight(bool alignRight) {
    m_alignRight = alignRight;
    Refresh();
  }

private:
  int ContentWidth() const {
    return kDotSize + kTightGap +
           GetTextExtent(m_label.IsEmpty() ? "Xg" : m_label).x;
  }

  void Recalc() {
    // Size to the longest label the control ever shows, so that changing state
    // does not reflow the layout and the text is never clipped.
    const wxSize widest = GetTextExtent("Running (System Service)");
    const wxSize current = GetTextExtent(m_label.IsEmpty() ? "Xg" : m_label);

    SetMinSize(wxSize(kDotSize + kTightGap + wxMax(widest.x, current.x) + 4,
                      wxMax(current.y, widest.y) + 6));
    InvalidateBestSize();
  }

  void OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    dc.SetFont(GetFont());
    const wxSize sz = GetClientSize();
    const int cy = sz.y / 2;
    const int x = m_alignRight ? wxMax(0, sz.x - ContentWidth()) : 0;

    dc.SetBrush(wxBrush(m_colour));
    dc.SetPen(wxPen(m_colour));
    dc.DrawCircle(x + kDotSize / 2 + 1, cy, kDotSize / 2);

    dc.SetTextForeground(GetForegroundColour());
    const wxSize ext = dc.GetTextExtent(m_label);
    dc.DrawText(m_label, x + kDotSize + kTightGap + 1, cy - ext.y / 2);
  }

  static const int kDotSize = 10;

  wxString m_label;
  wxColour m_colour;
  bool m_alignRight = false;
};

} // namespace ui

#endif
