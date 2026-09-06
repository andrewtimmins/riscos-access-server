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
- Read chunk size: **8192 bytes**, and it is not a free choice. A reading client acknowledges with the amount it has contiguously received plus a **bitmask** of the chunks it holds beyond that, and a chunk's bit is its offset divided by the chunk size, so the sizes must agree. Writes are driven by the client and follow its own sizing.
- Read window: **2 chunks** ahead of what the client has acknowledged, so 16KB in flight. A server should send every chunk in that window the client does not already hold, rather than one per acknowledgement: one costs a round trip per chunk, which over a wide-area link is the difference between a usable share and an unusable one.
- Do not exceed it. The protocol allows up to 32 and some client builds use 16, but the ordinary module tracks 2 and **discards anything beyond its window**. Overshooting also bursts the guest's network stack - an 8KB chunk is six IP fragments, so sixteen chunks is ninety-six frames at once - and the symptom is that small files transfer correctly while larger ones fail.
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
| `B`  | C↔S       | Extended request carrying an extra word: dir open, and the B-form read and readdir; also Freeway broadcast frame on 32770 |
| `a`  | C→S       | Handle-based request (close, read, write, seqptr, setinfo, …) |
| `F`  | C↔S       | Simple query with no payload of its own: RVERSION, RDEADHANDLES. Also sent unsolicited by the server to announce dead handles |
| `D`  | S→C       | Read data, with the transfer-relative offset in the header; also rename new-name payload (uppercase variant, C→S) |
| `d`  | C→S       | Write data; also rename new-name payload (lowercase variant) |
| `S`  | S→C       | Catalogue payload paired with `B` responses |
| `R`  | S→C       | Success reply |
| `E`  | S→C       | Error reply; RISC OS error number u32 le, then a NUL-terminated message (see section 10) |
| `w`  | S→C       | Request for byte range (writes, rename name-fetch) |
| `r`  | C→S       | Ack for read data, carrying how much has arrived and which chunks are held |

## 5) Generic RPC Framing (Port 49171)
- Common header: `cmd(1) | rid[3]`.
- A-command layout: `A | rid[3] | code(u32) | handle(u32) | payload...` (minimum 12 bytes)
  - Path-based codes (0x00–0x09, 0x16): payload is zero-terminated RISC OS path starting at offset 12.
  - Handle/binary codes (reads/writes/setinfo): the bytes from offset 12 carry command-specific fields.
- a-command layout: `a | rid[3] | code(u32) | handle(u32) | args...` (minimum 12 bytes). Same shape as A, but never carries a path, so no share-name authentication applies to it. The handle itself is the authority,, and the server binds each handle to the address and port that opened it.
- B-command layout: `B | rid[3] | code(u32) | handle(u32) | extra(u32) | path...` (minimum 16 bytes). The extra word sits between the handle and the path, so a B path starts at offset 16, not 12.
- F-command layout: `F | rid[3] | code(u32) | handle(u32)`: 12 bytes, no payload.
- Replies:
  - Success: `R | rid[3] | payload...`
  - Error:   `E | rid[3] | errnum(u32 le) | message(NUL-terminated)` — total `9 + strlen(message)`

### FileDesc Structure (20 bytes, le)
`load(u32) | exec(u32) | length(u32) | attrs(u32) | type(u32)`
- `load/exec`: as per section 2
- `length`: file length; a directory reports 0x800
- `attrs`: RISC OS bits (R,W,L,r,w)
- `type`: object type in bits 0–7 (1=file, 2=dir), then buffered (bit 8), interactive (bit 9), no-OSGBPB (bit 10). Files are announced as buffered, so an ordinary file is `0x101` and a directory `0x102`.

The type word is **last**. It is the same 20 bytes whether it arrives as a reply payload or as one entry of a catalogue page.

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
| 0x08 | RFREESPACE | path (z), optional                                                 | free(u32), largest creatable(u32), total(u32), each capped at 0xFFFFFFFF |
| 0x09 | RRENAME    | new_len(u32) at 8, path (z) at 16 (old name)                       | Two-step; see state machine             |
| 0x0a | RCLOSE     | (no extra)                                                         | empty                                   |
| 0x0b | RREAD      | offset(u32), amount(u32)                                           | D/r stream then `R` bytes_sent+seqptr   |
| 0x0c | RWRITE     | offset(u32), amount(u32)                                           | w/d stream then empty `R`               |
| 0x0d | RREADDIR   | start_entry(u32)                                                   | Catalogue page as S+B; see section 8    |
| 0x0e | RENSURE    | size(u32)                                                          | size(u32)                               |
| 0x0f | RSETLENGTH | size(u32)                                                          | size(u32)                               |
| 0x10 | RSETINFO   | load(u32), exec(u32)                                               | FileDesc (or empty `R` if stat fails)   |
| 0x11 | RGETSEQPTR | (no extra)                                                         | seqptr(u32)                             |
| 0x12 | RSETSEQPTR | seqptr(u32)                                                        | seqptr(u32)                             |
| 0x13 | RDEADHANDLES | (no extra)                                                       | count(u32) then that many handle ids. Also pushed unsolicited by the server as an `F` packet |
| 0x14 | RZERO      | offset(u32), length(u32)                                           | empty                                   |
| 0x15 | RVERSION   | (no extra)                                                         | u32 = 0x00000002 (as an F-command); the A-command form answers with a 16-bit 0x0002 |
| 0x16 | RFREESPACE64 | path (z), optional                                               | free, largest creatable and total, each as a lo/hi u32 pair (24 bytes) |

