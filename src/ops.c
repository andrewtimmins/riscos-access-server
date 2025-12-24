// RISC OS Access/ShareFS Server - File Operations (full impl)
// Author: Andrew Timmins
// License: GPL-3.0-only

#include "ops.h"
#include "log.h"
#include "riscos.h"
#include "platform.h"
#include "accessplus.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

// Maximum pending write transfers
#define MAX_PENDING_WRITES 32
#define WRITE_CHUNK_SIZE 8192

// Pending write transfer state
typedef struct {
    int active;
    int handle_id;
    uint32_t start_pos;       // Original start position from client
    uint32_t current_pos;     // Current position in file
    uint32_t end_pos;         // End position (start + amount)
    unsigned char rid[3];     // Reply ID to use
    char addr[64];            // Client address
    unsigned short port;      // Client port
} pending_write_t;

static pending_write_t pending_writes[MAX_PENDING_WRITES];

static pending_write_t *find_pending_write(const unsigned char *rid) {
    for (int i = 0; i < MAX_PENDING_WRITES; i++) {
        if (pending_writes[i].active && 
            pending_writes[i].rid[0] == rid[0] &&
            pending_writes[i].rid[1] == rid[1] &&
            pending_writes[i].rid[2] == rid[2]) {
            return &pending_writes[i];
        }
    }
    return NULL;
}

static pending_write_t *alloc_pending_write(void) {
    for (int i = 0; i < MAX_PENDING_WRITES; i++) {
        if (!pending_writes[i].active) {
            pending_writes[i].active = 1;
            return &pending_writes[i];
        }
    }
    return NULL;
}

static void free_pending_write(pending_write_t *pw) {
    if (pw) pw->active = 0;
}

static unsigned int read_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void write_u32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// Create parent directories for a path (like mkdir -p)
static int mkpath(const char *path, mode_t mode) {
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    strcpy(tmp, path);
    
    // Remove trailing slash
    if (tmp[len - 1] == '/') tmp[--len] = '\0';
    
    // Create each component
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, mode);
}

// Send 'w' packet to request data from client
static void send_w_pkt(ras_net *net, const unsigned char *rid, uint32_t rel_pos, uint32_t rel_end, const char *addr, unsigned short port) {
    // Format: w + rid(3) + pos(4) + zero(4) + end(4)
    unsigned char pkt[16];
    pkt[0] = 'w';
    pkt[1] = rid[0];
    pkt[2] = rid[1];
    pkt[3] = rid[2];
    write_u32(pkt + 4, rel_pos);
    write_u32(pkt + 8, 0);
    write_u32(pkt + 12, rel_end);
    ras_log(RAS_LOG_DEBUG, "Sending w-pkt: rel_pos=%u rel_end=%u", rel_pos, rel_end);
    ras_net_sendto(net->rpc, pkt, sizeof(pkt), addr, port);
}

static int resolve_path(const ras_config *cfg, const char *ro_path, char *out, size_t out_sz) {
    if (!cfg || !ro_path || !out || out_sz == 0) return -1;

    // RISC OS uses '.' as path separator - convert to Unix '/'
    // Find the share name (first component before '.')
    const char *dot = strchr(ro_path, '.');
    size_t share_len = dot ? (size_t)(dot - ro_path) : strlen(ro_path);
    
    ras_log(RAS_LOG_DEBUG, "resolve_path: ro_path='%s' share_len=%zu", ro_path, share_len);
    
    for (size_t i = 0; i < cfg->share_count; ++i) {
        const char *name = cfg->shares[i].name;
        if (name && strlen(name) == share_len && strncasecmp(name, ro_path, share_len) == 0) {
            // Build the host path, converting '.' to '/'
            const char *rest = dot ? dot + 1 : "";
            int n = snprintf(out, out_sz, "%s", cfg->shares[i].path);
            if (n < 0 || (size_t)n >= out_sz) return -1;
            
            // Append rest of path, converting '.' to '/'
            size_t offset = (size_t)n;
            while (*rest && offset < out_sz - 1) {
                out[offset++] = '/';
                while (*rest && *rest != '.' && offset < out_sz - 1) {
                    out[offset++] = *rest++;
                }
                if (*rest == '.') rest++;
            }
            out[offset] = '\0';
            
            ras_log(RAS_LOG_DEBUG, "resolve_path: resolved to '%s'", out);
            
            // Safety check on final path - skip the leading '/' separator
            const char *rel = out + strlen(cfg->shares[i].path);
            if (*rel == '/') rel++;  // Skip separator
            if (!ras_path_is_safe(rel)) {
                ras_log(RAS_LOG_DEBUG, "resolve_path: safety check failed on '%s'", rel);
                return -1;
            }
            return 0;
        }
    }
    ras_log(RAS_LOG_DEBUG, "resolve_path: no matching share found");
    return -1;
}

// Try to find a file, checking for ,xxx filetype suffix variants
// If the exact path doesn't exist, scan directory for matching base name with suffix
static int find_file_with_suffix(const char *base_path, char *out, size_t out_sz) {
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
        return -1;  // No directory component
    }
    
    size_t dir_len = (size_t)(last_slash - base_path);
    char dir_path[512];
    if (dir_len >= sizeof(dir_path)) return -1;
    memcpy(dir_path, base_path, dir_len);
    dir_path[dir_len] = '\0';
    
    const char *filename = last_slash + 1;
    size_t filename_len = strlen(filename);
    
    // Scan directory for file with matching base name + ,xxx suffix
    DIR *d = opendir(dir_path);
    if (!d) return -1;
    
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        size_t ent_len = strlen(ent->d_name);
        
        // Check for base name + ,xxx pattern
        if (ent_len == filename_len + 4 &&
            strncasecmp(ent->d_name, filename, filename_len) == 0 &&
            ent->d_name[filename_len] == ',' &&
            ras_filetype_from_suffix(ent->d_name) >= 0) {
            
            snprintf(out, out_sz, "%s/%s", dir_path, ent->d_name);
            closedir(d);
            return 0;
        }
    }
    
    closedir(d);
    return -1;
}

