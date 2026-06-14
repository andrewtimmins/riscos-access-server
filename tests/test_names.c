// ShareFS Server - Unit tests for names.c
// Build: included via tests/CMakeLists.txt

#include "../src/names.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

static void encode_expect(const char *input, const char *want) {
    char out[1024];
    int rc = sfs_encode_host_name(input, out, sizeof(out));
    if (rc != 0 || strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL encode(%s): got '%s' want '%s' rc=%d\n",
                input, rc == 0 ? out : "<error>", want, rc);
        assert(0);
    }
}

static void decode_expect(const char *input, const char *want) {
    char out[1024];
    int rc = sfs_decode_host_name(input, out, sizeof(out));
    if (rc != 0 || strcmp(out, want) != 0) {
        fprintf(stderr, "FAIL decode(%s): got '%s' want '%s' rc=%d\n",
                input, rc == 0 ? out : "<error>", want, rc);
        assert(0);
    }
}

// Round-trip: encode then decode must give back the original.
static void roundtrip(const char *ros_name) {
    char encoded[1024];
    char decoded[1024];
    int rc1 = sfs_encode_host_name(ros_name, encoded, sizeof(encoded));
    if (rc1 != 0) {
        fprintf(stderr, "FAIL roundtrip encode(%s)\n", ros_name);
        assert(0);
    }
    int rc2 = sfs_decode_host_name(encoded, decoded, sizeof(decoded));
    if (rc2 != 0 || strcmp(decoded, ros_name) != 0) {
        fprintf(stderr, "FAIL roundtrip(%s): encoded='%s' decoded='%s'\n",
                ros_name, encoded, decoded);
        assert(0);
    }
}

// ---------------------------------------------------------------------------
// Encode tests
// ---------------------------------------------------------------------------

static void test_encode_passthrough(void) {
    encode_expect("hello",          "hello");
    encode_expect("HelloWorld",     "HelloWorld");
    encode_expect("file123",        "file123");
    encode_expect("",               "");
    encode_expect("!Boot",          "!Boot");
    encode_expect("Report_March",   "Report_March");
}

static void test_encode_slash_to_dot(void) {
    // RISC OS '/' in a filename -> '.' on the host
    encode_expect("a/b",            "a.b");
    encode_expect("Report/March",   "Report.March");
    encode_expect("a/b/c",         "a.b.c");
}

static void test_encode_space_to_nbsp(void) {
    encode_expect("hello world",    "hello\xa0world");
    encode_expect(" leading",       "\xa0leading");
    encode_expect("trailing ",      "trailing\xa0");
}

static void test_encode_percent(void) {
    encode_expect("%",              "%25");
    encode_expect("100%done",       "100%25done");
    encode_expect("%2F",            "%252F");
}

static void test_encode_ntfs_forbidden(void) {
    encode_expect("file<name",      "file%3Cname");
    encode_expect("file>name",      "file%3Ename");
    encode_expect("file:name",      "file%3Aname");
    encode_expect("file\"name",     "file%22name");
    encode_expect("file|name",      "file%7Cname");
    encode_expect("file?name",      "file%3Fname");
    encode_expect("file*name",      "file%2Aname");
    encode_expect("file\\name",     "file%5Cname");
}

static void test_encode_control_chars(void) {
    char in[4] = {0x01, 'x', 0x1F, '\0'};
    char out[32];
    assert(sfs_encode_host_name(in, out, sizeof(out)) == 0);
    // 0x01 -> %01, 0x1F -> %1F
    assert(strcmp(out, "%01x%1F") == 0);
}

static void test_encode_comma_suffix_clash(void) {
    // Comma followed by exactly 3 hex digits at end -> escaped
    encode_expect("data,fff",       "data%2Cfff");
    encode_expect("data,FFF",       "data%2CFFF");
    encode_expect("data,abc",       "data%2Cabc");
    encode_expect("data,DEF",       "data%2CDEF");

    // Comma NOT at that position -> passes through
    encode_expect("a,bc",          "a,bc");         // only 2 hex after comma
    encode_expect("a,bcde",        "a,bcde");        // 4 hex after comma (not 3)
    encode_expect(",fff",           "%2Cfff");        // name IS the suffix pattern
    encode_expect("comma,here",    "comma,here");    // 'here' not all hex
    encode_expect("comma,zzz",     "comma,zzz");     // 'zzz' not hex
}

