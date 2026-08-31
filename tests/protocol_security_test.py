#!/usr/bin/env python3
"""Protocol-level regression tests for ShareFS access control.

These drive a running server over UDP the way a RISC OS client would, and
assert the two access-control properties that unit tests cannot reach:

  1. A share marked `protected` cannot be listed without authenticating.
  2. A file handle opened by one client cannot be used by another.

Usage:  protocol_security_test.py <config-file> <server-binary>

The server is started and stopped by this script, so it needs the UDP ports
in sharefs.conf to be free.
"""

import os
import socket
import struct
import subprocess
import sys
import tempfile
import time

HOST = "127.0.0.1"
RPC_PORT = 49171

RFIND = 0x00
ROPENIN = 0x01
ROPENDIR = 0x03
RREAD = 0x0B
RWRITE = 0x0C
RCLOSE = 0x0A


class Client:
    """One UDP endpoint. Distinct instances get distinct source ports, which
    is what makes them distinct clients as far as the server is concerned."""

    def __init__(self, timeout=2.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("127.0.0.1", 0))
        self.sock.settimeout(timeout)

    def send(self, packet):
        self.sock.sendto(packet, (HOST, RPC_PORT))

    def recv(self):
        try:
            return self.sock.recvfrom(65536)[0]
        except socket.timeout:
            return None

    def request(self, packet):
        self.send(packet)
        return self.recv()

    def close(self):
        self.sock.close()


def a_cmd(rid, code, handle, payload=b""):
    return b"A" + rid + struct.pack("<II", code, handle) + payload


def b_cmd(rid, code, handle, extra, payload=b""):
    return b"B" + rid + struct.pack("<III", code, handle, extra) + payload


def lower_a_cmd(rid, code, handle, payload=b""):
    return b"a" + rid + struct.pack("<II", code, handle) + payload


def errno_of(reply):
    """Return the errno from an E packet, or None if it is not an error."""
    if reply and reply[0:1] == b"E":
        return reply[4]
    return None


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


def test_protected_share_needs_auth(results):
    """A share with `protected` must not be catalogued without Access+ auth.

    Regression: the auth check was only wired into the 'A' command branch, so
    the 'B' branch's ROPENDIR listed protected shares to anyone who asked.
    """
    print("\nProtected share must not be listable without authentication")
    client = Client()

    reply = client.request(b_cmd(b"\x01\x00\x01", ROPENDIR, 0, 0, b"Secret\x00"))
    results.check(
        "B ROPENDIR on protected share is refused",
        errno_of(reply) is not None,
        f"expected an E packet, got {reply[0:1]!r} ({len(reply or b'')} bytes)",
    )

    reply = client.request(a_cmd(b"\x01\x00\x02", ROPENDIR, 0, b"Secret\x00"))
    results.check(
        "A ROPENDIR on protected share is refused",
        errno_of(reply) is not None,
        f"expected an E packet, got {reply[0:1]!r}",
    )

    reply = client.request(a_cmd(b"\x01\x00\x03", RFIND, 0, b"Secret\x00"))
    results.check(
        "A RFIND on protected share is refused",
        errno_of(reply) is not None,
        f"expected an E packet, got {reply[0:1]!r}",
    )

    # The unprotected share must still work, or we have simply broken the server.
    reply = client.request(b_cmd(b"\x01\x00\x04", ROPENDIR, 0, 0, b"Public\x00"))
    results.check(
        "B ROPENDIR on a public share still works",
        reply is not None and reply[0:1] == b"S",
        f"expected an S packet, got {reply[0:1]!r}",
    )
    client.close()


def test_handle_is_bound_to_its_client(results):
    """A handle opened by one client must not be usable by another.

    Regression: handlers in the lowercase 'a' branch, and B ROREADDIR, looked
    handles up by id alone. Ids are sequential from 1, so any host on the LAN
    could read or write another client's open file by guessing.
    """
    print("\nHandle opened by one client must be unusable by another")

    owner = Client()
    reply = owner.request(a_cmd(b"\x02\x00\x01", ROPENIN, 0, b"Public.ReadMe\x00"))
    if not reply or reply[0:1] != b"R" or len(reply) < 28:
        results.check("owner could open the file", False, f"got {reply!r}")
        owner.close()
        return

    handle = struct.unpack("<I", reply[24:28])[0]
    print(f"    owner opened handle {handle}")

    attacker = Client()

    reply = attacker.request(
        lower_a_cmd(b"\x02\x00\x02", RREAD, handle, struct.pack("<II", 0, 8))
    )
    results.check(
        "another client cannot read through the handle ('a' branch)",
        reply is None or errno_of(reply) is not None,
        f"expected refusal, got {reply[0:1]!r} — data may have leaked",
    )

    reply = attacker.request(
        lower_a_cmd(b"\x02\x00\x03", RWRITE, handle, struct.pack("<II", 0, 4))
    )
    results.check(
        "another client cannot write through the handle ('a' branch)",
        reply is None or errno_of(reply) is not None,
        f"expected refusal, got {reply[0:1]!r}",
    )

    reply = attacker.request(lower_a_cmd(b"\x02\x00\x04", RCLOSE, handle))
    results.check(
        "another client cannot close the handle ('a' branch)",
        reply is None or errno_of(reply) is not None,
        f"expected refusal, got {reply[0:1]!r}",
    )

    # The rightful owner must still be able to use it after all that.
    reply = owner.request(
        a_cmd(b"\x02\x00\x05", RREAD, handle, struct.pack("<II", 0, 8))
    )
    results.check(
        "the owner can still use its own handle",
        reply is not None and reply[0:1] in (b"D", b"R"),
        f"expected data, got {reply[0:1]!r}",
    )

    owner.close()
    attacker.close()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    config, binary = sys.argv[1], sys.argv[2]

    server = subprocess.Popen(
        [binary, "serve", "--config", config],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(1.0)
    if server.poll() is not None:
        print("server exited immediately; check the config")
        return 2

    results = Results()
    try:
        test_protected_share_needs_auth(results)
        test_handle_is_bound_to_its_client(results)
    finally:
        server.terminate()
        server.wait(timeout=5)

    print(f"\n{results.passes} passed, {len(results.failures)} failed")
    for name in results.failures:
        print(f"  failed: {name}")
    return 1 if results.failures else 0


if __name__ == "__main__":
    sys.exit(main())
