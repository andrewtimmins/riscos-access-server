/*
  ShareFS - About dialog

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

// Laid out to match the About window in RPCEmu Extended: logo and title block,
// a rule, the tagline, a scrolling credits list, then licence and build detail.

#include "AboutDialog.h"

#include "Icons.h"
#include "UiHelpers.h"

#include <algorithm>

#include <wx/artprov.h>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>
#include <wx/statline.h>
#include <wx/tokenzr.h>
#include <wx/wx.h>

#ifndef SHAREFS_VERSION
#define SHAREFS_VERSION "0.0.0"
#endif

namespace {

// The application mark: a disc platter over a network-blue rounded field.
// Drawn rather than shipped so the dialog has no external asset dependency.
wxBitmap AboutLogo(int size) {
  wxBitmap bitmap(size, size, 24);

  wxMemoryDC dc(bitmap);
  wxGCDC gc(dc);

  const wxColour field(0x2C, 0x5F, 0x8A);
  const wxColour platter(0xF0, 0xE8, 0xD0);

  // The mark fills the whole square rather than floating on a transparent
  // background: a transparent clear does not reliably produce an alpha channel
  // across platforms, which leaves the logo sitting on a black tile.
  gc.SetPen(*wxTRANSPARENT_PEN);
  gc.SetBrush(wxBrush(field));
  gc.DrawRectangle(0, 0, size, size);

  // Disc platter, for the shared volume a client sees.
  const int cx = size / 2;
  const int cy = size / 2;
  gc.SetBrush(wxBrush(platter));
  gc.DrawCircle(cx, cy, size / 3);
  gc.SetBrush(wxBrush(field));
  gc.DrawCircle(cx, cy, std::max(2, size / 10));

  dc.SelectObject(wxNullBitmap);
  if (bitmap.IsOk())
    return bitmap;

  return wxArtProvider::GetBitmap(wxART_INFORMATION, wxART_OTHER,
                                  wxSize(size, size));
}

wxString BuildDescription() {
#if defined(__WXMSW__)
  const wxString platform = "Windows";
#elif defined(__WXOSX__)
  const wxString platform = "macOS";
#else
  const wxString platform = "Linux";
#endif
  return platform +
         wxString::Format(" build, %ld-bit",
                          static_cast<long>(sizeof(void *) * 8));
}

/*
 * Who made what.
 *
 * Named individually rather than as "contributors", because a blanket phrase
 * credits nobody. The acknowledgements below are drawn from the project's own
 * commit history and from the licences of the code and assets this build
 * carries; the last group is a licence condition rather than a courtesy.
 */
struct CreditGroup {
  const char *heading;
  const char *entries;
};

const CreditGroup kCredits[] = {
    {"ShareFS Server", "Andy Timmins"},
    {"Contributed to ShareFS Server",
     "David Pitt: include fixes for building on RISC OS-adjacent toolchains\n"
     "Charles Ferguson: guidance on Obey file handling and filetype "
     "behaviour"},
    {"Assets this build carries, with thanks",
     "Lucide (https://lucide.dev): the toolbar icons, MIT licensed"},
    {"With acknowledgement to",
     "Acorn Computers Ltd: the ShareFS and Access+ protocols this server "
     "implements\n"
     "wxWidgets: the cross-platform toolkit this interface is built on"},
};

} // namespace

AboutDialog::AboutDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, "About ShareFS", wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxCLOSE_BOX) {
  BuildUi();
  Fit();
  SetMinSize(GetSize());
  CentreOnParent();
}