## 7) State Machines with Hex Examples

### RWRITE (w/d reliable write over UDP)
Flow:
1) Client → Server: `A` with code 0x0c, handle H, offset O, amount N.
2) Server → Client: `w` request for rel range `[0, chunk)` where `chunk = min(8192, N)`.
3) Client → Server: `d` with `rel_pos=0` and up to `chunk` bytes.
4) Server writes; if more needed, send next `w` for the next window. Enforce sequentiality; if a gap is detected, re-request expected window and drop out-of-order data. Zero-length `d` triggers retry with backoff; abort after 1000 retries → `E(EIO)`.
5) On completion, server sends empty `R`.

Hex example (open handle=0x00000005, offset=0, amount=0x00000400):
- Request `A`:
  - Bytes: `41 rr rr cc 0c 00 00 00 05 00 00 00 00 00 00 00 00 04 00 00`
  - Breakdown: `41`='A', `rrrrrccc`=rid bytes, `0c000000`=code, `05000000`=handle, `00000000`=offset, `00040000`=amount 1024.
- Server `w` (request 0..1024): `77 rr rr cc 00 00 00 00 00 00 00 00 00 04 00 00`
  - The `w` packet is 16 bytes: `w | rid[3] | rel_pos(u32) | zero(u32) | rel_end(u32)`. The zero word between the two positions is part of the format and is easy to miss.
- Client `d` (rel_pos=0, 1024 bytes follow): `64 rr rr cc 00 00 00 00 <1024 bytes>`
- Server `R`: `52 rr rr cc` (no payload)

### RREAD (D/r windowed read)
Flow:
1) Client → Server: `A code=0x0b` with handle H, offset O (0xFFFFFFFF for sequential), amount N.
2) Server → Client: every chunk of the window (2 chunks of 8192) the client does not already hold, as `D` packets. Each carries its own transfer-relative offset, so they need not arrive in order.
3) Client → Server: `r` ack, carrying how much it has contiguously received and a bitmask of the chunks it holds beyond that.
4) Repeat 2–3 until the acknowledged point reaches N; server then `R` with `bytes_sent` and `new_seqptr`.

It is a sliding window, not a strict ping-pong: one acknowledgement releases as many chunks as the window allows, and sending a single chunk per ack costs a round trip per 8KB.

Hex example (handle=3, offset=0, amount=32):
- Request `A`: `41 rr rr cc 0b 00 00 00 03 00 00 00 00 00 00 00 20 00 00 00`
- Server `D` with 32 bytes: `44 rr rr cc 00 00 00 00 <32 bytes data>`
  - The `D` header is 8 bytes: `D | rid[3] | offset(u32)`, the offset being relative to the start of this transfer rather than to the file.
- Client `r`: `72 rr rr cc 20 00 00 00 00 00 00 00` (32 received, no chunks held beyond that)
- Server `R`: `52 rr rr cc 20 00 00 00 20 00 00 00` (sent 32, seqptr=32)

#### The acknowledgement (`r`)
- Layout: `r | rid[3] | done(u32 le) | bits(u32 le)`.
- `done` is how much of the transfer the client has contiguously received; `bits` is a bitmask of the chunks it holds beyond that, bit *n* meaning the chunk at `done + n * 8192`.
- Both are relative to the start of the transfer. A shorter acknowledgement carries neither, and is treated as "nothing beyond what was last sent", so an older client that only ever acks still makes progress.

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

A catalogue page is one datagram holding an `S` header, the entries, and a `B` trailer:

`S | rid[3] | entries_len(u32) | trailer_len(u32) | entries... | B | rid[3] | trailer words...`

- `entries_len` counts the entry bytes only, not the trailer.
- `trailer_len` is `0x24` after `ROPENDIR` (the `B`+rid, then eight words) and `0x0c` for an `RREADDIR` page (the `B`+rid, then two words).

