# RISC OS Access / ShareFS Protocol — Implementer’s Guide

This document is a low-level, implement-from-scratch reference. It defines field sizes, byte order, packet layouts, state machines, name/filetype rules, error semantics, and shows concrete packet examples (hex) for common flows.

## 1) Transport, Ports, and Notation
- Transport: UDP; each datagram is one logical message (no streaming).
- Endianness: all multibyte integers are little-endian.
- Ports:
  - 32770: Freeway broadcast (host/share/printer beacons).
  - 32771: Access+ authentication / secure discovery.
  - 49171: File RPC (all commands below).
- Strings: zero-terminated unless stated otherwise.
- Default chunk size: 1024 bytes for reads and writes.
- RID: 3-byte request ID echoed in all replies for that transaction.

## 2) Time, Filetype, Load/Exec
- RISC OS timestamp is centiseconds since 1900-01-01.
  - Convert Unix seconds $t$ via $cs = (t + 2{,}208{,}988{,}800) \times 100$.
- Load/Exec words for files:
  - `load = 0xFFFtttdd` where `ttt` = 12-bit filetype, `dd` = high byte of `cs`.
  - `exec = low32(cs)`.
- Filetype resolution precedence:
  1) On-disk suffix `,xxx` (comma + 3 hex) if present.
  2) Mimemap/extension table.
  3) Defaults: data 0xFFD, text 0xFFF, dir 0x1000.
- When setting filetype (RSETINFO), rename may add/adjust `,xxx`. Renames preserve an existing suffix if the client omits it.

## 3) Path Mapping and Safety
- Separator: `.` (dot). First component = share name; remaining components are path segments.
- Inside a filename, RISC OS `/` → `.` on disk (prevents unintended subdirectories).
- Space → NBSP (0xa0) on disk (preserves semantics).
- Safety: reject absolute paths and `..` after mapping.
- Example mapping:
  - RISC OS `Docs.Manuals/Net` → host `/<share-root>/Docs/Manuals.Net`
  - `!Run` remains `!Run` (no slash mapping applied because no `/`).

## 4) Packet Type Bytes
| Byte | Direction | Purpose |
|------|-----------|---------|
| `A`  | C→S       | Main RPC request (code inside payload) |
| `B`  | C↔S       | RPC helper for dir open; also Freeway broadcast frame on 32770 |
| `D`  | S→C       | Read data; also rename new-name payload (uppercase variant) |
| `d`  | C→S       | Write data; also rename new-name payload (lowercase variant) |
| `S`  | S→C       | Catalogue payload paired with `B` responses |
| `R`  | S→C       | Success reply |
| `E`  | S→C       | Error reply; errno u32 le |
| `w`  | S→C       | Request for byte range (writes, rename name-fetch) |
| `r`  | C→S       | Ack for read data (RREAD ping-pong) |

## 5) Generic RPC Framing (Port 49171)
- Common header: `cmd(1) | rid[3]`.
- A-command layout: `A | rid[3] | code(u32) | handle(u32) | payload...`
  - Path-based codes (0x00–0x09, 0x16): payload is zero-terminated RISC OS path starting at offset 12.
  - Handle/binary codes (reads/writes/setinfo): the 8 bytes after `code` carry handle + command-specific fields.
- B-command layout mirrors A but uses leading `B`.
- Replies:
  - Success: `R | rid[3] | payload...`
  - Error:   `E | rid[3] | errnum(u32 le) | message(NUL-terminated)` — total `9 + strlen(message)`

### FileDesc Structure (20 bytes, le)
`type(u32) | load(u32) | exec(u32) | length(u32) | attrs(u32)`
- `type`: 1=file, 2=dir
- `load/exec`: as per section 2
- `length`: file length (undefined for dirs)
- `attrs`: RISC OS bits (R,W,L,r,w)

## 6) Command Reference (Payload and Reply)

