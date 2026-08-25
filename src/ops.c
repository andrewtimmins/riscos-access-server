/*
  ShareFS Server - File Operations (full impl)

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

#include "ops.h"
#include "accessplus.h"
#include "log.h"
#include "names.h"
#include "platform.h"
#include "riscos.h"
#include "net.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32
#define sfs_lstat(path, st) stat((path), (st))
#else
#define sfs_lstat(path, st) lstat((path), (st))
#endif

#ifdef _WIN32
static int map_winerr_to_errno(DWORD err) {
  switch (err) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
  case ERROR_INVALID_DRIVE:
    return ENOENT;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
  case ERROR_LOCK_VIOLATION:
    return EACCES;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS:
    return EEXIST;
  case ERROR_NOT_SAME_DEVICE:
    return EXDEV;
  case ERROR_INVALID_NAME:
    return EINVAL;
  default:
    return EIO;
  }
}
#endif

// Maximum pending write transfers
#define MAX_PENDING_WRITES 32
#define WRITE_CHUNK_SIZE 8192

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef _WIN32
#define strncasecmp _strnicmp
#endif

// Pending write transfer state
typedef struct {
  int active;
  int handle_id;
  uint32_t start_pos;   // Original start position from client
  uint32_t current_pos; // Current position in file
  uint32_t end_pos;     // End position (start + amount)
  unsigned char rid[3]; // Reply ID to use
  char addr[64];        // Client address
  unsigned short port;  // Client port
  int retries;          // Retry counter for empty packets
} pending_write_t;

static pending_write_t pending_writes[MAX_PENDING_WRITES];

// Matched on the reply id *and* the peer. The reply id is only three bytes,
// so on its own it let any host on the network join someone else's transfer.
static pending_write_t *find_pending_write(const unsigned char *rid,
                                           const char *addr,
                                           unsigned short port) {
  for (int i = 0; i < MAX_PENDING_WRITES; i++) {
    if (pending_writes[i].active && pending_writes[i].rid[0] == rid[0] &&
        pending_writes[i].rid[1] == rid[1] &&
        pending_writes[i].rid[2] == rid[2] &&
        pending_writes[i].port == port && addr &&
        strcmp(pending_writes[i].addr, addr) == 0) {
      return &pending_writes[i];
    }
  }
  return NULL;
}

static pending_write_t *alloc_pending_write(void) {
  for (int i = 0; i < MAX_PENDING_WRITES; i++) {
    if (!pending_writes[i].active) {
      pending_writes[i].active = 1;
      pending_writes[i].retries = 0;
      return &pending_writes[i];
    }
  }
  return NULL;
}

static void free_pending_write(pending_write_t *pw) {
  if (pw)
    pw->active = 0;
}

// Pending rename state (RRENAME 0x09 uses an A packet followed by a D packet
// containing the new name)
typedef struct {
  int active;
  unsigned char rid[3];
  uint32_t expected_len;      // New name length from the A packet
  char old_host_path[1024];   // Fully resolved old path on the host
  int suffix_type;            // Existing ,xxx suffix filetype (or -1 if none)
  char addr[64];              // Client address to reply to
  unsigned short port;        // Client port
  time_t started_at;          // When the rename was armed
} pending_rename_t;

// A single global meant one client renaming a file locked every other client
// out of renaming anything. Keyed by reply id and peer, like the other
// pending-transfer tables.
static void send_err_pkt(sfs_net *net, const unsigned char *rid, int code,
                         const char *addr, unsigned short port);

#define MAX_PENDING_RENAMES 16

static pending_rename_t pending_renames[MAX_PENDING_RENAMES];

static pending_rename_t *find_pending_rename(const unsigned char *rid,
                                             const char *addr,
                                             unsigned short port) {
  for (int i = 0; i < MAX_PENDING_RENAMES; i++) {
    if (pending_renames[i].active &&
        memcmp(pending_renames[i].rid, rid, 3) == 0 &&
        pending_renames[i].port == port && addr &&
        strcmp(pending_renames[i].addr, addr) == 0) {
      return &pending_renames[i];
    }
  }
  return NULL;
}

static pending_rename_t *alloc_pending_rename(void) {
  for (int i = 0; i < MAX_PENDING_RENAMES; i++) {
    if (!pending_renames[i].active) {
      memset(&pending_renames[i], 0, sizeof(pending_renames[i]));
      pending_renames[i].active = 1;
      pending_renames[i].suffix_type = -1;
      return &pending_renames[i];
    }
  }
  return NULL;
}

static void clear_pending_rename(pending_rename_t *pr) {
  if (!pr)
    return;
  pr->active = 0;
  pr->expected_len = 0;
  pr->suffix_type = -1;
}

// Time out renames whose client never sent the new name.
static void expire_stale_pending_renames(sfs_net *net) {
  time_t now = time(NULL);
  for (int i = 0; i < MAX_PENDING_RENAMES; i++) {
    pending_rename_t *pr = &pending_renames[i];
    if (pr->active && now - pr->started_at > 5) {
      sfs_log(SFS_LOG_INFO,
              "RRENAME: timeout waiting for D-pkt rid=%02x%02x%02x old='%s'",
              pr->rid[0], pr->rid[1], pr->rid[2], pr->old_host_path);
      // EBUSY so legacy clients show a standard error rather than a blank one
      send_err_pkt(net, pr->rid, EBUSY, pr->addr, pr->port);
      clear_pending_rename(pr);
    }
  }
}

static int safe_rename_cross(const char *oldp, const char *newp) {
#ifdef _WIN32
  // Windows: handle case-only renames and allow replacement without unlink
  // Case-only rename: use replace-existing to allow casing change in place
  DWORD flags = MOVEFILE_REPLACE_EXISTING;
  if (!MoveFileExA(oldp, newp, flags)) {
    errno = map_winerr_to_errno(GetLastError());
    return -1;
  }
  return 0;
#else
  return rename(oldp, newp);
#endif
}

// Pending read transfer state
#define MAX_PENDING_READS 32
#define READ_CHUNK_SIZE 1024

#define MAX_CLIENTS 128

// Helpers for little-endian encoding
static unsigned int read_u32(const unsigned char *p);
static void write_u32(unsigned char *p, unsigned int v);
static void send_f_pkt(sfs_net *net, uint32_t code, const void *data,
                       uint16_t dlen, const char *addr,
                       unsigned short port);

typedef struct {
  char addr[64];
  unsigned short port;
} sfs_client_entry;

static sfs_client_entry client_list[MAX_CLIENTS];
static size_t client_count = 0;

static void add_client(const char *addr, unsigned short port) {
  if (!addr)
    return;
  for (size_t i = 0; i < client_count; ++i) {
    if (client_list[i].port == port && strcmp(client_list[i].addr, addr) == 0)
      return;
  }
  if (client_count >= MAX_CLIENTS)
    return;
  strncpy(client_list[client_count].addr, addr,
          sizeof(client_list[client_count].addr) - 1);
  client_list[client_count].addr[sizeof(client_list[client_count].addr) - 1] =
      '\0';
  client_list[client_count].port = port;
  client_count++;
}

static void broadcast_deadhandles(sfs_net *net, sfs_handle_table *handles) {
  if (!net || !handles)
    return;
  size_t dead_count = 0;
  const int *dead = sfs_handles_get_dead(handles, &dead_count);
  if (!dead || dead_count == 0)
    return;

  size_t capped = dead_count > 60 ? 60 : dead_count; // ~240 bytes table
  size_t payload_len = (capped + 1) * sizeof(uint32_t);
  unsigned char *dh = (unsigned char *)malloc(payload_len);
  if (!dh)
    return;

  write_u32(dh, (uint32_t)capped);
  for (size_t i = 0; i < capped; ++i)
    write_u32(dh + 4 + i * 4, (uint32_t)dead[i]);

  for (size_t i = 0; i < client_count; ++i) {
    send_f_pkt(net, 0x13, dh, (uint16_t)payload_len, client_list[i].addr,
               client_list[i].port);
  }

  free(dh);
  sfs_handles_clear_dead(handles);
}

// States for RREAD ping-pong protocol
#define SFS_READ_STATE_WAIT_DATA_ACK 0
#define SFS_READ_STATE_WAIT_STATUS_ACK 1

typedef struct {
  int active;
  int state; // Current state
  int handle_id;
  uint32_t start_pos;   // Original start position
  uint32_t current_pos; // Current position (being sent)
  uint32_t end_pos;     // End position
  unsigned char rid[3];
  char addr[64];
  unsigned short port;
  time_t started_at;    // Wall-clock time the transfer began
} pending_read_t;

static pending_read_t pending_reads[MAX_PENDING_READS];

// As with writes, the peer forms part of the key.
static pending_read_t *find_pending_read(const unsigned char *rid,
                                         const char *addr,
                                         unsigned short port) {
  for (int i = 0; i < MAX_PENDING_READS; i++) {
    if (pending_reads[i].active && pending_reads[i].rid[0] == rid[0] &&
        pending_reads[i].rid[1] == rid[1] &&
        pending_reads[i].rid[2] == rid[2] &&
        pending_reads[i].port == port && addr &&
        strcmp(pending_reads[i].addr, addr) == 0) {
      return &pending_reads[i];
    }
  }
  return NULL;
}

static pending_read_t *alloc_pending_read(void) {
  for (int i = 0; i < MAX_PENDING_READS; i++) {
    if (!pending_reads[i].active) {
      pending_reads[i].active = 1;
      return &pending_reads[i];
    }
  }
  return NULL;
}

static void free_pending_read(pending_read_t *pr) {
  if (pr)
    pr->active = 0;
}

#define PENDING_READ_TIMEOUT_SECS 30

static void expire_stale_pending_reads(void) {
  time_t now = time(NULL);
  for (int i = 0; i < MAX_PENDING_READS; i++) {
    if (pending_reads[i].active &&
        now - pending_reads[i].started_at > PENDING_READ_TIMEOUT_SECS) {
      sfs_log(SFS_LOG_DEBUG,
              "Expiring stale pending read for handle %d (idle %lds)",
              pending_reads[i].handle_id,
              (long)(now - pending_reads[i].started_at));
      pending_reads[i].active = 0;
    }
  }
}

static unsigned int read_u32(const unsigned char *p) {
  return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
         ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void write_u32(unsigned char *p, unsigned int v) {
  p[0] = (unsigned char)(v & 0xFF);
  p[1] = (unsigned char)((v >> 8) & 0xFF);
  p[2] = (unsigned char)((v >> 16) & 0xFF);
  p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// Create parent directories for a path (like mkdir -p)
static int mkpath(const char *path) {
  char tmp[1024];
  size_t len = strlen(path);
  if (len >= sizeof(tmp))
    return -1;
  memcpy(tmp, path, len + 1);

  // Remove trailing slash
  if (len > 0 && tmp[len - 1] == '/')
    tmp[--len] = '\0';
  if (len == 0)
    return -1;

  // Create each component
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (sfs_mkdir(tmp) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }
  return sfs_mkdir(tmp);
}

// Send 'w' packet to request data from client
static void send_w_pkt(sfs_net *net, const unsigned char *rid, uint32_t rel_pos,
                       uint32_t rel_end, const char *addr,
                       unsigned short port) {
  // Format: w + rid(3) + pos(4) + zero(4) + end(4)
  unsigned char pkt[16];
  pkt[0] = 'w';
  pkt[1] = rid[0];
  pkt[2] = rid[1];
  pkt[3] = rid[2];
  write_u32(pkt + 4, rel_pos);
  write_u32(pkt + 8, 0);
  write_u32(pkt + 12, rel_end);
  sfs_log(SFS_LOG_DEBUG, "Sending w-pkt: rel_pos=%u rel_end=%u", rel_pos,
          rel_end);
  sfs_net_sendto(net->rpc, pkt, sizeof(pkt), addr, port);
}

// (Filename encoding helpers moved to names.c / names.h)

// Build the host path for a share using a pre-sanitized/encoded RISC OS tail.
// Walks existing directories separated by '.', then treats the remainder as
// the filename. The caller provides the already-encoded/rest string.
// Build a host path from a RISC OS path tail (after the share name), treating
// '.' as the RISC OS path separator and translating '/' inside components to
// '.' so that filenames containing slashes do not create subdirectories.
static int build_host_path_from_ro(const char *share_path, const char *rest,
                                   char *out, size_t out_sz) {
  int n = snprintf(out, out_sz, "%s", share_path);
  if (n < 0 || (size_t)n >= out_sz)
    return -1;

  size_t offset = (size_t)n;
  if (offset < out_sz)
    out[offset] = '\0';

  const char *cursor = rest;
  while (*cursor) {
    const char *nextdot = strchr(cursor, '.');
    size_t seglen = nextdot ? (size_t)(nextdot - cursor) : strlen(cursor);

    if (offset + 1 >= out_sz)
      return -1;
    out[offset++] = '/';
    out[offset] = '\0';

    // Copy the raw segment into a temporary buffer so we can encode it.
    // Worst-case expansion is 3x (every byte becomes %XX).
    char seg[1024];
    if (seglen >= sizeof(seg))
      return -1;
    memcpy(seg, cursor, seglen);
    seg[seglen] = '\0';

    char encoded[3 * sizeof(seg) + 1];
    if (sfs_encode_host_name(seg, encoded, sizeof(encoded)) != 0)
      return -1;

    size_t enc_len = strlen(encoded);
    if (offset + enc_len >= out_sz)
      return -1;
    memcpy(out + offset, encoded, enc_len + 1);
    offset += enc_len;

    if (!nextdot)
      break;
    cursor = nextdot + 1;
  }

  return 0;
}

// Confirm that a resolved host path really lies inside its share root.
//
// sfs_path_is_safe() rejects ".." and absolute paths, but it works on the
// textual path only, so a symlink planted inside a share still resolved to
// wherever it pointed. Compare the canonical paths instead. The target may
// not exist yet (creates), so walk up to the nearest ancestor that does.
static int path_within_share(const char *share_root, const char *host_path) {
  char real_root[PATH_MAX];
  if (!realpath(share_root, real_root))
    return 0;

  char candidate[1024];
  if (snprintf(candidate, sizeof(candidate), "%s", host_path) < 0)
    return 0;

  char real_path[PATH_MAX];
  while (!realpath(candidate, real_path)) {
    // Strip the last component and try again; a create targets a path that
    // does not exist, but its parent must still be inside the share.
    char *slash = strrchr(candidate, '/');
    if (!slash || slash == candidate)
      return 0;
    *slash = '\0';
  }

  size_t root_len = strlen(real_root);
  if (strncmp(real_path, real_root, root_len) != 0)
    return 0;
  // Either exactly the root, or a path below it. Guards against a sibling
  // directory whose name merely starts with the root's name.
  return real_path[root_len] == '\0' || real_path[root_len] == '/';
}

static int resolve_path(const sfs_config *cfg, const char *ro_path, char *out,
                        size_t out_sz) {
  if (!cfg || !ro_path || !out || out_sz == 0)
    return -1;

  // RISC OS uses '.' as path separator - convert to Unix '/'
  // Find the share name (first component before '.')
  const char *dot = strchr(ro_path, '.');
  size_t share_len = dot ? (size_t)(dot - ro_path) : strlen(ro_path);

  sfs_log(SFS_LOG_DEBUG, "resolve_path: ro_path='%s' share_len=%zu", ro_path,
          share_len);

  for (size_t i = 0; i < cfg->share_count; ++i) {
    const char *name = cfg->shares[i].name;
    if (name && strlen(name) == share_len &&
        strncasecmp(name, ro_path, share_len) == 0) {
      // Translate RISC OS characters to host filesystem characters:
      // - RISC OS '/' in filenames -> '.' (extension separator)
      // - RISC OS space -> non-breaking space (0xa0)
      // This avoids creating unintended directory structures while preserving
      // the ability to name files with slashes in their RISC OS names.
        const char *rest_raw = dot ? dot + 1 : "";

        if (build_host_path_from_ro(cfg->shares[i].path, rest_raw, out, out_sz) !=
          0)
        return -1;

      sfs_log(SFS_LOG_DEBUG, "resolve_path: resolved to '%s'", out);

      // Safety check on final path - skip the leading '/' separator
      const char *rel = out + strlen(cfg->shares[i].path);
      if (*rel == '/')
        rel++; // Skip separator
      if (!sfs_path_is_safe(rel)) {
        sfs_log(SFS_LOG_DEBUG, "resolve_path: safety check failed on '%s'",
                rel);
        return -1;
      }
      if (!path_within_share(cfg->shares[i].path, out)) {
        sfs_log(SFS_LOG_ERROR,
                "resolve_path: '%s' escapes share '%s' (symlink?)", out,
                cfg->shares[i].name ? cfg->shares[i].name : "?");
        return -1;
      }
      return 0;
    }
  }
  sfs_log(SFS_LOG_DEBUG, "resolve_path: no matching share found");
  return -1;
}

// Try to find a file, checking for ,xxx filetype suffix variants
// If the exact path doesn't exist, scan directory for matching base name with
// suffix
static int find_file_with_suffix(const char *base_path, char *out,
                                 size_t out_sz) {
  struct stat st;

  // First, try exact path
  if (stat(base_path, &st) == 0) {
    strncpy(out, base_path, out_sz - 1);
    out[out_sz - 1] = '\0';
    return 0;
  }

  // Extract directory and filename
  const char *last_slash = strrchr(base_path, '/');
  if (!last_slash) {
    return -1; // No directory component
  }

  size_t dir_len = (size_t)(last_slash - base_path);
  char dir_path[1024];
  if (dir_len >= sizeof(dir_path))
    return -1;
  memcpy(dir_path, base_path, dir_len);
  dir_path[dir_len] = '\0';

  const char *filename = last_slash + 1;
  size_t filename_len = strlen(filename);

  // Scan directory for file with matching base name + ,xxx suffix
  DIR *d = opendir(dir_path);
  if (!d)
    return -1;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    size_t ent_len = strlen(ent->d_name);

    // Check for base name + ,xxx pattern
    if (ent_len == filename_len + 4 &&
        strncasecmp(ent->d_name, filename, filename_len) == 0 &&
        ent->d_name[filename_len] == ',' &&
        sfs_filetype_from_suffix(ent->d_name) >= 0) {

      snprintf(out, out_sz, "%s/%s", dir_path, ent->d_name);
      closedir(d);
      return 0;
    }
  }

  closedir(d);
  return -1;
}

static void send_err_pkt(sfs_net *net, const unsigned char *rid, int code,
                         const char *addr, unsigned short port) {
  unsigned char pkt[8] = {'E', rid[0], rid[1], rid[2], 0, 0, 0, 0};
  pkt[4] = (unsigned char)(code & 0xFF);
  sfs_log(SFS_LOG_PROTOCOL, "Sending E-pkt: error=%d", code);
  sfs_net_sendto(net->rpc, pkt, sizeof(pkt), addr, port);
}

static void send_r_pkt(sfs_net *net, const unsigned char *rid, const void *data,
                       size_t dlen, const char *addr, unsigned short port) {
  unsigned char header[4] = {'R', rid[0], rid[1], rid[2]};
  struct {
    unsigned char h[4];
    unsigned char p[2048];
  } pkt;
  if (dlen > sizeof(pkt.p))
    dlen = sizeof(pkt.p);
  memcpy(pkt.h, header, 4);
  if (data && dlen)
    memcpy(pkt.p, data, dlen);
  sfs_log(SFS_LOG_PROTOCOL, "Sending R-pkt: %zu bytes", dlen);
  sfs_net_sendto(net->rpc, &pkt, 4 + dlen, addr, port);
}

// Send an unsolicited F packet (e.g., RDEADHANDLES broadcast)
static void send_f_pkt(sfs_net *net, uint32_t code, const void *data,
                       uint16_t dlen, const char *addr,
                       unsigned short port) {
  if (!net)
    return;

  // Format: 'F' + rid(3)=0 + code(4) + handle(4)=0 + payload
  unsigned char header[12] = {'F', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  write_u32(header + 4, code);
  // handle field left as zero

  unsigned char pkt[256];
  size_t max_copy = sizeof(pkt) - sizeof(header);
  size_t copy_len = dlen;
  if (copy_len > max_copy)
    copy_len = max_copy;

  memcpy(pkt, header, sizeof(header));
  if (data && copy_len)
    memcpy(pkt + sizeof(header), data, copy_len);

  sfs_net_sendto(net->rpc, pkt, sizeof(header) + copy_len, addr, port);
}

static void send_d_pkt_with_offset(sfs_net *net, const unsigned char *rid,
                                   uint32_t offset, const void *data,
                                   size_t dlen, const char *addr,
                                   unsigned short port) {
  if (dlen > 0)
    sfs_log(SFS_LOG_DEBUG,
            "RREAD: SEND PAYLOAD RID=%02x%02x%02x Offset=%u Len=%zu", rid[0],
            rid[1], rid[2], offset, dlen);
  else
    sfs_log(SFS_LOG_DEBUG, "RREAD: SEND STATUS RID=%02x%02x%02x Offset=%u",
            rid[0], rid[1], rid[2], offset);

  unsigned char header[8];
  header[0] = 'D';
  header[1] = rid[0];
  header[2] = rid[1];
  header[3] = rid[2];
  write_u32(header + 4, offset);

  struct {
    unsigned char h[8];
    unsigned char p[2048];
  } pkt;
  if (dlen > sizeof(pkt.p))
    dlen = sizeof(pkt.p);
  memcpy(pkt.h, header, 8);
  if (data && dlen)
    memcpy(pkt.p, data, dlen);

  sfs_net_sendto(net->rpc, &pkt, 8 + dlen, addr, port);
}

// Build FileDesc (20 bytes): load(4), exec(4), length(4), attrs(4),
// type+flags(4)
static void build_filedesc(unsigned char *out, const struct stat *st,
                           uint32_t filetype) {
  uint64_t cs = sfs_time_to_riscos(st->st_mtime);
  uint32_t load = sfs_make_load_addr(filetype, cs);
  uint32_t exec = sfs_make_exec_addr(cs);
  // Cap file sizes >4 GB to 0xFFFFFFFF; the wire protocol is 32-bit.
  uint32_t len = S_ISDIR(st->st_mode) ? 0x800u
               : (st->st_size > (off_t)0xFFFFFFFF ? 0xFFFFFFFFu
                                                    : (uint32_t)st->st_size);
  uint32_t attrs = sfs_mode_to_attrs(st->st_mode);
  uint32_t type = S_ISDIR(st->st_mode) ? SFS_TYPE_DIR : SFS_TYPE_FILE;

  // Pack type with flags: type (bits 0-7), buffered (bit 8), interactive (bit
  // 9), noosgbpb (bit 10) Files are buffered (bit 8 = 1), not interactive (bit
  // 9 = 0), support OSGBPB (bit 10 = 0)
  uint32_t type_and_flags = type | (1 << 8); // Set buffered flag

  write_u32(out, load);
  write_u32(out + 4, exec);
  write_u32(out + 8, len);
  write_u32(out + 12, attrs);
  write_u32(out + 16, type_and_flags);
}

// Comparator for qsort on sfs_dir_entry: case-insensitive alphabetical.
static int compare_dir_entries(const void *a, const void *b) {
  const sfs_dir_entry *ea = (const sfs_dir_entry *)a;
  const sfs_dir_entry *eb = (const sfs_dir_entry *)b;
  return strcasecmp(ea->name ? ea->name : "", eb->name ? eb->name : "");
}

// Read a directory once, decode all entry names, sort, and attach to the
// handle's cache.  After this the handle owns the entries array.
static void populate_dir_listing(const char *host_path, const sfs_config *cfg,
                                 sfs_handle_table *handles, int hid) {
  DIR *d = opendir(host_path);
  if (!d)
    return;

  sfs_dir_entry *entries = NULL;
  size_t count = 0;
  size_t capacity = 0;
  struct dirent *ent;

  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;

    if (count >= 100000) {
      sfs_log(SFS_LOG_INFO, "populate_dir_listing: capped at 100000 entries in '%s'",
              host_path);
      break;
    }

    char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", host_path, ent->d_name);

    struct stat st;
    if (sfs_lstat(full_path, &st) != 0)
      continue;

    int filetype = S_ISDIR(st.st_mode)
                       ? (int)SFS_FILETYPE_DIR
                       : (int)sfs_filetype_from_ext(ent->d_name, cfg);

    char stripped[1024];
    sfs_strip_type_suffix(ent->d_name, stripped, sizeof(stripped));
    char ros_name[1024];
    if (sfs_decode_host_name(stripped, ros_name, sizeof(ros_name)) != 0) {
      strncpy(ros_name, stripped, sizeof(ros_name) - 1);
      ros_name[sizeof(ros_name) - 1] = '\0';
    }

    if (count >= capacity) {
      size_t new_cap = capacity == 0 ? 64 : capacity * 2;
      sfs_dir_entry *p = (sfs_dir_entry *)realloc(entries,
                                                   new_cap * sizeof(sfs_dir_entry));
      if (!p)
        break;
      entries = p;
      capacity = new_cap;
    }

    entries[count].name = strdup(ros_name);
    if (!entries[count].name)
      break;
    entries[count].st = st;
    entries[count].filetype = filetype;
    count++;
  }
  closedir(d);

  if (count > 0)
    qsort(entries, count, sizeof(sfs_dir_entry), compare_dir_entries);

  sfs_handle_set_dir_listing(handles, hid, entries, count);
}

// Serialize cached directory entries into an output buffer (wire format).
// Returns the number of bytes written.
static size_t build_dir_entries_from_cache(const sfs_dir_entry *entries,
                                           size_t count, unsigned char *out,
                                           size_t out_sz, size_t start_entry) {
  size_t offset = 0;
  for (size_t i = start_entry; i < count; i++) {
    const sfs_dir_entry *e = &entries[i];
    size_t name_len = e->name ? strlen(e->name) : 0;
    size_t entry_size = 20 + name_len + 1;
    entry_size = (entry_size + 3) & ~3u;

    if (offset + entry_size > out_sz)
      break;

    build_filedesc(out + offset, &e->st, (uint32_t)e->filetype);
    if (e->name)
      memcpy(out + offset + 20, e->name, name_len + 1);
    else
      out[offset + 20] = '\0';

    size_t pad_start = 20 + name_len + 1;
    while (pad_start < entry_size)
      out[offset + pad_start++] = 0;

    offset += entry_size;
  }
  return offset;
}

// Send a combined S+B response for directory catalogue.
// Uses the pre-built sorted cache from the handle.
static void send_catalogue_response(sfs_net *net, const unsigned char *rid,
                                    const sfs_dir_entry *dir_entries,
                                    size_t dir_count, int handle,
                                    const char *addr, unsigned short port) {
  // Entry buffer: 1800 bytes per packet (fits in a single Ethernet frame after
  // the S+B header overhead of ~48 bytes, keeping total ~1848 bytes < 2048).
  unsigned char pkt[2048];
  size_t offset = 0;

  pkt[offset++] = 'S';
  pkt[offset++] = rid[0];
  pkt[offset++] = rid[1];
  pkt[offset++] = rid[2];

  unsigned char entries[1800];
  size_t entries_len = build_dir_entries_from_cache(
      dir_entries, dir_count, entries, sizeof(entries), 0);

  write_u32(pkt + offset, (uint32_t)entries_len);
  offset += 4;
  write_u32(pkt + offset, 0x24); // trailer_len = 36 bytes (B+rid + 8 words)
  offset += 4;

  memcpy(pkt + offset, entries, entries_len);
  offset += entries_len;

  pkt[offset++] = 'B';
  pkt[offset++] = rid[0];
  pkt[offset++] = rid[1];
  pkt[offset++] = rid[2];

  uint32_t load = 0xFFFFCD00;
  uint32_t exec_val = 0x00000000;
  uint32_t rounded_len = ((uint32_t)entries_len + 2047) & ~2047u;
  uint32_t access = 0x13;
  uint32_t share_val = (((uint32_t)handle) & 0xFFFFFF00) ^ 0xFFFFFF02;
  uint32_t marker = 0xFFFFFFFF;

  write_u32(pkt + offset, load);          offset += 4;
  write_u32(pkt + offset, exec_val);      offset += 4;
  write_u32(pkt + offset, rounded_len);   offset += 4;
  write_u32(pkt + offset, access);        offset += 4;
  write_u32(pkt + offset, share_val);     offset += 4;
  write_u32(pkt + offset, (uint32_t)handle); offset += 4;
  write_u32(pkt + offset, (uint32_t)entries_len); offset += 4;
  write_u32(pkt + offset, marker);        offset += 4;

  sfs_log(SFS_LOG_PROTOCOL,
          "Sending S+B catalogue: %zu bytes, %zu entries_len, handle=%d",
          offset, entries_len, handle);
  sfs_net_sendto(net->rpc, pkt, offset, addr, port);
}

// Send S+B response for RREADDIR pagination.
static void send_readdir_response(sfs_net *net, const unsigned char *rid,
                                  const sfs_dir_entry *dir_entries,
                                  size_t dir_count, int handle,
                                  size_t start_entry, const char *addr,
                                  unsigned short port) {
  (void)handle; // not embedded in the readdir wire format
  unsigned char pkt[2048];
  size_t offset = 0;

  pkt[offset++] = 'S';
  pkt[offset++] = rid[0];
  pkt[offset++] = rid[1];
  pkt[offset++] = rid[2];

  unsigned char entries[1800];
  size_t entries_len = build_dir_entries_from_cache(
      dir_entries, dir_count, entries, sizeof(entries), start_entry);

  write_u32(pkt + offset, (uint32_t)entries_len); offset += 4;
  write_u32(pkt + offset, 0x0c);                 offset += 4;

  memcpy(pkt + offset, entries, entries_len);
  offset += entries_len;

  pkt[offset++] = 'B';
  pkt[offset++] = rid[0];
  pkt[offset++] = rid[1];
  pkt[offset++] = rid[2];

  write_u32(pkt + offset, (uint32_t)entries_len); offset += 4;
  write_u32(pkt + offset, 0xFFFFFFFF);            offset += 4;

  sfs_net_sendto(net->rpc, pkt, offset, addr, port);
}

// Check if client is authorized to access a share (returns 1 if OK, 0 if
// denied)
static int check_share_auth(const sfs_config *cfg, sfs_auth_state *auth,
                            const char *client_ip, const char *ro_path) {
  if (!cfg || !ro_path)
    return 0;

  // Extract share name from RISC OS path
  const char *dot = strchr(ro_path, '.');
  size_t share_len = dot ? (size_t)(dot - ro_path) : strlen(ro_path);

  // Find the share
  for (size_t i = 0; i < cfg->share_count; ++i) {
    const char *name = cfg->shares[i].name;
    if (name && strlen(name) == share_len &&
        strncasecmp(name, ro_path, share_len) == 0) {
      // Found the share - check if protected
      if (!(cfg->shares[i].attributes & SFS_ATTR_PROTECTED)) {
        return 1; // Not protected, allow
      }
      // Protected - check if client is authenticated
      if (auth && sfs_auth_check(auth, client_ip, name)) {
        return 1; // Authenticated
      }
      sfs_log(SFS_LOG_DEBUG,
              "Auth denied: client %s not authenticated for share '%s'",
              client_ip ? client_ip : "?", name);
      return 0; // Denied
    }
  }
  return 0; // Share not found
}

int sfs_rpc_handle(const unsigned char *buf, size_t len, const char *addr,
                   unsigned short port, const sfs_config *cfg, sfs_net *net,
                   sfs_handle_table *handles, sfs_auth_state *auth) {
  if (!buf || len < 4 || !net || !cfg || !handles)
    return -1;

  expire_stale_pending_reads();

  // Time out renames whose client never delivered the D-packet
  expire_stale_pending_renames(net);

  // Track client for potential broadcasts
  add_client(addr, port);
  // If there are pending dead handles, broadcast them now
  broadcast_deadhandles(net, handles);

  unsigned char cmd = buf[0];
  unsigned char rid[3] = {buf[1], buf[2], buf[3]};

  // Hex dump for debugging
  char hexdump[128];
  size_t hlen = len > 32 ? 32 : len;
  for (size_t i = 0; i < hlen; ++i) {
    snprintf(hexdump + i * 3, 4, "%02x ", buf[i]);
  }
  sfs_log(SFS_LOG_PROTOCOL, "RPC cmd='%c' len=%zu: %s",
          (cmd >= 32 && cmd < 127) ? cmd : '?', len, hexdump);

  char host_path[1024];

  // Command 'A' is the main file operation command
  // Format: cmd(1) + rid(3) + code(4) + handle(4) + path...
  if (cmd == 'A') {
    if (len < 12) {
      send_err_pkt(net, rid, EINVAL, addr, port);
      return 0;
    }
    uint32_t code = read_u32(buf + 4);
    uint32_t handle = read_u32(buf + 8);

    // Only some commands have a path at position 12. Handle-based commands
    // like RCLOSE (0x0a), RREAD (0x0b), RWRITE (0x0c), RSETINFO (0x10), etc.
    // have binary data there instead (offsets, lengths, load/exec addresses).
    // Path-based commands: 0x00-0x09, 0x16
    int has_path = (code <= 0x09 || code == 0x16);
    const char *path = (has_path && len > 12) ? (const char *)(buf + 12) : "";

    if (has_path) {
      sfs_log(SFS_LOG_PROTOCOL, "A-cmd code=%u handle=%u path='%s'", code,
              handle, path);
    } else {
      sfs_log(SFS_LOG_PROTOCOL, "A-cmd code=%u handle=%u (handle-based)", code,
              handle);
    }

    // Check authentication for path-based operations only
    if (has_path && path[0] && !check_share_auth(cfg, auth, addr, path)) {
      send_err_pkt(net, rid, EACCES, addr, port);
      return 0;
    }

    switch (code) {
    case 0x00: // RFIND
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Try to find file with ,xxx suffix if exact path doesn't exist
      char actual_path[1024];
      if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) !=
          0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      struct stat st;
      if (stat(actual_path, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      uint32_t filetype = S_ISDIR(st.st_mode)
                              ? SFS_FILETYPE_DIR
                              : sfs_filetype_from_ext(actual_path, cfg);
      unsigned char desc[20];
      build_filedesc(desc, &st, filetype);
      send_r_pkt(net, rid, desc, sizeof(desc), addr, port);
      break;
    }

    case 0x01: // ROPENIN (open for reading)
    case 0x02: // ROPENUP (open for read/write)
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        // If path is empty, they're asking about the share itself
        if (path[0] == '\0') {
          send_err_pkt(net, rid, ENOENT, addr, port);
          break;
        }
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Try to find file with ,xxx suffix if exact path doesn't exist
      char actual_path[1024];
      if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) !=
          0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      struct stat st;
      if (stat(actual_path, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      // If it's a directory, return directory info
      if (S_ISDIR(st.st_mode)) {
        uint32_t filetype = SFS_FILETYPE_DIR;
        uint64_t cs = sfs_time_to_riscos(st.st_mtime);

        int hid = 0, tok = 0;
        if (sfs_handles_add_ex(
                handles, SFS_HANDLE_DIR, -1, actual_path,
                sfs_make_load_addr(filetype, cs), sfs_make_exec_addr(cs), 0,
                sfs_mode_to_attrs(st.st_mode), &hid, &tok) != 0) {
          send_err_pkt(net, rid, EMFILE, addr, port);
          break;
        }

        sfs_handle_set_client(handles, hid, addr, port);
        // Reply: FileDesc(20) + handle(4)
        unsigned char reply[24];
        build_filedesc(reply, &st, filetype);
        write_u32(reply + 20, (uint32_t)hid);
        send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      } else {
        // It's a file
        uint32_t filetype = sfs_filetype_from_ext(actual_path, cfg);
        int flags = ((code == 0x01) ? O_RDONLY : O_RDWR) | O_BINARY;

        int fd = open(actual_path, flags);
        if (fd < 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
        // filetype already calculated above
        uint64_t cs = sfs_time_to_riscos(st.st_mtime);

        int hid = 0, tok = 0;
        if (sfs_handles_add_ex(handles, SFS_HANDLE_FILE, fd, actual_path,
                               sfs_make_load_addr(filetype, cs),
                               sfs_make_exec_addr(cs), (uint32_t)st.st_size,
                               sfs_mode_to_attrs(st.st_mode), &hid,
                               &tok) != 0) {
          close(fd);
          send_err_pkt(net, rid, EMFILE, addr, port);
          break;
        }

        sfs_handle_set_client(handles, hid, addr, port);
        // Reply: FileDesc(20) + handle(4)
        unsigned char reply[24];
        build_filedesc(reply, &st, filetype);
        write_u32(reply + 20, (uint32_t)hid);
        send_r_pkt(net, rid, reply, sizeof(reply), addr, port);

        // Store open flags for reopening (needed for Windows rename)
        sfs_handle *h = NULL;
        if (sfs_handles_get(handles, hid, &h) == 0 && h) {
          h->open_flags = flags;
        }
      }
      break;
    }

    case 0x03: // ROPENDIR
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      struct stat st;
      if (stat(host_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        send_err_pkt(net, rid, ENOTDIR, addr, port);
        break;
      }
      int hid = 0, tok = 0;
      if (sfs_handles_add_ex(handles, SFS_HANDLE_DIR, -1, host_path, 0, 0, 0,
                             sfs_mode_to_attrs(st.st_mode), &hid, &tok) != 0) {
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }
      sfs_handle_set_client(handles, hid, addr, port);
      // Pre-build the sorted directory listing so RREADDIR is O(1) per page.
      populate_dir_listing(host_path, cfg, handles, hid);
      // Return handle + token in R response
      unsigned char reply[8];
      write_u32(reply, (uint32_t)hid);
      write_u32(reply + 4, (uint32_t)tok);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x04: // RCREATE
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Create parent directories if needed
      char parent[1024];
      strncpy(parent, host_path, sizeof(parent) - 1);
      parent[sizeof(parent) - 1] = '\0';
      char *last_slash = strrchr(parent, '/');
      if (!last_slash) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      *last_slash = '\0';
      mkpath(parent);

      uint32_t filetype = sfs_filetype_from_ext(host_path, cfg);
      int flags = O_CREAT | O_TRUNC | O_RDWR | O_BINARY;
      int fd = open(host_path, flags, 0664);
      if (fd < 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      struct stat st;
      fstat(fd, &st);
      // filetype already calculated
      uint64_t cs = sfs_time_to_riscos(time(NULL));

      int hid = 0, tok = 0;
      if (sfs_handles_add_ex(
              handles, SFS_HANDLE_FILE, fd, host_path,
              sfs_make_load_addr(filetype, cs), sfs_make_exec_addr(cs), 0,
              SFS_ATTR_R | SFS_ATTR_W | SFS_ATTR_r, &hid, &tok) != 0) {
        close(fd);
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }

      sfs_handle_set_client(handles, hid, addr, port);
      // Store open flags (O_CREAT | O_TRUNC | O_RDWR)
      sfs_handle *h = NULL;
      if (sfs_handles_get(handles, hid, &h) == 0 && h) {
        h->open_flags = flags;
      }

      unsigned char reply[24];
      build_filedesc(reply, &st, filetype);
      write_u32(reply + 20, (uint32_t)hid);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x05: // RCREATEDIR
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Use mkpath to create parent directories as needed
      if (mkpath(host_path) != 0 && errno != EEXIST) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      struct stat st;
      stat(host_path, &st);
      int hid = 0, tok = 0;
      if (sfs_handles_add_ex(handles, SFS_HANDLE_DIR, -1, host_path, 0, 0, 0,
                             sfs_mode_to_attrs(st.st_mode), &hid, &tok) != 0) {
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }
      sfs_handle_set_client(handles, hid, addr, port);
      // Return FileDesc(20) + handle(4) = 24 bytes
      unsigned char reply[24];
      build_filedesc(reply, &st, SFS_FILETYPE_DIR);
      write_u32(reply + 20, (uint32_t)hid);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x06: // RDELETE
    {
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Try to find file with ,xxx suffix if exact path doesn't exist
      char actual_path[1024];
      if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) !=
          0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      struct stat st;
      if (stat(actual_path, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      unsigned char reply[20];
      build_filedesc(reply, &st,
                     S_ISDIR(st.st_mode)
                         ? SFS_FILETYPE_DIR
                         : sfs_filetype_from_ext(actual_path, cfg));
      if (unlink(actual_path) != 0 && rmdir(actual_path) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x07: // RACCESS (set attributes)
    {
      // Format: cmd(1) + rid(3) + code(4) + attrs(4) + handle(4) + path...
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      uint32_t new_attrs = read_u32(buf + 8);
      const char *attr_path = (len > 16) ? (const char *)(buf + 16) : "";
      if (resolve_path(cfg, attr_path, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      // Try to find file with ,xxx suffix if exact path doesn't exist
      char actual_path[1024];
      if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) !=
          0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      struct stat st;
      if (stat(actual_path, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      // Map to Unix mode
      mode_t mode = 0;
      if (new_attrs & SFS_ATTR_R)
        mode |= 0400;
      if (new_attrs & SFS_ATTR_W)
        mode |= 0200;
      if (new_attrs & SFS_ATTR_r)
        mode |= 0044;
      if (new_attrs & SFS_ATTR_w)
        mode |= 0022;
      // Directories need execute bit to be accessible, and owner always needs
      // access
      if (S_ISDIR(st.st_mode)) {
        mode |= 0700; // Owner always gets rwx for directories
        if (mode & 0040)
          mode |= 0010; // group read -> group exec
        if (mode & 0004)
          mode |= 0001; // other read -> other exec
      }
      chmod(actual_path, mode);
      unsigned char reply[20];
      build_filedesc(reply, &st,
                     S_ISDIR(st.st_mode)
                         ? SFS_FILETYPE_DIR
                         : sfs_filetype_from_ext(actual_path, cfg));
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x08: // RFREESPACE
    {
      if (path[0] &&
          resolve_path(cfg, path, host_path, sizeof(host_path)) == 0) {
        // Use resolved path
      } else if (cfg->share_count > 0) {
        snprintf(host_path, sizeof(host_path), "%s", cfg->shares[0].path);
      } else {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      sfs_fsinfo fsinfo;
      if (sfs_get_fsinfo(host_path, &fsinfo) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      unsigned char reply[12];
      write_u32(reply,
                (uint32_t)(fsinfo.free_bytes > 0xFFFFFFFF ? 0xFFFFFFFF
                                                          : fsinfo.free_bytes));
      write_u32(reply + 4,
                (uint32_t)(fsinfo.free_bytes > 0xFFFFFFFF
                               ? 0xFFFFFFFF
                               : fsinfo.free_bytes)); // Largest creatable
      write_u32(reply + 8, (uint32_t)(fsinfo.total_bytes > 0xFFFFFFFF
                                          ? 0xFFFFFFFF
                                          : fsinfo.total_bytes));
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x16: // RFREESPACE64 - 64-bit free space
    {
      // handle is at buf+8
      // Response: 6 x 4 bytes (free_lo, free_hi, largest_lo, largest_hi,
      // total_lo, total_hi)
      sfs_fsinfo fsinfo;
      memset(&fsinfo, 0, sizeof(fsinfo));
      // Answer for the share named in the request; only fall back to the
      // first share when no usable path was given.
      if (path[0] &&
          resolve_path(cfg, path, host_path, sizeof(host_path)) == 0) {
        sfs_get_fsinfo(host_path, &fsinfo);
      } else if (cfg->share_count > 0) {
        sfs_get_fsinfo(cfg->shares[0].path, &fsinfo);
      }
      unsigned char reply[24];
      write_u32(reply, (uint32_t)(fsinfo.free_bytes & 0xFFFFFFFF));
      write_u32(reply + 4, (uint32_t)(fsinfo.free_bytes >> 32));
      write_u32(reply + 8, (uint32_t)(fsinfo.free_bytes & 0xFFFFFFFF));
      write_u32(reply + 12, (uint32_t)(fsinfo.free_bytes >> 32));
      write_u32(reply + 16, (uint32_t)(fsinfo.total_bytes & 0xFFFFFFFF));
      write_u32(reply + 20, (uint32_t)(fsinfo.total_bytes >> 32));
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x0a: // RCLOSE - close handle
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) = 12 bytes
      int hid = (int)handle;
      sfs_handles_remove(handles, hid);
      // Empty success reply
      send_r_pkt(net, rid, NULL, 0, addr, port);
      break;
    }

    case 0x0b: // RREAD
    {
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t offset = read_u32(buf + 12);
      uint32_t rlen = read_u32(buf + 16);

      sfs_log(SFS_LOG_DEBUG, "A-cmd RREAD: handle=%d offset=%u len=%u", hid,
              offset, rlen);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h || h->fd < 0) {
        // Broadcast any pending dead handles to all known clients
        broadcast_deadhandles(net, handles);
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // Allocate pending read
      pending_read_t *pr = alloc_pending_read();
      if (!pr) {
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }

      pr->handle_id = hid;
      pr->start_pos = offset;
      pr->current_pos = offset;
      pr->end_pos = offset + rlen;
      pr->state = SFS_READ_STATE_WAIT_DATA_ACK;
      pr->rid[0] = rid[0];
      pr->rid[1] = rid[1];
      pr->rid[2] = rid[2];
      strncpy(pr->addr, addr, sizeof(pr->addr) - 1);
      pr->addr[sizeof(pr->addr) - 1] = '\0';
      pr->port = port;
      pr->started_at = time(NULL);

      // Send first chunk
      uint32_t amount = (rlen < READ_CHUNK_SIZE) ? rlen : READ_CHUNK_SIZE;

      if (lseek(h->fd, (off_t)pr->current_pos, SEEK_SET) < 0) {
        free_pending_read(pr);
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      unsigned char data[READ_CHUNK_SIZE];
      ssize_t n = read(h->fd, data, amount);
      if (n < 0) {
        free_pending_read(pr);
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      // Send D packet with offset relative to start (should be 0 for first
      // chunk)
      send_d_pkt_with_offset(net, rid, pr->current_pos - pr->start_pos, data,
                             (size_t)n, addr, port);

      pr->current_pos += (uint32_t)n;

      // If completed immediately (small file), send R packet too
      if (pr->current_pos >= pr->end_pos) {
        unsigned char reply[8];
        write_u32(reply, pr->end_pos - pr->start_pos);
        write_u32(reply + 4, pr->end_pos);
        send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
        free_pending_read(pr);
      }
      break;
    }

    case 0x0c: // RWRITE - write file data (initiates w/d protocol)
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + amount(4)
      // This is a request to receive 'amount' bytes starting at 'offset'
      // We need to send 'w' packets to request data, then receive 'd' packets
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t offset = read_u32(buf + 12);
      uint32_t amount = read_u32(buf + 16);

      sfs_log(SFS_LOG_DEBUG, "RWRITE: handle=%d offset=%u amount=%u", hid,
              offset, amount);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        // Broadcast any pending dead handles to all known clients
        broadcast_deadhandles(net, handles);
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // If amount is 0, nothing to do
      if (amount == 0) {
        send_r_pkt(net, rid, NULL, 0, addr, port);
        break;
      }

      // Allocate pending write state
      pending_write_t *pw = alloc_pending_write();
      if (!pw) {
        send_err_pkt(net, rid, ENOMEM, addr, port);
        break;
      }

      pw->handle_id = hid;
      pw->start_pos = offset;
      pw->current_pos = offset;
      pw->end_pos = offset + amount;
      pw->rid[0] = rid[0];
      pw->rid[1] = rid[1];
      pw->rid[2] = rid[2];
      strncpy(pw->addr, addr, sizeof(pw->addr) - 1);
      pw->addr[sizeof(pw->addr) - 1] = '\0';
      pw->port = port;

      // Request first chunk of data
      // Positions sent to client are relative to start_pos
      uint32_t chunk = (amount < WRITE_CHUNK_SIZE) ? amount : WRITE_CHUNK_SIZE;
      send_w_pkt(net, rid, 0, chunk, addr, port);
      break;
    }

    case 0x0d: // RREADDIR - read directory entries
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + count(4) =
      // 20 bytes
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t start_entry = read_u32(buf + 12);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->type != SFS_HANDLE_DIR || !h->path) {
        send_err_pkt(net, rid, ENOTDIR, addr, port);
        break;
      }

      send_readdir_response(net, rid, h->dir_entries, h->dir_entry_count,
                            hid, (size_t)start_entry, addr, port);
      break;
    }

    case 0x0f: // RSETLENGTH - set file length
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + length(4) = 16 bytes
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t new_len = read_u32(buf + 12);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (ftruncate(h->fd, (off_t)new_len) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      // Reply with the new length
      unsigned char reply[4];
      write_u32(reply, new_len);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x10: // RSETINFO - set load/exec addresses (filetype + date)
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + load(4) + exec(4) = 20
      // bytes
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t load_addr = read_u32(buf + 12);
      uint32_t exec_addr = read_u32(buf + 16);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // Update handle's stored load/exec addresses
      h->load_addr = load_addr;
      h->exec_addr = exec_addr;

      // Extract filetype and rename file with ,xxx suffix (files only, not
      // directories)
      uint32_t new_ftype = 0;
      if ((load_addr & 0xFFF00000) == 0xFFF00000) {
        new_ftype = (load_addr >> 8) & 0xFFF;

        // Only rename files, not directories (directories don't need ,xxx
        // suffix)
        if (h->path[0] && h->type == SFS_HANDLE_FILE) {
          char new_path[1024];
          sfs_append_type_suffix(h->path, new_ftype, new_path,
                                 sizeof(new_path));
          if (strcmp(h->path, new_path) != 0) {
            sfs_log(SFS_LOG_DEBUG, "RSETINFO: attempting rename '%s' -> '%s'",
                    h->path, new_path);
            int rename_err = 0;

#ifdef _WIN32
            // Windows cannot rename open files. Close, rename, reopen.
            int old_fd = h->fd;
            close(old_fd);
            h->fd = -1; // Mark invalid in case of failure

            // Ensure target doesn't exist (Windows rename fails only if target
            // exists)
            unlink(new_path);

            if (rename(h->path, new_path) == 0) {
              // Rename succeeded
              // Re-open with saved flags
              int flags = h->open_flags;
              // If we don't have flags (old handle), guess RDWR
              if (flags == 0)
                flags = O_RDWR;

              // Filter out creation flags
              flags &= ~(O_CREAT | O_EXCL | O_TRUNC);

              flags |= O_BINARY;

              int new_fd = open(new_path, flags);
              if (new_fd >= 0) {
                h->fd = new_fd;
                char *np = realloc(h->path, strlen(new_path) + 1);
                if (!np) {
                  close(h->fd); h->fd = -1;
                  rename_err = ENOMEM;
                } else {
                  h->path = np; strcpy(h->path, new_path);
                }
                // Update open_flags to reflect the new state (Text/Binary)
                h->open_flags = flags;

                sfs_log(SFS_LOG_DEBUG, "RSETINFO: renamed and reopened '%s'",
                        new_path);
              } else {
                sfs_log(SFS_LOG_ERROR,
                        "RSETINFO: renamed but failed to reopen '%s': %s",
                        new_path, strerror(errno));
                // Handle is effectively dead/closed.
                rename_err = errno;
              }
            } else {
              // Rename failed
              rename_err = errno;
              sfs_log(SFS_LOG_ERROR, "RSETINFO: rename failed '%s' -> '%s': %s",
                      h->path, new_path, strerror(errno));
              // Try to re-open original
              int flags = h->open_flags ? h->open_flags : (O_RDWR | O_BINARY);
              flags &= ~(O_CREAT | O_EXCL | O_TRUNC);
              h->fd = open(h->path, flags);
            }
#else
            // POSIX - just rename
            if (rename(h->path, new_path) == 0) {
              // Update handle's stored path
              char *np = realloc(h->path, strlen(new_path) + 1);
              if (!np) {
                rename_err = ENOMEM;
              } else {
                h->path = np; strcpy(h->path, new_path);
              }
              sfs_log(SFS_LOG_DEBUG, "RSETINFO: renamed to '%s'", new_path);
            } else {
              rename_err = errno;
              sfs_log(SFS_LOG_ERROR, "RSETINFO: rename failed '%s' -> '%s': %s",
                      h->path, new_path, strerror(errno));
            }
#endif
            if (rename_err) {
              send_err_pkt(net, rid, rename_err, addr, port);
              break;
            }
          }
        }

        // Update file mtime from timestamp
        uint64_t riscos_time = ((uint64_t)(load_addr & 0xFF) << 32) | exec_addr;
        // RISC OS epoch is 1900-01-01, Unix is 1970-01-01
        // Difference is 2208988800 seconds = 220898880000 centiseconds
        if (riscos_time >= 220898880000ULL) {
          time_t unix_time = (time_t)((riscos_time - 220898880000ULL) / 100);
          struct utimbuf ut;
          ut.actime = unix_time;
          ut.modtime = unix_time;
          if (h->path[0]) {
            utime(h->path, &ut);
          }
        }
      }

      // Return FileDesc
      struct stat st;
      if (h->path[0] && stat(h->path, &st) == 0) {
        unsigned char reply[20];
        build_filedesc(reply, &st, new_ftype);
        send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      } else {
        // Can't stat, just acknowledge
        send_r_pkt(net, rid, NULL, 0, addr, port);
      }
      break;
    }

    case 0x09: // RRENAME
    {
      // Format: cmd(1) + rid(3) + code(4) + new_len(4) + handle(4) + old_path
      // Client then sends a 'D' packet carrying the new name of length new_len
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }

      uint32_t new_name_len = read_u32(buf + 8);
      const char *old_path_str = (len > 16) ? (const char *)(buf + 16) : "";

      if (!new_name_len || new_name_len >= 512) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }

      pending_rename_t *existing = find_pending_rename(rid, addr, port);
      if (existing) {
        // Duplicate A-pkt for the same pending rename; keep waiting and
        // re-prompt the client for the new name.
        sfs_log(SFS_LOG_INFO,
                "RRENAME: duplicate A-pkt rid=%02x%02x%02x old='%s'", rid[0],
                rid[1], rid[2], existing->old_host_path);
        send_w_pkt(net, rid, 0, existing->expected_len, existing->addr,
                   existing->port);
        break;
      }

      if (resolve_path(cfg, old_path_str, host_path, sizeof(host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }

      // Allow renaming files that already have a ,xxx suffix on disk
      char actual_old_path[1024];
      if (find_file_with_suffix(host_path, actual_old_path,
                                sizeof(actual_old_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        break;
      }
      int old_suffix_type = sfs_filetype_from_suffix(actual_old_path);

      pending_rename_t *pr = alloc_pending_rename();
      if (!pr) {
        send_err_pkt(net, rid, EBUSY, addr, port);
        break;
      }
      memcpy(pr->rid, rid, sizeof(pr->rid));
      pr->expected_len = new_name_len;
      strncpy(pr->old_host_path, actual_old_path,
              sizeof(pr->old_host_path) - 1);
      pr->old_host_path[sizeof(pr->old_host_path) - 1] = '\0';
      strncpy(pr->addr, addr, sizeof(pr->addr) - 1);
      pr->addr[sizeof(pr->addr) - 1] = '\0';
      pr->port = port;
      pr->suffix_type = old_suffix_type;
      pr->started_at = time(NULL);

      sfs_log(SFS_LOG_DEBUG,
              "RRENAME: awaiting D-pkt rid=%02x%02x%02x old='%s' new_len=%u",
              rid[0], rid[1], rid[2], old_path_str, new_name_len);

            // Request the new name from the client (same w/d scheme as writes)
            send_w_pkt(net, rid, 0, new_name_len, addr, port);
      // Do not reply yet; we will send the result when the D packet arrives
      break;
    }

    case 0x0e: // RENSURE - ensure file size allocated
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + size(4) = 16 bytes
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t ensure_size = read_u32(buf + 12);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // Get current size
      struct stat st;
      if (fstat(h->fd, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      // Only extend if needed
      if ((off_t)ensure_size > st.st_size) {
        if (ftruncate(h->fd, (off_t)ensure_size) != 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
      }

      // Reply with the length
      unsigned char reply[4];
      write_u32(reply, ensure_size);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x11: // RGETSEQPTR - get sequential file pointer
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) = 12 bytes
      int hid = (int)handle;

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      off_t pos = lseek(h->fd, 0, SEEK_CUR);
      if (pos < 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      unsigned char reply[4];
      write_u32(reply, (uint32_t)pos);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x12: // RSETSEQPTR - set sequential file pointer
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + pos(4) = 16 bytes
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t new_pos = read_u32(buf + 12);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      off_t pos = lseek(h->fd, (off_t)new_pos, SEEK_SET);
      if (pos < 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      unsigned char reply[4];
      write_u32(reply, (uint32_t)pos);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x14: // RZERO - write zeros to file
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + length(4) =
      // 20 bytes
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t offset = read_u32(buf + 12);
      uint32_t zero_len = read_u32(buf + 16);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 || !h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // Guard against offset + zero_len overflow before using the sum.
      if (zero_len > 0xFFFFFFFFu - offset) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      uint32_t new_length = offset + zero_len;
      struct stat st;
      if (fstat(h->fd, &st) == 0 && (off_t)new_length > st.st_size) {
        if (ftruncate(h->fd, (off_t)new_length) != 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
      }

      // Reply with the new length
      unsigned char reply[4];
      write_u32(reply, new_length);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    default:
      sfs_log(SFS_LOG_DEBUG, "Unsupported A-cmd code %u", code);
      send_err_pkt(net, rid, ENOSYS, addr, port);
      break;
    }
    return 0;
  }

  // 'B' command - file operations with extended format
  // Format: cmd(1) + rid(3) + code(4) + handle(4) + extra(4) + path...
  if (cmd == 'B') {
    if (len < 16) {
      send_err_pkt(net, rid, EINVAL, addr, port);
      return 0;
    }
    uint32_t code = read_u32(buf + 4);
    uint32_t handle = read_u32(buf + 8);
    uint32_t extra = read_u32(buf + 12);
    const char *path = (len > 16) ? (const char *)(buf + 16) : "";

    sfs_log(SFS_LOG_PROTOCOL, "B-cmd code=%u handle=%u extra=%u path='%s'",
            code, handle, extra, path);

    // Path-based B commands need the same authentication as their A-command
    // counterparts. Without this, ROPENDIR here listed a protected share to
    // anyone who asked, because the check below lived only in the A branch.
    const int b_has_path = (code == 0x03);
    if (b_has_path && path[0] && !check_share_auth(cfg, auth, addr, path)) {
      send_err_pkt(net, rid, EACCES, addr, port);
      return 0;
    }

    switch (code) {
    case 0x03: // ROPENDIR
    {
      sfs_log(SFS_LOG_DEBUG, "ROPENDIR: attempting resolve_path for '%s'",
              path);
      if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
        sfs_log(SFS_LOG_DEBUG,
                "ROPENDIR: resolve_path failed, trying share match");
        // Path is a share name - check if it's a valid share
        int share_idx = -1;
        for (size_t i = 0; i < cfg->share_count; ++i) {
          if (cfg->shares[i].name &&
              strcasecmp(cfg->shares[i].name, path) == 0) {
            share_idx = (int)i;
            break;
          }
        }
        if (share_idx >= 0) {
          snprintf(host_path, sizeof(host_path), "%s",
                   cfg->shares[share_idx].path);
          sfs_log(SFS_LOG_DEBUG, "ROPENDIR: share match found, path='%s'",
                  host_path);
        } else {
          sfs_log(SFS_LOG_DEBUG, "ROPENDIR: no share match, sending ENOENT");
          send_err_pkt(net, rid, ENOENT, addr, port);
          break;
        }
      }
      sfs_log(SFS_LOG_DEBUG, "ROPENDIR: host_path='%s'", host_path);
      struct stat st;
      if (stat(host_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        sfs_log(SFS_LOG_DEBUG, "ROPENDIR: stat failed or not a dir: errno=%d",
                errno);
        send_err_pkt(net, rid, ENOTDIR, addr, port);
        break;
      }
      sfs_log(SFS_LOG_DEBUG, "ROPENDIR: stat OK, is directory");
      int hid = 0, tok = 0;
      if (sfs_handles_add_ex(handles, SFS_HANDLE_DIR, -1, host_path, 0, 0, 0,
                             sfs_mode_to_attrs(st.st_mode), &hid, &tok) != 0) {
        sfs_log(SFS_LOG_DEBUG, "ROPENDIR: sfs_handles_add_ex failed");
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }
      sfs_handle_set_client(handles, hid, addr, port);
      sfs_log(SFS_LOG_DEBUG,
              "ROPENDIR: handle=%d, calling send_catalogue_response", hid);
      // Build sorted listing once; serve all RREADDIR pages from cache.
      populate_dir_listing(host_path, cfg, handles, hid);
      {
        sfs_handle *dh = NULL;
        sfs_handles_get(handles, hid, &dh);
        send_catalogue_response(net, rid,
                                dh ? dh->dir_entries : NULL,
                                dh ? dh->dir_entry_count : 0,
                                hid, addr, port);
      }
      break;
    }

    case 0x0b: // RREAD - read file data (B command format, returns S+B)
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + pos(4) + length(4) =
      // 20 bytes
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;
      uint32_t pos = extra; // extra contains position
      uint32_t rlen = read_u32(buf + 16);

      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0 ||
          !h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      if (pos == 0xFFFFFFFF) {
        // Sequential read: get current position
        off_t current = lseek(h->fd, 0, SEEK_CUR);
        if (current < 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
        pos = (uint32_t)current;
      } else {
        if (lseek(h->fd, (off_t)pos, SEEK_SET) < 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
      }

      // Limit read size
      if (rlen > 16384)
        rlen = 16384;
      unsigned char data[16384];
      ssize_t n = read(h->fd, data, rlen);
      if (n < 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      uint32_t new_pos = pos + (uint32_t)n;
      h->seq_ptr = new_pos;

      // Build S+B combined response
      // Header: S + rid + length(4) + trailer_len(4)
      // Data
      // Trailer: B + rid + length(4) + new_pos(4)
      size_t pkt_len = 4 + 4 + 4 + (size_t)n + 4 + 4 + 4;
      unsigned char *pkt = malloc(pkt_len);
      if (!pkt) {
        send_err_pkt(net, rid, ENOMEM, addr, port);
        break;
      }

      size_t off = 0;
      pkt[off++] = 'S';
      pkt[off++] = rid[0];
      pkt[off++] = rid[1];
      pkt[off++] = rid[2];
      write_u32(pkt + off, (uint32_t)n);
      off += 4;
      write_u32(pkt + off, 0x0c);
      off += 4; // trailer_len indicator
      memcpy(pkt + off, data, (size_t)n);
      off += (size_t)n;
      pkt[off++] = 'B';
      pkt[off++] = rid[0];
      pkt[off++] = rid[1];
      pkt[off++] = rid[2];
      write_u32(pkt + off, (uint32_t)n);
      off += 4;
      write_u32(pkt + off, new_pos);
      off += 4;

      sfs_net_sendto(net->rpc, pkt, off, addr, port);
      free(pkt);
      break;
    }

    case 0x0d: // RREADDIR (B command)
    {
      // Format: cmd(1) + rid(3) + code(4) + handle(4) + toggle(4) + count(4)
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      int hid = (int)handle;

      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->type != SFS_HANDLE_DIR || !h->path) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      // extra field is the start offset for pagination
      send_readdir_response(net, rid, h->dir_entries, h->dir_entry_count,
                            hid, (size_t)extra, addr, port);
      break;
    }

    default:
      sfs_log(SFS_LOG_DEBUG, "Unsupported B-cmd code %u", code);
      send_err_pkt(net, rid, ENOSYS, addr, port);
      break;
    }
    return 0;
  }

  // Handle other single-character commands
  // 'a' = handle-based operations
  if (cmd == 'a') {
    if (len < 12) {
      send_err_pkt(net, rid, EINVAL, addr, port);
      return 0;
    }
    uint32_t code = read_u32(buf + 4);
    int hid = (int)read_u32(buf + 8);

    sfs_log(SFS_LOG_PROTOCOL, "a-cmd code=%u handle=%d", code, hid);

    switch (code) {
    case 0x0a: // RCLOSE
    {
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (h->fd >= 0)
        close(h->fd);
      sfs_handles_close(handles, hid, h->token);
      send_r_pkt(net, rid, NULL, 0, addr, port);
      break;
    }

    case 0x0b: // RREAD
    {
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int off = read_u32(buf + 12);
      unsigned int rlen = read_u32(buf + 16);

      sfs_log(SFS_LOG_DEBUG, "A-cmd RREAD: handle=%d offset=%u len=%u", hid,
              off, rlen);

      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // Allocate pending read
      pending_read_t *pr = alloc_pending_read();
      if (!pr) {
        send_err_pkt(net, rid, EMFILE, addr, port);
        break;
      }

      pr->handle_id = hid;
      pr->start_pos = off;
      pr->current_pos = off;
      pr->end_pos = off + rlen;
      pr->state = SFS_READ_STATE_WAIT_DATA_ACK;
      pr->rid[0] = rid[0];
      pr->rid[1] = rid[1];
      pr->rid[2] = rid[2];
      strncpy(pr->addr, addr, sizeof(pr->addr) - 1);
      pr->addr[sizeof(pr->addr) - 1] = '\0';
      pr->port = port;

      // Send first chunk
      uint32_t amount = (rlen < READ_CHUNK_SIZE) ? rlen : READ_CHUNK_SIZE;

      if (lseek(h->fd, (off_t)pr->current_pos, SEEK_SET) < 0) {
        free_pending_read(pr);
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      unsigned char data[READ_CHUNK_SIZE];
      ssize_t n = read(h->fd, data, amount);
      if (n < 0) {
        free_pending_read(pr);
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }

      // Send D packet with offset relative to start (should be 0 for first
      // chunk)
      send_d_pkt_with_offset(net, rid, pr->current_pos - pr->start_pos, data,
                             (size_t)n, addr, port);

      pr->current_pos += (uint32_t)n;

      // If completed immediately (small file), send R packet too
      if (pr->current_pos >= pr->end_pos) {
        unsigned char reply[8];
        write_u32(reply, pr->end_pos - pr->start_pos);
        write_u32(reply + 4, pr->end_pos);
        send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
        free_pending_read(pr);
      }
      break;
    }

    case 0x0c: // RWRITE (a-cmd format, initiates w/d protocol)
    {
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int off = read_u32(buf + 12);
      unsigned int amount = read_u32(buf + 16);

      sfs_log(SFS_LOG_DEBUG, "a-cmd RWRITE: handle=%d offset=%u amount=%u", hid,
              off, amount);

      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      // If amount is 0, nothing to do
      if (amount == 0) {
        send_r_pkt(net, rid, NULL, 0, addr, port);
        break;
      }

      // Allocate pending write state
      pending_write_t *pw = alloc_pending_write();
      if (!pw) {
        send_err_pkt(net, rid, ENOMEM, addr, port);
        break;
      }

      pw->handle_id = hid;
      pw->start_pos = off;
      pw->current_pos = off;
      pw->end_pos = off + amount;
      pw->rid[0] = rid[0];
      pw->rid[1] = rid[1];
      pw->rid[2] = rid[2];
      strncpy(pw->addr, addr, sizeof(pw->addr) - 1);
      pw->addr[sizeof(pw->addr) - 1] = '\0';
      pw->port = port;

      // Request first chunk of data
      uint32_t chunk = (amount < WRITE_CHUNK_SIZE) ? amount : WRITE_CHUNK_SIZE;
      send_w_pkt(net, rid, 0, chunk, addr, port);
      break;
    }

    case 0x0d: // RREADDIR
    {
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int start = read_u32(buf + 12);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->type != SFS_HANDLE_DIR || !h->path) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      send_readdir_response(net, rid, h->dir_entries, h->dir_entry_count,
                            hid, (size_t)start, addr, port);
      break;
    }

    case 0x0e: // RENSURE - ensure file size
    {
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int ensure_size = read_u32(buf + 12);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      struct stat st;
      if (fstat(h->fd, &st) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      if ((off_t)ensure_size > st.st_size) {
        if (ftruncate(h->fd, (off_t)ensure_size) != 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
      }
      unsigned char reply[4];
      write_u32(reply, ensure_size);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x0f: // RSETLENGTH
    {
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int newlen = read_u32(buf + 12);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      if (ftruncate(h->fd, (off_t)newlen) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        break;
      }
      h->length = newlen;
      unsigned char reply[4];
      write_u32(reply, newlen);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x10: // RSETINFO (set load/exec addresses)
    {
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      uint32_t load = read_u32(buf + 12);
      uint32_t exec = read_u32(buf + 16);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      h->load_addr = load;
      h->exec_addr = exec;
      // Update file mtime from exec address
      if (h->path) {
        uint64_t cs = ((uint64_t)(load & 0xFF) << 32) | exec;
        time_t t = sfs_time_from_riscos(cs);
        sfs_set_mtime(h->path, t);
      }
      send_r_pkt(net, rid, NULL, 0, addr, port);
      break;
    }

    case 0x11: // RGETSEQPTR
    {
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      unsigned char reply[4];
      write_u32(reply, h->seq_ptr);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x12: // RSETSEQPTR
    {
      if (len < 16) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      uint32_t ptr = read_u32(buf + 12);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }
      h->seq_ptr = ptr;
      if (h->fd >= 0)
        lseek(h->fd, (off_t)ptr, SEEK_SET);
      send_r_pkt(net, rid, NULL, 0, addr, port);
      break;
    }

    case 0x14: // RZERO - write zeros
    {
      if (len < 20) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      unsigned int offset = read_u32(buf + 12);
      unsigned int zero_len = read_u32(buf + 16);
      // Bound to the opening client: a handle id alone is not authority.
      sfs_handle *h = NULL;
      if (sfs_handles_get_for_client(handles, hid, addr, port, &h) != 0)
        h = NULL;
      if (!h || h->fd < 0) {
        send_err_pkt(net, rid, EBADF, addr, port);
        break;
      }

      if (zero_len > 0xFFFFFFFFu - offset) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        break;
      }
      uint32_t new_length = offset + zero_len;
      struct stat st;
      if (fstat(h->fd, &st) == 0 && (off_t)new_length > st.st_size) {
        if (ftruncate(h->fd, (off_t)new_length) != 0) {
          send_err_pkt(net, rid, errno, addr, port);
          break;
        }
      }
      unsigned char reply[4];
      write_u32(reply, new_length);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    case 0x15: // RVERSION
    {
      unsigned char reply[2];
      reply[0] = 0x02; // Version 2
      reply[1] = 0x00;
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    default:
      sfs_log(SFS_LOG_DEBUG, "Unsupported a-cmd code %u", code);
      send_err_pkt(net, rid, ENOSYS, addr, port);
      break;
    }
    return 0;
  }

  // 'F' command - simple queries (RDEADHANDLES, RVERSION, etc.)
  // Format: cmd(1) + rid(3) + code(4) + handle(4)
  if (cmd == 'F') {
    if (len < 12) {
      send_err_pkt(net, rid, EINVAL, addr, port);
      return 0;
    }
    uint32_t code = read_u32(buf + 4);
    uint32_t handle = read_u32(buf + 8);
    (void)handle;

    sfs_log(SFS_LOG_PROTOCOL, "F-cmd code=%u handle=%u", code, handle);

    switch (code) {
    case 0x13: // RDEADHANDLES - client asking about dead handles
    {
      size_t dead_count = 0;
      const int *dead = sfs_handles_get_dead(handles, &dead_count);

      // Cap to a reasonable maximum (protocol handletable is 240/4 entries)
      const size_t max_dead = 60; // 60 * 4 bytes = 240 bytes like original
      if (dead_count > max_dead)
        dead_count = max_dead;

      size_t payload_len = (dead_count + 1) * sizeof(uint32_t);
      unsigned char *reply = (unsigned char *)malloc(payload_len);
      if (!reply) {
        send_err_pkt(net, rid, ENOMEM, addr, port);
        break;
      }

      write_u32(reply, (uint32_t)dead_count);
      for (size_t i = 0; i < dead_count; ++i) {
        write_u32(reply + 4 + i * 4, (uint32_t)dead[i]);
      }

      send_r_pkt(net, rid, reply, (uint16_t)payload_len, addr, port);
      free(reply);

      // Clear after reporting so the list doesn't grow without bound
      sfs_handles_clear_dead(handles);
      break;
    }

    case 0x15: // RVERSION
    {
      unsigned char reply[4];
      write_u32(reply, 0x00000002); // Version 2
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      break;
    }

    default:
      sfs_log(SFS_LOG_DEBUG, "Unsupported F-cmd code %u", code);
      send_err_pkt(net, rid, ENOSYS, addr, port);
      break;
    }
    return 0;
  }

  // 'd' command - data packet from client (response to our 'w' request)
  // Format: d + rid(3) + pos(4) + data...
  if (cmd == 'd') {
    if (len < 8) {
      return 0; // Too short, ignore
    }

    uint32_t rel_pos = read_u32(buf + 4);
    const unsigned char *data = buf + 8;
    size_t data_len = len - 8;

    sfs_log(SFS_LOG_DEBUG, "d-pkt: rel_pos=%u data_len=%zu", rel_pos, data_len);

    // Handle pending RRENAME first (client uses the lowercase 'd' channel
    // for the new name payload). This avoids misclassifying it as a write.
    pending_rename_t *prn = find_pending_rename(rid, addr, port);
    if (prn) {
      if (data_len == 0) {
        // Client is waiting; re-request the full name payload
        send_w_pkt(net, rid, rel_pos, prn->expected_len,
                   prn->addr, prn->port);
        return 0;
      }

      if (rel_pos != 0) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        clear_pending_rename(prn);
        return 0;
      }

      // Some clients include an extra 4-byte zero word before the name
      // (data_len == expected_len, but first word is 0). Skip it when present
      // so the name string is parsed correctly.
      const unsigned char *name_ptr = data;
      size_t name_len = data_len;
      if (name_len >= 4 && name_ptr[0] == 0 && name_ptr[1] == 0 &&
          name_ptr[2] == 0 && name_ptr[3] == 0 &&
          name_len == prn->expected_len) {
        name_ptr += 4;
        name_len -= 4;
      }

      if (name_len == 0) {
        send_err_pkt(net, rid, EINVAL, addr, port);
        clear_pending_rename(prn);
        return 0;
      }

      size_t copy_len = name_len;
      if (copy_len >= sizeof(host_path))
        copy_len = sizeof(host_path) - 1;

      char new_ro_path[1024];
      memcpy(new_ro_path, name_ptr, copy_len);
      new_ro_path[copy_len] = '\0';

      char new_host_path[1024];
      if (resolve_path(cfg, new_ro_path, new_host_path,
                      sizeof(new_host_path)) != 0) {
        send_err_pkt(net, rid, ENOENT, addr, port);
        clear_pending_rename(prn);
        return 0;
      }

      // Preserve existing ,xxx suffix when renaming files stored with
      // RISC OS filetype suffixes on disk.
      if (prn->suffix_type >= 0 &&
          sfs_filetype_from_suffix(new_host_path) < 0) {
        char suffixed[1024];
        sfs_append_type_suffix(new_host_path,
                               (uint32_t)prn->suffix_type,
                               suffixed, sizeof(suffixed));
        strncpy(new_host_path, suffixed, sizeof(new_host_path) - 1);
        new_host_path[sizeof(new_host_path) - 1] = '\0';
      }

      if (safe_rename_cross(prn->old_host_path, new_host_path) != 0) {
        send_err_pkt(net, rid, errno, addr, port);
        clear_pending_rename(prn);
        return 0;
      }

      sfs_log(SFS_LOG_DEBUG, "RRENAME: '%s' -> '%s'",
              prn->old_host_path, new_host_path);
      send_r_pkt(net, rid, NULL, 0, prn->addr, prn->port);
      clear_pending_rename(prn);
      return 0;
    }

    // Find pending write for this reply ID
    pending_write_t *pw = find_pending_write(rid, addr, port);
    if (!pw) {
      sfs_log(SFS_LOG_DEBUG, "d-pkt: no pending write found for rid");
      return 0;
    }

    // If data length is 0, it means client might be busy or waiting.
    // Instead of aborting immediately, we retry with a backoff.
    if (data_len == 0) {
      if (pw->retries >= 1000) {
        sfs_log(SFS_LOG_ERROR, "d-pkt: timeout waiting for data (received 0 "
                               "bytes 1000 times), aborting write");
        send_err_pkt(net, pw->rid, EIO, pw->addr, pw->port);
        free_pending_write(pw);
        return 0;
      }
      pw->retries++;
      // No sleep here: this path is driven by the client's own packets, and
      // the server is single-threaded, so sleeping stalled every other client
      // for up to ten seconds.

      // Resend 'w' packet to request the expected chunk
      uint32_t expected_rel = pw->current_pos - pw->start_pos;
      uint32_t remaining = pw->end_pos - pw->current_pos;
      uint32_t chunk =
          (remaining < WRITE_CHUNK_SIZE) ? remaining : WRITE_CHUNK_SIZE;

      // Log occasionally to avoid spam
      if (pw->retries % 100 == 0) {
        sfs_log(SFS_LOG_DEBUG, "d-pkt: received 0 bytes, retrying (%d/1000)",
                pw->retries);
      }

      send_w_pkt(net, pw->rid, expected_rel, expected_rel + chunk, pw->addr,
                 pw->port);
      return 0;
    }
    // Data received, reset retries
    pw->retries = 0;

    // Get the handle
    sfs_handle *h = NULL;
    if (sfs_handles_get(handles, pw->handle_id, &h) != 0 || !h || h->fd < 0) {
      sfs_log(SFS_LOG_DEBUG, "d-pkt: handle %d invalid", pw->handle_id);
      free_pending_write(pw);
      return 0;
    }

    // Check for sequential write
    uint32_t expected_rel = pw->current_pos - pw->start_pos;
    if (rel_pos != expected_rel) {
      sfs_log(SFS_LOG_ERROR,
              "d-pkt: Non-sequential write. rel_pos=%u expected=%u", rel_pos,
              expected_rel);

      // If gap detected, re-request the expected chunk immediately
      if (rel_pos > expected_rel) {
        uint32_t remaining = pw->end_pos - pw->current_pos;
        uint32_t chunk =
            (remaining < WRITE_CHUNK_SIZE) ? remaining : WRITE_CHUNK_SIZE;
        send_w_pkt(net, pw->rid, expected_rel, expected_rel + chunk, pw->addr,
                   pw->port);
      }
      return 0;
    }

    // Calculate absolute position and write data
    uint32_t abs_pos = pw->start_pos + rel_pos;
    if (lseek(h->fd, (off_t)abs_pos, SEEK_SET) < 0) {
      sfs_log(SFS_LOG_DEBUG, "d-pkt: lseek failed");
      send_err_pkt(net, pw->rid, errno, pw->addr, pw->port);
      free_pending_write(pw);
      return 0;
    }

    ssize_t n = write(h->fd, data, data_len);
    if (n < 0) {
      sfs_log(SFS_LOG_DEBUG, "d-pkt: write failed");
      send_err_pkt(net, pw->rid, errno, pw->addr, pw->port);
      free_pending_write(pw);
      return 0;
    }

    pw->current_pos = abs_pos + (uint32_t)n;
    h->seq_ptr = pw->current_pos;
    if (h->seq_ptr > h->length)
      h->length = h->seq_ptr;

    sfs_log(SFS_LOG_DEBUG,
            "d-pkt: wrote %zd bytes at %u, current_pos=%u end_pos=%u", n,
            abs_pos, pw->current_pos, pw->end_pos);

    // Check if we need more data
    if (pw->current_pos < pw->end_pos) {
      // Request next chunk
      uint32_t rel_current = pw->current_pos - pw->start_pos;
      uint32_t remaining = pw->end_pos - pw->current_pos;
      uint32_t chunk =
          (remaining < WRITE_CHUNK_SIZE) ? remaining : WRITE_CHUNK_SIZE;
      send_w_pkt(net, pw->rid, rel_current, rel_current + chunk, pw->addr,
                 pw->port);
    } else {
      // Transfer complete
      sfs_log(SFS_LOG_DEBUG, "d-pkt: transfer complete, sending R-pkt");
      send_r_pkt(net, pw->rid, NULL, 0, pw->addr, pw->port);
      free_pending_write(pw);
    }
    return 0;
  }

  // 'D' command - payload carrying new name for a pending RRENAME
  // Format: D + rid(3) + new_name bytes (length announced in prior A-packet)
  if (cmd == 'D') {
    sfs_log(SFS_LOG_INFO, "D-pkt: len=%zu rid=%02x%02x%02x", len,
            rid[0], rid[1], rid[2]);
    size_t payload_len = (len > 4) ? (len - 4) : 0;

    pending_rename_t *prn = find_pending_rename(rid, addr, port);
    if (!prn) {
      sfs_log(SFS_LOG_INFO, "D-pkt: no pending rename for rid=%02x%02x%02x",
              rid[0], rid[1], rid[2]);
      return 0;
    }

    if (payload_len < prn->expected_len) {
      send_err_pkt(net, rid, EINVAL, addr, port);
      clear_pending_rename(prn);
      return 0;
    }

    size_t copy_len = prn->expected_len;
    if (copy_len >= 512)
      copy_len = 511;

    char new_ro_path[1024];
    memcpy(new_ro_path, buf + 4, copy_len);
    new_ro_path[copy_len] = '\0';

    char new_host_path[1024];
    if (resolve_path(cfg, new_ro_path, new_host_path, sizeof(new_host_path)) !=
        0) {
      send_err_pkt(net, rid, ENOENT, addr, port);
      clear_pending_rename(prn);
      return 0;
    }

    if (prn->suffix_type >= 0 &&
        sfs_filetype_from_suffix(new_host_path) < 0) {
      char suffixed[1024];
      sfs_append_type_suffix(new_host_path,
                             (uint32_t)prn->suffix_type, suffixed,
                             sizeof(suffixed));
      strncpy(new_host_path, suffixed, sizeof(new_host_path) - 1);
      new_host_path[sizeof(new_host_path) - 1] = '\0';
    }

    if (safe_rename_cross(prn->old_host_path, new_host_path) != 0) {
      send_err_pkt(net, rid, errno, addr, port);
      clear_pending_rename(prn);
      return 0;
    }

    sfs_log(SFS_LOG_DEBUG, "RRENAME: '%s' -> '%s'", prn->old_host_path,
            new_host_path);
    send_r_pkt(net, rid, NULL, 0, prn->addr, prn->port);
    clear_pending_rename(prn);
    return 0;
  }

  // 'r' command - acknowledgement packet from client for RREAD
  if (cmd == 'r') {
    return sfs_rpc_handle_r(buf, len, addr, port, net, handles);
  }

  // Unknown command
  sfs_log(SFS_LOG_DEBUG, "Unsupported cmd '%c' (%u)", cmd, cmd);
  send_err_pkt(net, rid, ENOSYS, addr, port);
  return 0;
}

// Handle 'r' packet (acknowledgement from client for RREAD data)
int sfs_rpc_handle_r(const unsigned char *buf, size_t len, const char *addr,
                     unsigned short port, sfs_net *net,
                     sfs_handle_table *handles) {
  // Format: r + rid(3) + ...
  if (len < 4)
    return 0;

  unsigned char rid[3] = {buf[1], buf[2], buf[3]};
  // Low level protocol logging only
  // sfs_log(SFS_LOG_DEBUG, "r-pkt from %s:%u", addr, port);

  pending_read_t *pr = find_pending_read(rid, addr, port);
  if (!pr) {
    // Can happen if we resent R or client is delayed
    return 0;
  }

  sfs_handle *h = NULL;
  if (sfs_handles_get(handles, pr->handle_id, &h) != 0 || !h || h->fd < 0) {
    sfs_log(SFS_LOG_DEBUG, "r-pkt: handle %d invalid", pr->handle_id);
    free_pending_read(pr);
    return 0;
  }

  if (pr->state == SFS_READ_STATE_WAIT_DATA_ACK) {
    // We received ACK for the data packet.
    sfs_log(SFS_LOG_DEBUG, "RREAD: Done Data %u/%u. Sending Status.",
            pr->current_pos - pr->start_pos, pr->end_pos - pr->start_pos);

    // Send Status Packet (D header with current offset, no data)
    send_d_pkt_with_offset(net, rid, pr->current_pos - pr->start_pos, NULL, 0,
                           addr, port);

    if (pr->current_pos >= pr->end_pos) {
      sfs_log(SFS_LOG_DEBUG, "RREAD: Transfer Complete. Sending R.");
      // Transfer complete
      unsigned char reply[8];
      write_u32(reply, pr->end_pos - pr->start_pos);
      write_u32(reply + 4, pr->end_pos);
      send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
      free_pending_read(pr);
    } else {
      // Wait for client to request next chunk
      pr->state = SFS_READ_STATE_WAIT_STATUS_ACK;
    }
  } else if (pr->state == SFS_READ_STATE_WAIT_STATUS_ACK) {
    // We received ACK for status packet.
    // Client sends next requested range in this packet.
    // Format: r + rid + pos(4) + end(4) (relative to start)
    if (len < 12) {
      sfs_log(SFS_LOG_DEBUG, "r-pkt: short status ack");
      free_pending_read(pr);
      return 0;
    }

    uint32_t rel_pos = read_u32(buf + 4);
    uint32_t rel_end = read_u32(buf + 8);

    sfs_log(SFS_LOG_DEBUG, "RREAD: Client Request: RelPos=%u RelEnd=%u",
            rel_pos, rel_end);

    uint32_t next_pos = pr->start_pos + rel_pos;
    uint32_t chunk_end = pr->start_pos + rel_end;

    // Sanity check
    if (next_pos >= pr->end_pos) {
      sfs_log(SFS_LOG_DEBUG, "RREAD: Client requested beyond end.");
      free_pending_read(pr);
      return 0;
    }

    // Update current position to what client asked for
    pr->current_pos = next_pos;

    // Determine chunk size
    uint32_t amount = chunk_end - next_pos;
    if (amount > READ_CHUNK_SIZE)
      amount = READ_CHUNK_SIZE;
    // Also cap at total file end
    if (pr->current_pos + amount > pr->end_pos)
      amount = pr->end_pos - pr->current_pos;

    sfs_log(SFS_LOG_DEBUG, "RREAD: Sending Data: Offset=%u Len=%u",
            pr->current_pos - pr->start_pos, amount);

    // Read next chunk
    if (lseek(h->fd, (off_t)pr->current_pos, SEEK_SET) < 0) {
      free_pending_read(pr);
      return 0;
    }

    unsigned char data[READ_CHUNK_SIZE];
    ssize_t n = read(h->fd, data, amount);
    if (n < 0) {
      free_pending_read(pr);
      return 0;
    }

    // Send Data Packet
    send_d_pkt_with_offset(net, rid, pr->current_pos - pr->start_pos, data,
                           (size_t)n, addr, port);

    pr->current_pos += (uint32_t)n;
    pr->state = SFS_READ_STATE_WAIT_DATA_ACK;
  }

  return 0;
}