static void test_encode_combined(void) {
    encode_expect("Test<File>",     "Test%3CFile%3E");
    encode_expect("weird:name",     "weird%3Aname");
    encode_expect("pipe|name",      "pipe%7Cname");
    encode_expect("slash/dot",      "slash.dot");
}

// ---------------------------------------------------------------------------
// Decode tests
// ---------------------------------------------------------------------------

static void test_decode_passthrough(void) {
    decode_expect("hello",          "hello");
    decode_expect("",               "");
    decode_expect("abc123",         "abc123");
}

static void test_decode_dot_to_slash(void) {
    decode_expect("a.b",            "a/b");
    decode_expect("Report.March",   "Report/March");
    decode_expect("a.b.c",         "a/b/c");
}

static void test_decode_nbsp_to_space(void) {
    decode_expect("hello\xa0world", "hello world");
    decode_expect("\xa0leading",    " leading");
}

static void test_decode_percent(void) {
    decode_expect("%25",            "%");
    decode_expect("%3C",            "<");
    decode_expect("%3E",            ">");
    decode_expect("%3A",            ":");
    decode_expect("%22",            "\"");
    decode_expect("%7C",            "|");
    decode_expect("%3F",            "?");
    decode_expect("%2A",            "*");
    decode_expect("%5C",            "\\");
    decode_expect("%2C",            ",");
    // Lowercase hex
    decode_expect("%3c",            "<");
    decode_expect("%2f",            "/");
}

static void test_decode_invalid_percent(void) {
    // Malformed %XX (non-hex digits) -> passed through literally
    decode_expect("%ZZ",            "%ZZ");
    decode_expect("%GG",            "%GG");
    // Short at end -> passed through
    decode_expect("%2",             "%2");
    decode_expect("%",              "%");
}

// ---------------------------------------------------------------------------
// Round-trip tests
// ---------------------------------------------------------------------------

static void test_roundtrips(void) {
    roundtrip("hello");
    roundtrip("");
    roundtrip("Report/March");
    roundtrip("Test<File>");
    roundtrip("weird:name");
    roundtrip("pipe|name");
    roundtrip("hello world");
    roundtrip("100%done");
    roundtrip("data,fff");      // comma-suffix case
    roundtrip(",fff");          // whole name is the suffix pattern
    roundtrip("comma,here");    // non-hex after comma
    roundtrip("a/b/c");
    roundtrip("file\\name");
    // High-bit Latin-1 pass-through
    roundtrip("caf\xE9");       // Latin-1 'é'
    roundtrip("\x80\xFF");
}

// ---------------------------------------------------------------------------
// Buffer-too-small tests
// ---------------------------------------------------------------------------

static void test_buffer_too_small(void) {
    char small[4];
    char big[32];

    // "hello" needs 6 bytes (5 + NUL); buf of 4 should fail
    assert(sfs_encode_host_name("hello", small, 4) == -1);
    assert(sfs_encode_host_name("hello", big, 6) == 0);
    assert(strcmp(big, "hello") == 0);

    // Decode: ">>" needs 3 bytes (2 + NUL) decoded from "%3E%3E"
    assert(sfs_decode_host_name("%3E%3E", small, 2) == -1);
    assert(sfs_decode_host_name("%3E%3E", big, 3) == 0);
    assert(strcmp(big, ">>") == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void) {
    test_encode_passthrough();
    test_encode_slash_to_dot();
    test_encode_space_to_nbsp();
    test_encode_percent();
    test_encode_ntfs_forbidden();
    test_encode_control_chars();
    test_encode_comma_suffix_clash();
    test_encode_combined();

    test_decode_passthrough();
    test_decode_dot_to_slash();
    test_decode_nbsp_to_space();
    test_decode_percent();
    test_decode_invalid_percent();

    test_roundtrips();

    test_buffer_too_small();

    printf("All tests passed.\n");
    return 0;
}