| Code | Name       | Request Payload (after handle field)                              | Reply Payload                           |
|------|------------|--------------------------------------------------------------------|-----------------------------------------|
| 0x00 | RFIND      | path (z)                                                           | FileDesc                                |
| 0x01 | ROPENIN    | path (z)                                                           | FileDesc + handle(u32)                  |
| 0x02 | ROPENUP    | path (z)                                                           | FileDesc + handle(u32)                  |
| 0x03 | ROPENDIR   | path (z) (B-cmd variant)                                           | handle(u32) + token(u32); catalogue via S+B |
| 0x04 | RCREATE    | path (z)                                                           | FileDesc + handle                       |
| 0x05 | RCREATEDIR | path (z)                                                           | FileDesc + handle                       |
| 0x06 | RDELETE    | path (z)                                                           | FileDesc of deleted                     |
| 0x07 | RACCESS    | attrs(u32) at offset 8, path (z) at 16                             | FileDesc                                |
| 0x08 | RFREESPACE | (no extra)                                                         | impl-specific free/total u32s           |
| 0x09 | RRENAME    | new_len(u32) at 8, path (z) at 16 (old name)                       | Two-step; see state machine             |
| 0x0a | RCLOSE     | (no extra)                                                         | empty                                   |
| 0x0b | RREAD      | offset(u32), amount(u32)                                           | D/r stream then `R` bytes_sent+seqptr   |
| 0x0c | RWRITE     | offset(u32), amount(u32)                                           | w/d stream then empty `R`               |
| 0x0d | RREADDIR   | start_entry(u32)                                                   | Catalogue page in `R` payload           |
| 0x0e | RENSURE    | size(u32)                                                          | size(u32)                               |
| 0x0f | RSETLENGTH | size(u32)                                                          | size(u32)                               |
| 0x10 | RSETINFO   | load(u32), exec(u32)                                               | FileDesc (or empty `R` if stat fails)   |
| 0x11 | RGETSEQPTR | (no extra)                                                         | seqptr(u32)                             |
| 0x12 | RSETSEQPTR | seqptr(u32)                                                        | seqptr(u32)                             |
| 0x13 | RDEADHANDLES | (broadcast)                                                      | list of dead handles                    |
| 0x14 | RZERO      | offset(u32), length(u32)                                           | empty                                   |
| 0x15 | RVERSION   | (no extra)                                                         | u32 = 0x00000002                        |

## 7) State Machines with Hex Examples

### RWRITE (w/d reliable write over UDP)
Flow:
1) Client → Server: `A` with code 0x0c, handle H, offset O, amount N.
2) Server → Client: `w` request for rel range `[0, chunk)` where `chunk = min(1024, N)`.
3) Client → Server: `d` with `rel_pos=0` and up to `chunk` bytes.
4) Server writes; if more needed, send next `w` for the next window. Enforce sequentiality; if a gap is detected, re-request expected window and drop out-of-order data. Zero-length `d` triggers retry with backoff; abort after 1000 retries → `E(EIO)`.
5) On completion, server sends empty `R`.

Hex example (open handle=0x00000005, offset=0, amount=0x00000400):
- Request `A`:
  - Bytes: `41 rr rr cc 0c 00 00 00 05 00 00 00 00 00 00 00 00 04 00 00`
  - Breakdown: `41`='A', `rrrrrccc`=rid bytes, `0c000000`=code, `05000000`=handle, `00000000`=offset, `00040000`=amount 1024.
- Server `w` (request 0..1024): `77 rr rr cc 00 00 00 00 00 04 00 00`
- Client `d` (rel_pos=0, 1024 bytes follow): `64 rr rr cc 00 00 00 00 <1024 bytes>`
- Server `R`: `52 rr rr cc` (no payload)

### RREAD (D/r ping-pong)
Flow:
1) Client → Server: `A code=0x0b` with handle H, offset O (0xFFFFFFFF for sequential), amount N.
2) Server → Client: `D` with up to 1024 bytes.
3) Client → Server: `r` ack (no payload).
4) Repeat 2–3 until N satisfied; server then `R` with `bytes_sent` and `new_seqptr`.

Hex example (handle=3, offset=0, amount=32):
- Request `A`: `41 rr rr cc 0b 00 00 00 03 00 00 00 00 00 00 00 20 00 00 00`
- Server `D` with 32 bytes: `44 rr rr cc <32 bytes data>`
- Client `r`: `72 rr rr cc`
- Server `R`: `52 rr rr cc 20 00 00 00 20 00 00 00` (sent 32, seqptr=32)

