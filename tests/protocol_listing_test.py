#!/usr/bin/env python3
"""Protocol-level regression tests for filename visibility in a catalogue.

A RISC OS name beginning with '/' is stored on the host with a leading '.'
(see src/names.c). The catalogue must therefore list dotfiles, or a file the
client has just written successfully becomes invisible to it - which is what
issue #21 reported: the copy lands on disk, then the Filer cannot see it and
reports the copy as failed.

This is a protocol test rather than a unit test because the fault was in the
directory enumeration, which only exists behind ROPENDIR.

Usage:  protocol_listing_test.py <config-file> <server-binary>
"""

import re
import socket
import struct
import subprocess
import sys
import time

HOST = "127.0.0.1"
RPC_PORT = 49171

RFIND = 0x00
ROPENIN = 0x01
ROPENDIR = 0x03


class Client:
    def __init__(self, timeout=2.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(timeout)

    def request(self, packet):
        self.sock.sendto(packet, (HOST, RPC_PORT))
        try:
            return self.sock.recvfrom(65536)[0]
        except socket.timeout:
            return None

    def drain(self):
        """Collect any further packets, e.g. extra catalogue pages."""
        out = []
        while True:
            try:
                out.append(self.sock.recvfrom(65536)[0])
            except socket.timeout:
                return out

    def close(self):
        self.sock.close()


def a_cmd(rid, code, handle, payload=b""):
    return b"A" + rid + struct.pack("<II", code, handle) + payload


def b_cmd(rid, code, handle, extra, payload=b""):
    return b"B" + rid + struct.pack("<III", code, handle, extra) + payload


class Results:
    def __init__(self):
        self.failures = []
        self.passes = 0

    def check(self, name, condition, detail=""):
        if condition:
            self.passes += 1
            print(f"  PASS  {name}")
        else:
            self.failures.append(name)
            print(f"  FAIL  {name}" + (f"\n          {detail}" if detail else ""))


def catalogue_names(client, share):
    """Return the printable names in a share's catalogue pages."""
    reply = client.request(b_cmd(b"\x03\x00\x01", ROPENDIR, 0, 0, share + b"\x00"))
    blob = (reply or b"") + b"".join(client.drain())
    return [m.group().decode("latin-1") for m in re.finditer(rb"[ -~]{3,}", blob)]


def test_leading_slash_names_are_listed(results):
    """A RISC OS name starting with '/' must appear in the catalogue.

    Regression: enumeration skipped every entry whose host name began with a
    '.', to get rid of '.' and '..'. That also discarded every RISC OS name
    beginning with '/', so files written to the share could not be seen again.
    """
    print("\nNames beginning with '/' must be visible in the catalogue")
    client = Client()
    names = catalogue_names(client, b"Public")
    print(f"    catalogue: {names}")

    # The name the reporter used, and a directory of the same shape.
    results.check(
        "a file named '/gitattributes' is listed",
        "/gitattributes" in names,
        f"got {names}",
    )
    results.check(
        "a directory named '/git' is listed",
        "/git" in names,
        f"got {names}",
    )

    # Cover the case that was never broken, so a fix that hides ordinary files
    # to make the dotfiles work does not pass.
    results.check(
        "an ordinary name is still listed",
        "ReadMe" in names,
        f"got {names}",
    )

    # '.' and '..' are the host's own entries and have no RISC OS meaning.
    # Decoded they would surface as '/' and '//'.
    results.check(
        "the directory's own '.' and '..' entries are not listed",
        "/" not in names and "//" not in names,
        f"got {names}",
    )
    client.close()


def test_leading_slash_names_are_reachable(results):
    """Looking one up by name must work as well as listing it.

    This half was never broken, and it is why the reporter saw the file arrive
    on disk. It is checked so that the two paths cannot drift apart.
    """
    print("\nNames beginning with '/' must be reachable by path")
    client = Client()

    reply = client.request(a_cmd(b"\x03\x00\x02", RFIND, 0, b"Public./gitattributes\x00"))
    results.check(
        "RFIND finds '/gitattributes'",
        reply is not None and reply[0:1] == b"R",
        f"got {reply[0:1]!r}",
    )

    reply = client.request(a_cmd(b"\x03\x00\x03", ROPENIN, 0, b"Public./gitattributes\x00"))
    results.check(
        "ROPENIN opens '/gitattributes'",
        reply is not None and reply[0:1] == b"R",
        f"got {reply[0:1]!r}",
    )

    reply = client.request(a_cmd(b"\x03\x00\x04", ROPENDIR, 0, b"Public./git\x00"))
    results.check(
        "ROPENDIR opens the '/git' directory",
        reply is not None and reply[0:1] != b"E",
        f"got {reply[0:1]!r}",
    )
    client.close()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    config, binary = sys.argv[1], sys.argv[2]

    server = subprocess.Popen(
        [binary, config], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    time.sleep(1.0)
    if server.poll() is not None:
        print("server exited immediately; check the config")
        return 2

    results = Results()
    try:
        test_leading_slash_names_are_listed(results)
        test_leading_slash_names_are_reachable(results)
    finally:
        server.terminate()
        server.wait(timeout=5)

    print(f"\n{results.passes} passed, {len(results.failures)} failed")
    for name in results.failures:
        print(f"  failed: {name}")
    return 1 if results.failures else 0


if __name__ == "__main__":
    sys.exit(main())