static void send_err_pkt(ras_net *net, const unsigned char *rid, int code, const char *addr, unsigned short port) {
    unsigned char pkt[8] = { 'E', rid[0], rid[1], rid[2], 0, 0, 0, 0 };
    pkt[4] = (unsigned char)(code & 0xFF);
    ras_log(RAS_LOG_PROTOCOL, "Sending E-pkt: error=%d", code);
    ras_net_sendto(net->rpc, pkt, sizeof(pkt), addr, port);
}

static void send_r_pkt(ras_net *net, const unsigned char *rid, const void *data, size_t dlen, const char *addr, unsigned short port) {
    unsigned char header[4] = { 'R', rid[0], rid[1], rid[2] };
    struct { unsigned char h[4]; unsigned char p[2048]; } pkt;
    if (dlen > sizeof(pkt.p)) dlen = sizeof(pkt.p);
    memcpy(pkt.h, header, 4);
    if (data && dlen) memcpy(pkt.p, data, dlen);
    ras_log(RAS_LOG_PROTOCOL, "Sending R-pkt: %zu bytes", dlen);
    ras_net_sendto(net->rpc, &pkt, 4 + dlen, addr, port);
}

static void send_d_pkt(ras_net *net, const unsigned char *rid, const void *data, size_t dlen, const char *addr, unsigned short port) {
    unsigned char header[4] = { 'D', rid[0], rid[1], rid[2] };
    struct { unsigned char h[4]; unsigned char p[2048]; } pkt;
    if (dlen > sizeof(pkt.p)) dlen = sizeof(pkt.p);
    memcpy(pkt.h, header, 4);
    if (data && dlen) memcpy(pkt.p, data, dlen);
    ras_net_sendto(net->rpc, &pkt, 4 + dlen, addr, port);
}

static void send_s_pkt(ras_net *net, const unsigned char *rid, const void *data, size_t dlen, const char *addr, unsigned short port) {
    unsigned char header[4] = { 'S', rid[0], rid[1], rid[2] };
    struct { unsigned char h[4]; unsigned char p[2048]; } pkt;
    if (dlen > sizeof(pkt.p)) dlen = sizeof(pkt.p);
    memcpy(pkt.h, header, 4);
    if (data && dlen) memcpy(pkt.p, data, dlen);
    ras_net_sendto(net->rpc, &pkt, 4 + dlen, addr, port);
}

// Build FileDesc (20 bytes): load(4), exec(4), length(4), attrs(4), type(4)
static void build_filedesc(unsigned char *out, const struct stat *st, uint32_t filetype) {
    uint64_t cs = ras_time_to_riscos(st->st_mtime);
    uint32_t load = ras_make_load_addr(filetype, cs);
    uint32_t exec = ras_make_exec_addr(cs);
    uint32_t len = S_ISDIR(st->st_mode) ? 0x800 : (uint32_t)st->st_size;  // 0x800 for dirs
    uint32_t attrs = ras_mode_to_attrs(st->st_mode);
    uint32_t type = S_ISDIR(st->st_mode) ? RAS_TYPE_DIR : RAS_TYPE_FILE;

    write_u32(out, load);
    write_u32(out + 4, exec);
    write_u32(out + 8, len);
    write_u32(out + 12, attrs);
    write_u32(out + 16, type);
}

// Build directory entries only (without header/trailer)
// Returns the number of bytes written
static size_t build_dir_entries(const char *dir_path, const ras_config *cfg, unsigned char *out, size_t out_sz, size_t start_entry) {
    DIR *d = opendir(dir_path);
    if (!d) return 0;

    size_t offset = 0;
    size_t entry_idx = 0;
    struct dirent *ent;

    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        if (entry_idx < start_entry) {
            entry_idx++;
            continue;
        }

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        uint32_t filetype = S_ISDIR(st.st_mode) ? RAS_FILETYPE_DIR : ras_filetype_from_ext(ent->d_name, cfg);

        // Strip ,xxx suffix from name for display to RISC OS
        char display_name[256];
        ras_strip_type_suffix(ent->d_name, display_name, sizeof(display_name));

        // Entry: FileDesc(20) + name + null + padding to 4-byte
        size_t name_len = strlen(display_name);
        size_t entry_size = 20 + name_len + 1;
        entry_size = (entry_size + 3) & ~3u;  // Align to 4 bytes

        if (offset + entry_size > out_sz) break;

        build_filedesc(out + offset, &st, filetype);
        memcpy(out + offset + 20, display_name, name_len + 1);
        // Zero padding
        size_t pad_start = 20 + name_len + 1;
        while (pad_start < entry_size) {
            out[offset + pad_start++] = 0;
        }

        offset += entry_size;
        entry_idx++;
    }

    closedir(d);
    return offset;
}

