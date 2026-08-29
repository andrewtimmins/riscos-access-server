/*
  ShareFS Server - Wire-format file length tests

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

#include "../src/riscos.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

static void expect(int64_t size, uint32_t want, const char *what) {
    uint32_t got = sfs_length_for_wire(size);
    if (got != want) {
        fprintf(stderr, "FAIL %s: sfs_length_for_wire(%" PRId64
                        ") gave 0x%08" PRIX32 ", want 0x%08" PRIX32 "\n",
                what, size, got, want);
        assert(0);
    }
}

// An ordinary file must report its own size. This is the case issue #22 broke:
// every file on the Windows build claimed to be 4G.
static void test_ordinary_sizes(void) {
    expect(0, 0u, "an empty file");
    expect(1, 1u, "a one-byte file");
    expect(7500, 7500u, "the 7.5K file from the report");
    expect(1024 * 1024, 1048576u, "a one-megabyte file");
}

// The cap exists because the protocol length field is 32 bits.
static void test_cap_at_four_gigabytes(void) {
    expect(0xFFFFFFFELL, 0xFFFFFFFEu, "one below the cap");
    expect(0xFFFFFFFFLL, 0xFFFFFFFFu, "exactly the cap");
    expect(0x100000000LL, 0xFFFFFFFFu, "one above the cap");
    expect(0x123456789LL, 0xFFFFFFFFu, "well above the cap");
}

// Only reachable where off_t is 32 bits and a file exceeds 2GB, but a size
// that arrives negative must not become a huge length.
static void test_negative_size(void) {
    expect(-1, 0u, "a size of -1");
    expect(-2147483648LL, 0u, "a size that overflowed a 32-bit off_t");
}

// The mechanism behind issue #22, demonstrated on any host.
//
// The old code compared against (off_t)0xFFFFFFFF. Where off_t is 32 bits -
// Windows, where long is 32-bit under LLP64 - that constant truncates to -1,
// so every non-negative size compared greater and took the cap branch. This
// asserts the truncation directly rather than trusting the description of it,
// and then that the helper is immune because its parameter is fixed-width.
static void test_the_windows_truncation(void) {
    int32_t narrow_cap = (int32_t)0xFFFFFFFF;
    if (narrow_cap != -1) {
        fprintf(stderr, "FAIL: a 32-bit (off_t)0xFFFFFFFF is %" PRId32
                        ", expected -1\n", narrow_cap);
        assert(0);
    }

    // The old expression, with the narrow limit the Windows build gave it.
    int32_t small_file = 7500;
    uint32_t old_result = small_file > narrow_cap ? 0xFFFFFFFFu
                                                  : (uint32_t)small_file;
    if (old_result != 0xFFFFFFFFu) {
        fprintf(stderr, "FAIL: the old expression was expected to report 4G "
                        "for a small file, gave 0x%08" PRIX32 "\n", old_result);
        assert(0);
    }

    // The helper takes int64_t, so the same file is reported correctly no
    // matter how wide the caller's off_t is.
    expect(small_file, 7500u, "a small file where off_t is 32 bits");
}

int main(void) {
    test_ordinary_sizes();
    test_cap_at_four_gigabytes();
    test_negative_size();
    test_the_windows_truncation();

    printf("All tests passed.\n");
    return 0;
}