Each entry is a 20-byte FileDesc exactly as section 5 describes, immediately followed by the NUL-terminated RISC OS display name, the whole entry then padded with zeros to a 4-byte boundary. The name has the mappings of section 3 applied and any `,xxx` suffix stripped; the filetype it encoded is in the FileDesc instead.

Entries are packed until the next one would not fit, so a page is bounded by the datagram rather than by an entry count. A client wanting the rest reissues `RREADDIR` with the index of the first entry it has not seen; the listing is read and sorted once when the directory handle is opened, so pagination is consistent and does not rescan.

The eight trailer words after `ROPENDIR` are: load (`0xFFFFCD00`), exec (0), the entry length rounded up to a multiple of 2048, access (`0x13`), a share word derived from the handle, the handle itself, `entries_len` again, and `0xFFFFFFFF` as an end marker. The two-word `RREADDIR` trailer carries `entries_len` and `0xFFFFFFFF`.

## 9) Authentication (Access+, 32771)
- PIN derivation: uppercase ASCII; digit → 1..10, letter → 11..36; folded per char: `pin = pin*37 + val`, over at most the first 6 characters.
- Shares marked `protected` require a successful Access+ exchange before path-based RPCs proceed.

Discovery exchange, both directions being 32771:

- Client → Server: `0x00010001 | 0x00010001 | pin(u32)`. The client has folded the password the user typed and is asking which shares it unlocks.
- Server → Client, once per protected share whose PIN matches: `0x00010004 | 0x00010001 | (0x00010000 | name_len)(u32) | pin(u32) | name | attrs(1) | 0x00`, where `name_len` excludes the terminator and `attrs` is the descriptor byte of section 12.

A match also records the client as authenticated for that share. Authentication is per client address and share name, expires 10 minutes after its last use, and is refreshed by each accepted request.

Authentication is checked on the **path-based** commands, which is where a share name appears. A command carrying only a handle is authorised by the handle, which is bound to the address and port that opened it, so a second client cannot borrow the first one's handle to reach a protected share.

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

| Limit | Value |
| --- | --- |
| Read chunk (RREAD `D` payload) | 8192 bytes |
| Write chunk (the range one `w` asks for) | 8192 bytes |
| Read window | 2 chunks, so 16KB in flight |
| Catalogue page entry area | 1800 bytes, inside a 2048-byte datagram |
| Rename new-name length | under 512 bytes |
| Internal host path buffer | 1024 bytes |
| Concurrent pending reads / writes / renames | 32 / 32 / 16 |
| Pending read idle timeout | 30 seconds |
| Pending write zero-length retry cap | 1000 before EIO |
| Dead handles per `RDEADHANDLES` reply | 60 (240 bytes of table) |
| Entries read from one directory | 100000 |
| Access+ authenticated clients | 64, each entry expiring 10 minutes after last use |

The read chunk size and window are the two that are not ours to choose; see section 1.

## 12) Broadcast / Freeway (32770)

Servers announce each share and printer on a heartbeat (3 seconds by default). One announcement is one datagram:

`word0(u32) | flags(u32) | lengths(u32) | name(z) | descriptor...`

- `word0` is the object class and action: `0x00010002` for a disc being added, `0x00020002` for a printer.
- `flags` is `0x00010000`.
- `lengths` packs the descriptor length in the high half and the name length in the low half. The name length **includes** its NUL terminator.
- `name` is the share or printer name.

The field after the name is an **attribute descriptor, not a description**. For a share it is a single byte carrying the attribute bits tabulated below, and it is how a client learns a shared disc is read only before anything tries to write to it. Sending an empty string there announces every share as ordinary and writable whatever its configuration says.

| Bit | Attribute |
| --- | --- |
| 0x01 | Protected (Access+ authentication required) |
| 0x02 | Read only |
| 0x04 | Hidden from the browser |
| 0x08 | Access+ subdirectory share |
| 0x10 | CD-ROM |

Printer announcements carry their human-readable description string there instead, terminator included.

Protected shares are **not** announced here at all. They are offered only over Access+ on 32771, to a client that already knows the password, so an unauthenticated machine never learns the share exists.

Announcing an attribute is politeness; it lets the Filer show the share correctly. It is not the control. A read-only share must also refuse anything that would change it, or the setting means nothing to a client that ignores the announcement or never receives one.

## 13) Interop Checklist
- Always echo `rid` in replies; discard packets with wrong `rid`.
- Honour the w/d ordering for writes and the rename name-fetch: those are strictly sequential, and a gap must be re-requested rather than seeked past.
- Reads are the exception: send the whole window each acknowledgement, in 8192-byte chunks, and never more than the window.
- Preserve `,xxx` suffixes on rename to retain filetype metadata.
- Apply RISC OS filename mappings (slash→dot, space→NBSP) consistently on both ends.
- Keep packets within typical UDP MTU; catalogue pages and data chunks are sized accordingly.