void AboutDialog::BuildUi() {
  const int year = wxDateTime::Now().GetYear();
  const wxColour muted = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);

  wxFont titleFont = GetFont();
  titleFont.SetPointSize(titleFont.GetPointSize() + 6);
  titleFont.SetWeight(wxFONTWEIGHT_BOLD);

  wxFont editionFont = GetFont();
  editionFont.SetPointSize(editionFont.GetPointSize() + 1);

  wxFont smallFont = GetFont();
  smallFont.SetPointSize(std::max(8, smallFont.GetPointSize() - 1));

  auto *icon = new wxStaticBitmap(
      this, wxID_ANY, AboutLogo(FromDIP(64)));

  auto *title = new wxStaticText(this, wxID_ANY, "ShareFS");
  title->SetFont(titleFont);

  // There is one product, so no edition line: it used to say "Admin", which
  // implied a separate program from the server rather than the same one.
  auto *edition = new wxStaticText(this, wxID_ANY, "File sharing for RISC OS");
  edition->SetFont(editionFont);
  edition->SetForegroundColour(muted);

  auto *version =
      new wxStaticText(this, wxID_ANY, "Version " + wxString(SHAREFS_VERSION));

  auto *titleBlock = new wxBoxSizer(wxVERTICAL);
  titleBlock->Add(title, 0, wxBOTTOM, 2);
  titleBlock->Add(edition, 0, wxBOTTOM, 4);
  titleBlock->Add(version, 0);

  auto *header = new wxBoxSizer(wxHORIZONTAL);
  header->Add(icon, 0, wxALIGN_CENTRE_VERTICAL | wxRIGHT, 16);
  header->Add(titleBlock, 1, wxALIGN_CENTRE_VERTICAL);

  auto *tagline = new wxStaticText(
      this, wxID_ANY,
      "An Acorn ShareFS-compatible file server for RISC OS machines");

  auto *creditsIntro = new wxStaticText(this, wxID_ANY, "Brought to you by:");

  auto *license = new wxStaticText(
      this, wxID_ANY,
      wxString::Format("Copyright 2025-%d Andy Timmins. ", year) +
          "This program is free software, licensed under the GNU General "
          "Public License, version 3. See the LICENSE file for details.");
  license->Wrap(FromDIP(420));
  license->SetForegroundColour(muted);

  auto *buildInfo = new wxStaticText(this, wxID_ANY, BuildDescription());
  buildInfo->SetFont(smallFont);
  buildInfo->SetForegroundColour(muted);

  // Sized from the font rather than hard-coded pixels, so a larger system font
  // or a HiDPI screen still gets a pane that fits its own contents. A part-shown
  // row at the bottom is deliberate: it signals there is more to scroll to.
  const int lineHeight = GetCharHeight();
  const int rowPadding = FromDIP(10);
  const int rowPitch = lineHeight + rowPadding;
  const int indent = FromDIP(16);
  const int scrollbar = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, this);
  const int creditsWidth =
      GetTextExtent("Charles Ferguson: guidance on Obey file handling and")
          .GetWidth() +
      indent + scrollbar + FromDIP(24);
  const int creditsHeight = rowPitch * 8 + rowPadding;
  const int wrapWidth = creditsWidth - indent - scrollbar - FromDIP(20);

  auto *credits =
      new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                           wxSize(creditsWidth, creditsHeight),
                           wxVSCROLL | wxBORDER_THEME);
  credits->SetBackgroundColour(
      wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

  wxFont headingFont = GetFont();
  headingFont.SetWeight(wxFONTWEIGHT_BOLD);

  auto *creditsSizer = new wxBoxSizer(wxVERTICAL);
  bool first = true;

  for (const CreditGroup &group : kCredits) {
    auto *heading = new wxStaticText(credits, wxID_ANY, group.heading);
    heading->SetFont(headingFont);
    creditsSizer->AddSpacer(first ? 10 : 14);
    creditsSizer->Add(heading, 0, wxLEFT | wxRIGHT, 10);

    for (const wxString &entry :
         wxSplit(wxString::FromUTF8(group.entries), '\n')) {
      auto *text = new wxStaticText(credits, wxID_ANY, entry);
      auto *row = new wxBoxSizer(wxHORIZONTAL);

      text->Wrap(wrapWidth);
      row->AddSpacer(indent);
      row->Add(text, 1);
      creditsSizer->Add(row, 0, wxRIGHT | wxTOP, rowPadding);
    }
    first = false;
  }
  creditsSizer->AddSpacer(10);

  credits->SetSizer(creditsSizer);
  // A line at a time, so scrolling lands on whole lines.
  credits->SetScrollRate(0, lineHeight);
  // Derive the virtual size from the contents, or the scrollbar never appears
  // and the entries past the bottom cannot be reached.
  credits->FitInside();

  auto *buttons = CreateStdDialogButtonSizer(wxOK);

  auto *main = new wxBoxSizer(wxVERTICAL);
  main->Add(header, 0, wxEXPAND | wxALL, 16);
  main->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 16);
  main->Add(tagline, 0, wxLEFT | wxRIGHT | wxTOP, 16);
  main->Add(creditsIntro, 0, wxLEFT | wxRIGHT | wxTOP, 16);
  main->Add(credits, 1, wxEXPAND | wxALL, 16);
  main->Add(license, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
  main->Add(buildInfo, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);
  main->Add(buttons, 0, wxEXPAND | wxALL, 10);
  SetSizer(main);
}