// Send a combined S+B response for directory catalogue
// Format: S+rid + [content_len, trailer_len, ...entries...] + B+rid + [load, exec, len, access, share_val, handle, content_len, marker]
static void send_catalogue_response(ras_net *net, const unsigned char *rid, const char *dir_path, 
                                     const ras_config *cfg, int handle, const char *addr, unsigned short port) {
    // Buffer for combined packet: S(4) + header(8) + entries(up to 1900) + B(4) + trailer(32)
    unsigned char pkt[2048];
    size_t offset = 0;

    // S + reply_id
    pkt[offset++] = 'S';
    pkt[offset++] = rid[0];
    pkt[offset++] = rid[1];
    pkt[offset++] = rid[2];

    // Build entries into temp buffer to get length
    unsigned char entries[1800];
    size_t entries_len = build_dir_entries(dir_path, cfg, entries, sizeof(entries), 0);

    // Header: content_len (length of entries), trailer_len (0x24 = 36 bytes = B+rid + 8 words)
    write_u32(pkt + offset, (uint32_t)entries_len);
    offset += 4;
    write_u32(pkt + offset, 0x24);  // Trailer length = 36 bytes (includes B+rid)
    offset += 4;

    // Entries
    memcpy(pkt + offset, entries, entries_len);
    offset += entries_len;

    // B + reply_id
    pkt[offset++] = 'B';
    pkt[offset++] = rid[0];
    pkt[offset++] = rid[1];
    pkt[offset++] = rid[2];

    // Trailer (8 words = 32 bytes): load, exec, rounded_len, access, share_val, handle, content_len, marker
    // Python uses fixed 0xffffcd00, 0x00000000 for load/exec in trailer
    uint32_t load = 0xFFFFCD00;
    uint32_t exec = 0x00000000;
    uint32_t rounded_len = ((uint32_t)entries_len + 2047) & ~2047u;
    uint32_t access = 0x13;  // Read-only for others, RW for owner
    uint32_t share_val = (((uint32_t)handle) & 0xFFFFFF00) ^ 0xFFFFFF02;
    uint32_t marker = 0xFFFFFFFF;  // End marker

    write_u32(pkt + offset, load);         offset += 4;
    write_u32(pkt + offset, exec);         offset += 4;
    write_u32(pkt + offset, rounded_len);  offset += 4;
    write_u32(pkt + offset, access);       offset += 4;
    write_u32(pkt + offset, share_val);    offset += 4;
    write_u32(pkt + offset, (uint32_t)handle); offset += 4;
    write_u32(pkt + offset, (uint32_t)entries_len); offset += 4;
    write_u32(pkt + offset, marker);       offset += 4;

    ras_log(RAS_LOG_PROTOCOL, "Sending S+B catalogue: %zu bytes, %zu entries_len, handle=%d", offset, entries_len, handle);
    ras_net_sendto(net->rpc, pkt, offset, addr, port);
}

// Send S+B response for RREADDIR (next chunk)
static void send_readdir_response(ras_net *net, const unsigned char *rid, const char *dir_path,
                                   const ras_config *cfg, int handle, size_t start_entry, const char *addr, unsigned short port) {
    unsigned char pkt[2048];
    size_t offset = 0;

    // S + reply_id
    pkt[offset++] = 'S';
    pkt[offset++] = rid[0];
    pkt[offset++] = rid[1];
    pkt[offset++] = rid[2];

    // Build entries
    unsigned char entries[1800];
    size_t entries_len = build_dir_entries(dir_path, cfg, entries, sizeof(entries), start_entry);

    // Header: content_len, trailer_len (0x0c = 12 bytes for readdir)
    write_u32(pkt + offset, (uint32_t)entries_len);
    offset += 4;
    write_u32(pkt + offset, 0x0c);
    offset += 4;

    // Entries
    memcpy(pkt + offset, entries, entries_len);
    offset += entries_len;

    // B + reply_id
    pkt[offset++] = 'B';
    pkt[offset++] = rid[0];
    pkt[offset++] = rid[1];
    pkt[offset++] = rid[2];

    // Trailer for readdir (3 words = 12 bytes): [content_len, marker]
    uint32_t marker = 0xFFFFFFFF;  // End marker
    write_u32(pkt + offset, (uint32_t)entries_len); offset += 4;
    write_u32(pkt + offset, marker); offset += 4;

    ras_net_sendto(net->rpc, pkt, offset, addr, port);
}

// Check if client is authorized to access a share (returns 1 if OK, 0 if denied)
static int check_share_auth(const ras_config *cfg, ras_auth_state *auth,
                            const char *client_ip, const char *ro_path) {
    if (!cfg || !ro_path) return 0;
    
    // Extract share name from RISC OS path
    const char *dot = strchr(ro_path, '.');
    size_t share_len = dot ? (size_t)(dot - ro_path) : strlen(ro_path);
    
    // Find the share
    for (size_t i = 0; i < cfg->share_count; ++i) {
        const char *name = cfg->shares[i].name;
        if (name && strlen(name) == share_len && strncasecmp(name, ro_path, share_len) == 0) {
            // Found the share - check if protected
            if (!(cfg->shares[i].attributes & RAS_ATTR_PROTECTED)) {
                return 1;  // Not protected, allow
            }
            // Protected - check if client is authenticated
            if (auth && ras_auth_check(auth, client_ip, name)) {
                return 1;  // Authenticated
            }
            ras_log(RAS_LOG_DEBUG, "Auth denied: client %s not authenticated for share '%s'", 
                    client_ip ? client_ip : "?", name);
            return 0;  // Denied
        }
    }
    return 0;  // Share not found
}