### RRENAME (suffix-aware two-step)
Flow:
1) Arm (A code 0x09): client sends `new_len` and old path. Server resolves old path (searches `,xxx`), records suffix if present, and replies `w` to request name bytes.
2) Payload: client sends new name via `d` or `D` with same `rid`. For `d`, `rel_pos` must be 0. A leading 4-byte zero word is tolerated and skipped if `data_len == new_len`.
3) Server resolves new path; if old had `,xxx` and new lacks it, appends the suffix. Performs rename, replies `R` or `E(errno)`.

Hex example (rename `BugReport,fff` → `BugReport_ass` 12 bytes):
- Arm `A`: `41 rr rr cc 09 00 00 00 0c 00 00 00 00 00 00 00 42 75 67 52 65 70 6f 72 74 2c 66 66 66 00`
  - `0c000000` new_len=12, path="BugReport,fff".
- Server `w`: `77 rr rr cc 00 00 00 00 0c 00 00 00`
- Client `d`: `64 rr rr cc 00 00 00 00 42 75 67 52 65 70 6f 72 74 5f 61 73 73 00`
- Server resolves dest to `BugReport_ass,fff` (suffix preserved) and replies `52 rr rr cc` or `45 rr rr cc <errno>` on failure.

## 8) Directory Catalogue (S+B)
- After `ROPENDIR`, server sends catalogue pages as `S` + `B` payloads (UDP-sized). Each entry encodes name (with RISC OS mapping applied), attributes, filetype, size, timestamps. Clients that need more entries reissue `ROPENDIR`/`RREADDIR` with a start index.

## 9) Authentication (Access+, 32771)
- PIN derivation: uppercase ASCII; digit → 1..10, letter → 11..36; folded per char: `pin = pin*37 + val`.
- Shares marked `protected` require a successful Access+ exchange before path-based RPCs proceed. (Full Access+ frame layout is outside this file; behavior matches Acorn Access.)

## 10) Error Semantics
- An `E` reply carries a **RISC OS error block**: a u32 le error number followed by a NUL-terminated message. The client passes it straight back to the operating system as an error, so the message is not optional — without one the OS is shown whatever follows the number in the client's buffer, and the error appears as rubbish.
- The **number must be one RISC OS recognises**, not a POSIX errno. In particular the client compares against its own "not found" number when deciding whether an object exists, so a copy into a share depends on getting that one right: answer it with an errno and the client raises an error instead of creating the object.
- Numbers used here:

  | Meaning | Number | Space |
  | --- | --- | --- |
  | Not found | `0x100D6` | standard filing system |
  | Bad parameters | `0x100DC` | standard filing system |
  | End of file | `0x100DF` | standard filing system |
  | Not a directory | `0x14CC5` | remote filing system (0x100 + FS number 76, shifted, + `0xC5`) |
  | Access violation | `0x14CBD` | remote filing system |
  | Read only | `0x14C4C` | remote filing system |

- POSIX errno values from the host are mapped onto those before the reply is built (`riscos_error_for` in `src/ops.c`); anything unmapped is sent as bad parameters with `strerror` text, so it is at least readable.

## 11) Limits and Defaults
- Chunk size: 1024 bytes for RREAD/RWRITE.
- Internal path buffer: 512 bytes budget.
- Pending write zero-length retry cap: 1000 before EIO.

## 12) Broadcast / Freeway (32770)
- Servers emit `B` broadcasts advertising host and share/printer availability on a heartbeat. Payload is an Access/Freeway frame containing host ID and announced objects (opaque to RPC shown here). Clients should listen and refresh periodically.

## 13) Interop Checklist
- Always echo `rid` in replies; discard packets with wrong `rid`.
- Honor D/r (reads) and w/d (writes, rename name-fetch) ordering—no pipelining without required acks.
- Preserve `,xxx` suffixes on rename to retain filetype metadata.
- Apply RISC OS filename mappings (slash→dot, space→NBSP) consistently on both ends.
- Keep packets within typical UDP MTU; catalogue pages and data chunks are sized accordingly.
