#!/usr/bin/env python3
"""Embed admin/src/icons/*.svg into admin/src/Icons.h.

The admin GUI renders its toolbar icons from SVG via wxBitmapBundle::FromSVG,
which keeps them crisp on HiDPI displays. Embedding the sources means the built
binary carries no external asset dependency.

Run from the repository root after adding or changing an icon:

    python3 admin/tools/embed_icons.py
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
ICON_DIR = ROOT / "admin" / "src" / "icons"
OUT = ROOT / "admin" / "src" / "Icons.h"

HEADER = """/*
  ShareFS Server - Admin GUI embedded icons

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

// GENERATED FILE - do not edit by hand.
// Regenerate with: python3 admin/tools/embed_icons.py
//
// Icons are Lucide (https://lucide.dev), MIT licensed, plus a pair of simple
// status discs. Each is stored as its SVG source and rendered at the required
// size at runtime, so they stay sharp on HiDPI displays.

#ifndef SHAREFS_ICONS_H
#define SHAREFS_ICONS_H

namespace icons {

"""

FOOTER = """
} // namespace icons

#endif // SHAREFS_ICONS_H
"""


def ident(name: str) -> str:
    return "k" + "".join(part.capitalize() for part in name.replace("-", "_").split("_"))


def main() -> int:
    if not ICON_DIR.is_dir():
        print(f"icon directory not found: {ICON_DIR}", file=sys.stderr)
        return 1

    svgs = sorted(p for p in ICON_DIR.glob("*.svg"))
    if not svgs:
        print(f"no .svg files in {ICON_DIR}", file=sys.stderr)
        return 1

    parts = [HEADER]
    for path in svgs:
        data = path.read_text(encoding="utf-8").strip()
        if ')SVG"' in data:
            print(f"{path.name}: contains raw-string terminator", file=sys.stderr)
            return 1
        parts.append(f'// {path.name}\n')
        parts.append(f'inline const char *{ident(path.stem)} = R"SVG({data})SVG";\n\n')
    parts.append(FOOTER)

    OUT.write_text("".join(parts), encoding="utf-8")
    print(f"wrote {OUT.relative_to(ROOT)} ({len(svgs)} icons)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