int ras_rpc_handle(const unsigned char *buf, size_t len, const char *addr, unsigned short port,
                   const ras_config *cfg, ras_net *net, ras_handle_table *handles, ras_auth_state *auth) {
    if (!buf || len < 4 || !net || !cfg || !handles) return -1;

    unsigned char cmd = buf[0];
    unsigned char rid[3] = { buf[1], buf[2], buf[3] };

    // Hex dump for debugging
    char hexdump[128];
    size_t hlen = len > 32 ? 32 : len;
    for (size_t i = 0; i < hlen; ++i) {
        snprintf(hexdump + i * 3, 4, "%02x ", buf[i]);
    }
    ras_log(RAS_LOG_PROTOCOL, "RPC cmd='%c' len=%zu: %s", (cmd >= 32 && cmd < 127) ? cmd : '?', len, hexdump);

    char host_path[512];

    // Command 'A' is the main file operation command
    // Format: cmd(1) + rid(3) + code(4) + handle(4) + path...
    if (cmd == 'A') {
        if (len < 12) {
            send_err_pkt(net, rid, EINVAL, addr, port);
            return 0;
        }
        uint32_t code = read_u32(buf + 4);
        uint32_t handle = read_u32(buf + 8);
        const char *path = (len > 12) ? (const char *)(buf + 12) : "";

        ras_log(RAS_LOG_PROTOCOL, "A-cmd code=%u handle=%u path='%s'", code, handle, path);

        // Check authentication for path-based operations
        if (path[0] && !check_share_auth(cfg, auth, addr, path)) {
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
            char actual_path[512];
            if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) != 0) {
                send_err_pkt(net, rid, ENOENT, addr, port);
                break;
            }
            struct stat st;
            if (stat(actual_path, &st) != 0) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            uint32_t filetype = S_ISDIR(st.st_mode) ? RAS_FILETYPE_DIR : ras_filetype_from_ext(actual_path, cfg);
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
            char actual_path[512];
            if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) != 0) {
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
                uint32_t filetype = RAS_FILETYPE_DIR;
                uint64_t cs = ras_time_to_riscos(st.st_mtime);

                int hid = 0, tok = 0;
                if (ras_handles_add_ex(handles, RAS_HANDLE_DIR, -1, actual_path,
                                       ras_make_load_addr(filetype, cs), ras_make_exec_addr(cs),
                                       0, ras_mode_to_attrs(st.st_mode),
                                       &hid, &tok) != 0) {
                    send_err_pkt(net, rid, EMFILE, addr, port);
                    break;
                }

                // Reply: FileDesc(20) + handle(4)
                unsigned char reply[24];
                build_filedesc(reply, &st, filetype);
                write_u32(reply + 20, (uint32_t)hid);
                send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            } else {
                // It's a file
                int flags = (code == 0x01) ? O_RDONLY : O_RDWR;
                int fd = open(actual_path, flags);
                if (fd < 0) {
                    send_err_pkt(net, rid, errno, addr, port);
                    break;
                }
                uint32_t filetype = ras_filetype_from_ext(actual_path, cfg);
                uint64_t cs = ras_time_to_riscos(st.st_mtime);

                int hid = 0, tok = 0;
                if (ras_handles_add_ex(handles, RAS_HANDLE_FILE, fd, actual_path,
                                       ras_make_load_addr(filetype, cs), ras_make_exec_addr(cs),
                                       (uint32_t)st.st_size, ras_mode_to_attrs(st.st_mode),
                                       &hid, &tok) != 0) {
                    close(fd);
                    send_err_pkt(net, rid, EMFILE, addr, port);
                    break;
                }

                // Reply: FileDesc(20) + handle(4)
                unsigned char reply[24];
                build_filedesc(reply, &st, filetype);
                write_u32(reply + 20, (uint32_t)hid);
                send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
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
            if (ras_handles_add_ex(handles, RAS_HANDLE_DIR, -1, host_path,
                                   0, 0, 0, ras_mode_to_attrs(st.st_mode),
                                   &hid, &tok) != 0) {
                send_err_pkt(net, rid, EMFILE, addr, port);
                break;
            }
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
            char parent[512];
            strncpy(parent, host_path, sizeof(parent) - 1);
            parent[sizeof(parent) - 1] = '\0';
            char *last_slash = strrchr(parent, '/');
            if (last_slash && last_slash != parent) {
                *last_slash = '\0';
                mkpath(parent, 0775);
            }
            int fd = open(host_path, O_CREAT | O_TRUNC | O_RDWR, 0664);
            if (fd < 0) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            struct stat st;
            fstat(fd, &st);
            uint32_t filetype = ras_filetype_from_ext(host_path, cfg);
            uint64_t cs = ras_time_to_riscos(time(NULL));

            int hid = 0, tok = 0;
            if (ras_handles_add_ex(handles, RAS_HANDLE_FILE, fd, host_path,
                                   ras_make_load_addr(filetype, cs), ras_make_exec_addr(cs),
                                   0, RAS_ATTR_R | RAS_ATTR_W | RAS_ATTR_r,
                                   &hid, &tok) != 0) {
                close(fd);
                send_err_pkt(net, rid, EMFILE, addr, port);
                break;
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
            if (mkpath(host_path, 0775) != 0 && errno != EEXIST) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            struct stat st;
            stat(host_path, &st);
            int hid = 0, tok = 0;
            if (ras_handles_add_ex(handles, RAS_HANDLE_DIR, -1, host_path,
                                   0, 0, 0, ras_mode_to_attrs(st.st_mode),
                                   &hid, &tok) != 0) {
                send_err_pkt(net, rid, EMFILE, addr, port);
                break;
            }
            // Return FileDesc(20) + handle(4) = 24 bytes
            unsigned char reply[24];
            build_filedesc(reply, &st, RAS_FILETYPE_DIR);
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
            char actual_path[512];
            if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) != 0) {
                send_err_pkt(net, rid, ENOENT, addr, port);
                break;
            }
            struct stat st;
            if (stat(actual_path, &st) != 0) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            unsigned char reply[20];
            build_filedesc(reply, &st, S_ISDIR(st.st_mode) ? RAS_FILETYPE_DIR : ras_filetype_from_ext(actual_path, cfg));
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
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            uint32_t new_attrs = read_u32(buf + 8);
            const char *attr_path = (len > 16) ? (const char *)(buf + 16) : "";
            if (resolve_path(cfg, attr_path, host_path, sizeof(host_path)) != 0) {
                send_err_pkt(net, rid, ENOENT, addr, port);
                break;
            }
            // Try to find file with ,xxx suffix if exact path doesn't exist
            char actual_path[512];
            if (find_file_with_suffix(host_path, actual_path, sizeof(actual_path)) != 0) {
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
            if (new_attrs & RAS_ATTR_R) mode |= 0400;
            if (new_attrs & RAS_ATTR_W) mode |= 0200;
            if (new_attrs & RAS_ATTR_r) mode |= 0044;
            if (new_attrs & RAS_ATTR_w) mode |= 0022;
            chmod(actual_path, mode);
            unsigned char reply[20];
            build_filedesc(reply, &st, S_ISDIR(st.st_mode) ? RAS_FILETYPE_DIR : ras_filetype_from_ext(actual_path, cfg));
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        case 0x08: // RFREESPACE
        {
            if (path[0] && resolve_path(cfg, path, host_path, sizeof(host_path)) == 0) {
                // Use resolved path
            } else if (cfg->share_count > 0) {
                snprintf(host_path, sizeof(host_path), "%s", cfg->shares[0].path);
            } else {
                send_err_pkt(net, rid, ENOENT, addr, port);
                break;
            }
            ras_fsinfo fsinfo;
            if (ras_get_fsinfo(host_path, &fsinfo) != 0) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            unsigned char reply[12];
            write_u32(reply, (uint32_t)(fsinfo.free_bytes > 0xFFFFFFFF ? 0xFFFFFFFF : fsinfo.free_bytes));
            write_u32(reply + 4, (uint32_t)(fsinfo.free_bytes > 0xFFFFFFFF ? 0xFFFFFFFF : fsinfo.free_bytes)); // Largest creatable
            write_u32(reply + 8, (uint32_t)(fsinfo.total_bytes > 0xFFFFFFFF ? 0xFFFFFFFF : fsinfo.total_bytes));
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        case 0x16: // RFREESPACE64 - 64-bit free space
        {
            // handle is at buf+8
            // Response: 6 x 4 bytes (free_lo, free_hi, largest_lo, largest_hi, total_lo, total_hi)
            ras_fsinfo fsinfo;
            // Try to get filesystem info from first share
            if (cfg->share_count > 0) {
                ras_get_fsinfo(cfg->shares[0].path, &fsinfo);
            } else {
                memset(&fsinfo, 0, sizeof(fsinfo));
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
            ras_handles_remove(handles, hid);
            // Empty success reply
            send_r_pkt(net, rid, NULL, 0, addr, port);
            break;
        }

        case 0x0b: // RREAD - read file data (uses S+B format like B command)
        {
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + length(4) = 20 bytes
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t offset = read_u32(buf + 12);
            uint32_t rlen = read_u32(buf + 16);
            
            ras_log(RAS_LOG_DEBUG, "RREAD: handle=%d offset=%u len=%u", hid, offset, rlen);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
                ras_log(RAS_LOG_DEBUG, "RREAD: handle %d not found", hid);
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            if (h->fd < 0) {
                ras_log(RAS_LOG_DEBUG, "RREAD: handle %d has no fd", hid);
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            
            if (lseek(h->fd, (off_t)offset, SEEK_SET) < 0) {
                ras_log(RAS_LOG_DEBUG, "RREAD: lseek failed errno=%d", errno);
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            
            // Limit read size
            if (rlen > 16384) rlen = 16384;
            unsigned char data[16384];
            ssize_t n = read(h->fd, data, rlen);
            if (n < 0) {
                ras_log(RAS_LOG_DEBUG, "RREAD: read failed errno=%d", errno);
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            
            uint32_t new_pos = offset + (uint32_t)n;
            h->seq_ptr = new_pos;
            ras_log(RAS_LOG_DEBUG, "RREAD: read %zd bytes at offset %u, new_pos=%u", n, offset, new_pos);
            
            // Build S+B combined response (same format as B command RREAD)
            // Header: S + rid + length(4) + trailer_len(4)
            // Data
            // Trailer: B + rid + length(4) + new_pos(4)
            size_t pkt_len = 4 + 4 + 4 + (size_t)n + 4 + 4 + 4;
            unsigned char *pkt = malloc(pkt_len);
            if (!pkt) { send_err_pkt(net, rid, ENOMEM, addr, port); break; }
            
            size_t off = 0;
            pkt[off++] = 'S';
            pkt[off++] = rid[0];
            pkt[off++] = rid[1];
            pkt[off++] = rid[2];
            write_u32(pkt + off, (uint32_t)n); off += 4;
            write_u32(pkt + off, 0x0c); off += 4;  // trailer_len indicator
            memcpy(pkt + off, data, (size_t)n); off += (size_t)n;
            pkt[off++] = 'B';
            pkt[off++] = rid[0];
            pkt[off++] = rid[1];
            pkt[off++] = rid[2];
            write_u32(pkt + off, (uint32_t)n); off += 4;
            write_u32(pkt + off, new_pos); off += 4;
            
            ras_net_sendto(net->rpc, pkt, off, addr, port);
            free(pkt);
            break;
        }

        case 0x0c: // RWRITE - write file data (initiates w/d protocol)
        {
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + amount(4)
            // This is a request to receive 'amount' bytes starting at 'offset'
            // We need to send 'w' packets to request data, then receive 'd' packets
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t offset = read_u32(buf + 12);
            uint32_t amount = read_u32(buf + 16);
            
            ras_log(RAS_LOG_DEBUG, "RWRITE: handle=%d offset=%u amount=%u", hid, offset, amount);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
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
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + count(4) = 20 bytes
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t start_entry = read_u32(buf + 12);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            if (h->type != RAS_HANDLE_DIR || !h->path) {
                send_err_pkt(net, rid, ENOTDIR, addr, port);
                break;
            }
            
            send_readdir_response(net, rid, h->path, cfg, hid, start_entry, addr, port);
            break;
        }

        case 0x0f: // RSETLENGTH - set file length
        {
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + length(4) = 16 bytes
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t new_len = read_u32(buf + 12);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
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
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + load(4) + exec(4) = 20 bytes
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t load_addr = read_u32(buf + 12);
            uint32_t exec_addr = read_u32(buf + 16);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            
            // Update handle's stored load/exec addresses
            h->load_addr = load_addr;
            h->exec_addr = exec_addr;
            
            // Extract filetype and rename file with ,xxx suffix (files only, not directories)
            uint32_t new_ftype = 0;
            if ((load_addr & 0xFFF00000) == 0xFFF00000) {
                new_ftype = (load_addr >> 8) & 0xFFF;
                
                // Only rename files, not directories (directories don't need ,xxx suffix)
                if (h->path[0] && h->type == RAS_HANDLE_FILE) {
                    char new_path[512];
                    ras_append_type_suffix(h->path, new_ftype, new_path, sizeof(new_path));
                    if (strcmp(h->path, new_path) != 0) {
                        if (rename(h->path, new_path) == 0) {
                            // Update handle's stored path
                            strncpy(h->path, new_path, sizeof(h->path) - 1);
                            h->path[sizeof(h->path) - 1] = '\0';
                            ras_log(RAS_LOG_DEBUG, "RSETINFO: renamed to '%s'", new_path);
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
            // Format: cmd(1) + rid(3) + code(4) + amount(4) + handle(4) + path...
            // The new name is sent in a following 'D' packet
            // For now, we need to receive the 'D' packet containing the new path
            // This is complex - the Python does this with a thread. We'll implement a simpler version.
            // Actually, looking at the packet format more closely:
            // The 'amount' field at buf+8 is the length of the new name that will follow
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            uint32_t new_name_len = read_u32(buf + 8);
            // handle is at buf+12 (but typically 0)
            const char *old_path_str = (len > 16) ? (const char *)(buf + 16) : "";
            
            if (resolve_path(cfg, old_path_str, host_path, sizeof(host_path)) != 0) {
                send_err_pkt(net, rid, ENOENT, addr, port);
                break;
            }
            
            // We need to wait for a 'D' packet containing the new name
            // For simplicity, store the pending rename info and handle it when 'D' arrives
            // Actually this is too complex for now - just send success
            // The Python impl uses a thread to receive the 'D' packet
            // For now, we can't do renames properly without state management
            ras_log(RAS_LOG_DEBUG, "RRENAME: old='%s' new_len=%u - not fully implemented", old_path_str, new_name_len);
            send_err_pkt(net, rid, ENOSYS, addr, port);
            break;
        }

        case 0x0e: // RENSURE - ensure file size allocated
        {
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + size(4) = 16 bytes
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t ensure_size = read_u32(buf + 12);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
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
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
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
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t new_pos = read_u32(buf + 12);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
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
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + offset(4) + length(4) = 20 bytes
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t offset = read_u32(buf + 12);
            uint32_t zero_len = read_u32(buf + 16);
            
            ras_handle *h = NULL;
            if (ras_handles_get(handles, hid, &h) != 0 || !h) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            if (h->fd < 0) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            
            // Seek to offset and extend file with zeros
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
            ras_log(RAS_LOG_DEBUG, "Unsupported A-cmd code %u", code);
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

        ras_log(RAS_LOG_PROTOCOL, "B-cmd code=%u handle=%u extra=%u path='%s'", code, handle, extra, path);

        switch (code) {
        case 0x03: // ROPENDIR
        {
            ras_log(RAS_LOG_DEBUG, "ROPENDIR: attempting resolve_path for '%s'", path);
            if (resolve_path(cfg, path, host_path, sizeof(host_path)) != 0) {
                ras_log(RAS_LOG_DEBUG, "ROPENDIR: resolve_path failed, trying share match");
                // Path is a share name - check if it's a valid share
                int share_idx = -1;
                for (size_t i = 0; i < cfg->share_count; ++i) {
                    if (strcasecmp(cfg->shares[i].name, path) == 0) {
                        share_idx = (int)i;
                        break;
                    }
                }
                if (share_idx >= 0) {
                    snprintf(host_path, sizeof(host_path), "%s", cfg->shares[share_idx].path);
                    ras_log(RAS_LOG_DEBUG, "ROPENDIR: share match found, path='%s'", host_path);
                } else {
                    ras_log(RAS_LOG_DEBUG, "ROPENDIR: no share match, sending ENOENT");
                    send_err_pkt(net, rid, ENOENT, addr, port);
                    break;
                }
            }
            ras_log(RAS_LOG_DEBUG, "ROPENDIR: host_path='%s'", host_path);
            struct stat st;
            if (stat(host_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
                ras_log(RAS_LOG_DEBUG, "ROPENDIR: stat failed or not a dir: errno=%d", errno);
                send_err_pkt(net, rid, ENOTDIR, addr, port);
                break;
            }
            ras_log(RAS_LOG_DEBUG, "ROPENDIR: stat OK, is directory");
            int hid = 0, tok = 0;
            if (ras_handles_add_ex(handles, RAS_HANDLE_DIR, -1, host_path,
                                   0, 0, 0, ras_mode_to_attrs(st.st_mode),
                                   &hid, &tok) != 0) {
                ras_log(RAS_LOG_DEBUG, "ROPENDIR: ras_handles_add_ex failed");
                send_err_pkt(net, rid, EMFILE, addr, port);
                break;
            }
            ras_log(RAS_LOG_DEBUG, "ROPENDIR: handle=%d, calling send_catalogue_response", hid);
            // Send combined S+B catalogue response
            send_catalogue_response(net, rid, host_path, cfg, hid, addr, port);
            break;
        }

        case 0x0b: // RREAD - read file data (B command format, returns S+B)
        {
            // Format: cmd(1) + rid(3) + code(4) + handle(4) + pos(4) + length(4) = 20 bytes
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            int hid = (int)handle;
            uint32_t pos = extra;  // extra contains position
            uint32_t rlen = read_u32(buf + 16);
            
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            
            if (lseek(h->fd, (off_t)pos, SEEK_SET) < 0) {
                send_err_pkt(net, rid, errno, addr, port);
                break;
            }
            
            // Limit read size
            if (rlen > 16384) rlen = 16384;
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
            if (!pkt) { send_err_pkt(net, rid, ENOMEM, addr, port); break; }
            
            size_t off = 0;
            pkt[off++] = 'S';
            pkt[off++] = rid[0];
            pkt[off++] = rid[1];
            pkt[off++] = rid[2];
            write_u32(pkt + off, (uint32_t)n); off += 4;
            write_u32(pkt + off, 0x0c); off += 4;  // trailer_len indicator
            memcpy(pkt + off, data, (size_t)n); off += (size_t)n;
            pkt[off++] = 'B';
            pkt[off++] = rid[0];
            pkt[off++] = rid[1];
            pkt[off++] = rid[2];
            write_u32(pkt + off, (uint32_t)n); off += 4;
            write_u32(pkt + off, new_pos); off += 4;
            
            ras_net_sendto(net->rpc, pkt, off, addr, port);
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

            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->type != RAS_HANDLE_DIR || !h->path) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            // Send combined S+B readdir response
            send_readdir_response(net, rid, h->path, cfg, hid, 0, addr, port);
            break;
        }

        default:
            ras_log(RAS_LOG_DEBUG, "Unsupported B-cmd code %u", code);
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

        ras_log(RAS_LOG_PROTOCOL, "a-cmd code=%u handle=%d", code, hid);

        switch (code) {
        case 0x0a: // RCLOSE
        {
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            if (h->fd >= 0) close(h->fd);
            ras_handles_close(handles, hid, h->token);
            send_r_pkt(net, rid, NULL, 0, addr, port);
            break;
        }

        case 0x0b: // RREAD
        {
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int off = read_u32(buf + 12);
            unsigned int rlen = read_u32(buf + 16);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            if (lseek(h->fd, (off_t)off, SEEK_SET) < 0) { send_err_pkt(net, rid, errno, addr, port); break; }
            unsigned char data[2048];
            if (rlen > sizeof(data)) rlen = sizeof(data);
            ssize_t n = read(h->fd, data, rlen);
            if (n < 0) { send_err_pkt(net, rid, errno, addr, port); break; }
            h->seq_ptr = off + (uint32_t)n;
            send_d_pkt(net, rid, data, (size_t)n, addr, port);
            break;
        }

        case 0x0c: // RWRITE (a-cmd format, initiates w/d protocol)
        {
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int off = read_u32(buf + 12);
            unsigned int amount = read_u32(buf + 16);
            
            ras_log(RAS_LOG_DEBUG, "a-cmd RWRITE: handle=%d offset=%u amount=%u", hid, off, amount);
            
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            
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
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int start = read_u32(buf + 12);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->type != RAS_HANDLE_DIR || !h->path) {
                send_err_pkt(net, rid, EBADF, addr, port);
                break;
            }
            send_readdir_response(net, rid, h->path, cfg, hid, start, addr, port);
            break;
        }

        case 0x0e: // RENSURE - ensure file size
        {
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int ensure_size = read_u32(buf + 12);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            
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
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int newlen = read_u32(buf + 12);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            if (ftruncate(h->fd, (off_t)newlen) != 0) { send_err_pkt(net, rid, errno, addr, port); break; }
            h->length = newlen;
            send_r_pkt(net, rid, NULL, 0, addr, port);
            break;
        }

        case 0x10: // RSETINFO (set load/exec addresses)
        {
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            uint32_t load = read_u32(buf + 12);
            uint32_t exec = read_u32(buf + 16);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            h->load_addr = load;
            h->exec_addr = exec;
            // Update file mtime from exec address
            if (h->path) {
                uint64_t cs = ((uint64_t)(load & 0xFF) << 32) | exec;
                time_t t = ras_time_from_riscos(cs);
                ras_set_mtime(h->path, t);
            }
            send_r_pkt(net, rid, NULL, 0, addr, port);
            break;
        }

        case 0x11: // RGETSEQPTR
        {
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            unsigned char reply[4];
            write_u32(reply, h->seq_ptr);
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        case 0x12: // RSETSEQPTR
        {
            if (len < 16) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            uint32_t ptr = read_u32(buf + 12);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            h->seq_ptr = ptr;
            if (h->fd >= 0) lseek(h->fd, (off_t)ptr, SEEK_SET);
            send_r_pkt(net, rid, NULL, 0, addr, port);
            break;
        }

        case 0x14: // RZERO - write zeros
        {
            if (len < 20) { send_err_pkt(net, rid, EINVAL, addr, port); break; }
            unsigned int offset = read_u32(buf + 12);
            unsigned int zero_len = read_u32(buf + 16);
            ras_handle *h = NULL;
            for (size_t i = 0; i < handles->count; ++i) {
                if (handles->items[i].id == hid) {
                    h = &handles->items[i];
                    break;
                }
            }
            if (!h || h->fd < 0) { send_err_pkt(net, rid, EBADF, addr, port); break; }
            
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
            reply[0] = 0x02;  // Version 2
            reply[1] = 0x00;
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        default:
            ras_log(RAS_LOG_DEBUG, "Unsupported a-cmd code %u", code);
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

        ras_log(RAS_LOG_PROTOCOL, "F-cmd code=%u handle=%u", code, handle);

        switch (code) {
        case 0x13: // RDEADHANDLES - client asking about dead handles
        {
            // Reply with empty list (no dead handles)
            // Format: R + rid + count(4) where count=0
            unsigned char reply[4];
            write_u32(reply, 0);  // No dead handles
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        case 0x15: // RVERSION
        {
            unsigned char reply[4];
            write_u32(reply, 0x00000002);  // Version 2
            send_r_pkt(net, rid, reply, sizeof(reply), addr, port);
            break;
        }

        default:
            ras_log(RAS_LOG_DEBUG, "Unsupported F-cmd code %u", code);
            send_err_pkt(net, rid, ENOSYS, addr, port);
            break;
        }
        return 0;
    }

    // 'd' command - data packet from client (response to our 'w' request)
    // Format: d + rid(3) + pos(4) + data...
    if (cmd == 'd') {
        if (len < 8) {
            return 0;  // Too short, ignore
        }
        
        uint32_t rel_pos = read_u32(buf + 4);
        const unsigned char *data = buf + 8;
        size_t data_len = len - 8;
        
        ras_log(RAS_LOG_DEBUG, "d-pkt: rel_pos=%u data_len=%zu", rel_pos, data_len);
        
        // Find pending write for this reply ID
        pending_write_t *pw = find_pending_write(rid);
        if (!pw) {
            ras_log(RAS_LOG_DEBUG, "d-pkt: no pending write found for rid");
            return 0;
        }
        
        // Get the handle
        ras_handle *h = NULL;
        if (ras_handles_get(handles, pw->handle_id, &h) != 0 || !h || h->fd < 0) {
            ras_log(RAS_LOG_DEBUG, "d-pkt: handle %d invalid", pw->handle_id);
            free_pending_write(pw);
            return 0;
        }
        
        // Calculate absolute position and write data
        uint32_t abs_pos = pw->start_pos + rel_pos;
        if (lseek(h->fd, (off_t)abs_pos, SEEK_SET) < 0) {
            ras_log(RAS_LOG_DEBUG, "d-pkt: lseek failed");
            send_err_pkt(net, pw->rid, errno, pw->addr, pw->port);
            free_pending_write(pw);
            return 0;
        }
        
        ssize_t n = write(h->fd, data, data_len);
        if (n < 0) {
            ras_log(RAS_LOG_DEBUG, "d-pkt: write failed");
            send_err_pkt(net, pw->rid, errno, pw->addr, pw->port);
            free_pending_write(pw);
            return 0;
        }
        
        pw->current_pos = abs_pos + (uint32_t)n;
        h->seq_ptr = pw->current_pos;
        if (h->seq_ptr > h->length) h->length = h->seq_ptr;
        
        ras_log(RAS_LOG_DEBUG, "d-pkt: wrote %zd bytes at %u, current_pos=%u end_pos=%u", 
                n, abs_pos, pw->current_pos, pw->end_pos);
        
        // Check if we need more data
        if (pw->current_pos < pw->end_pos) {
            // Request next chunk
            uint32_t rel_current = pw->current_pos - pw->start_pos;
            uint32_t remaining = pw->end_pos - pw->current_pos;
            uint32_t chunk = (remaining < WRITE_CHUNK_SIZE) ? remaining : WRITE_CHUNK_SIZE;
            send_w_pkt(net, pw->rid, rel_current, rel_current + chunk, pw->addr, pw->port);
        } else {
            // Transfer complete
            ras_log(RAS_LOG_DEBUG, "d-pkt: transfer complete, sending R-pkt");
            send_r_pkt(net, pw->rid, NULL, 0, pw->addr, pw->port);
            free_pending_write(pw);
        }
        return 0;
    }

    // Unknown command
    ras_log(RAS_LOG_DEBUG, "Unsupported cmd '%c' (%u)", cmd, cmd);
    send_err_pkt(net, rid, ENOSYS, addr, port);
    return 0;
}
