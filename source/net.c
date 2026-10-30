/* net.c - see net.h. The web server answers one request at a time (the
 * GameCube's network library is happiest that way), with "Connection:
 * close"; a phone's browser simply queues its requests.
 *
 *   GET  /                 the page (webpage.h)
 *   GET  /api/photos       every photo as JSON
 *   GET  /thumb/<key>      a photo's 160x120 thumbnail as JPEG
 *   GET  /photo/<key>/name the photo file itself (?dl=1 to download)
 *   POST /upload?name=..   a photo sent as the request body
 */
#include "net.h"
#include "photos.h"
#include "picture.h"
#include "pixels.h"
#include "settings.h"
#include "storage.h"
#include "applog.h"
#include "thr.h"
#include "webpage.h"
#include "stb_image_write.h"
#include <ctype.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#ifdef GEKKO
#include <network.h>
#define SK_SOCKET(a, b, c) net_socket(a, b, c)
#define SK_BIND net_bind
#define SK_LISTEN net_listen
#define SK_ACCEPT net_accept
#define SK_RECV(s, b, n) net_recv(s, b, n, 0)
#define SK_SEND(s, b, n) net_send(s, b, n, 0)
#define SK_CLOSE net_close
#define SK_SELECT net_select
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define SK_SOCKET(a, b, c) socket(a, b, c)
#define SK_BIND bind
#define SK_LISTEN listen
#define SK_ACCEPT accept
#define SK_RECV(s, b, n) recv(s, b, n, 0)
#define SK_SEND(s, b, n) send(s, b, n, MSG_NOSIGNAL)
#define SK_CLOSE close
#define SK_SELECT select
#endif

static volatile NetState state = NET_OFF;
static char ip[16], mask[16], gw[16], msg[96] = "Network not started", url[48];
static int port = 80;
static thr_t thread;
static thr_mutex lock;
static NetTransfer recent[NET_RECENT];
static int nrecent, received;
static char arrived[8][300];    /* uploaded files waiting to join the library */
static int narrived;

NetState net_state(void) { return state; }
const char *net_ip(void) { return ip; }
const char *net_gateway(void) { return gw; }
const char *net_message(void) { return msg; }
const char *net_url(void) { return url; }
int net_received(void) { return received; }

int net_recent(NetTransfer *out, int cap) {
    if (state == NET_OFF) return 0;
    thr_lock(&lock);
    int n = nrecent < cap ? nrecent : cap;
    memcpy(out, recent, sizeof(NetTransfer) * n);
    thr_unlock(&lock);
    return n;
}

/* a new line at the top of the recent list; returns its slot */
static int recent_add(const char *name, uint32_t total, bool to_phone, uint32_t key) {
    thr_lock(&lock);
    memmove(&recent[1], &recent[0], sizeof(NetTransfer) * (NET_RECENT - 1));
    if (nrecent < NET_RECENT) nrecent++;
    memset(&recent[0], 0, sizeof(recent[0]));
    snprintf(recent[0].name, sizeof(recent[0].name), "%s", name);
    recent[0].total = total;
    recent[0].state = 1;
    recent[0].to_phone = to_phone;
    recent[0].photo_key = key;   /* (an arriving photo gets its key once it's in the library) */
    thr_unlock(&lock);
    return 0;
}

static void recent_update(const char *name, uint32_t done, int st) {
    thr_lock(&lock);
    for (int i = 0; i < nrecent; i++)
        if (!strcmp(recent[i].name, name) && recent[i].state == 1) { recent[i].done = done; recent[i].state = st; break; }
    thr_unlock(&lock);
}

void net_tick(void) {
    if (state == NET_OFF) return;
    thr_lock(&lock);
    while (narrived > 0) {
        char path[300];
        snprintf(path, sizeof(path), "%s", arrived[0]);
        memmove(arrived[0], arrived[1], sizeof(arrived[0]) * (size_t)(narrived - 1));
        narrived--;
        thr_unlock(&lock);
        int idx = photos_add_file(path);
        uint32_t key = photos_key_of(idx);
        thr_lock(&lock);
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        /* the upload with this name (a photo sent to the phone may share it) */
        for (int i = 0; i < nrecent; i++)
            if (!recent[i].to_phone && !recent[i].photo_key && !strcmp(recent[i].name, name)) { recent[i].photo_key = key; break; }
    }
    thr_unlock(&lock);
}

/* ------------------------------------------------------------ sockets */

static int wait_fd(int fd, bool wr, int ms) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd, &set);
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    int r = SK_SELECT(fd + 1, wr ? NULL : &set, wr ? &set : NULL, NULL, &tv);
    return r < 0 ? -1 : r > 0;
}

static bool send_all(int fd, const void *data, size_t len) {
    const char *p = data;
    while (len > 0) {
        if (wait_fd(fd, true, 20000) != 1) return false;
        int chunk = len > 8192 ? 8192 : (int)len;
        int n = SK_SEND(fd, p, chunk);
        if (n <= 0) return false;
        p += n;
        len -= (size_t)n;
    }
    return true;
}

static int recv_some(int fd, void *buf, int cap, int ms) {
    if (wait_fd(fd, false, ms) != 1) return -1;
    int n = SK_RECV(fd, buf, cap);
    return n;
}

static void send_head(int fd, int code, const char *status, const char *type, long len, const char *extra) {
    char h[512];
    int n = snprintf(h, sizeof(h),
                     "HTTP/1.0 %d %s\r\nContent-Type: %s\r\n%s%sConnection: close\r\nCache-Control: no-cache\r\n",
                     code, status, type, extra ? extra : "", "");
    if (len >= 0) n += snprintf(h + n, sizeof(h) - n, "Content-Length: %ld\r\n", len);
    n += snprintf(h + n, sizeof(h) - n, "\r\n");
    send_all(fd, h, (size_t)n);
}

static void send_text(int fd, int code, const char *status, const char *type, const char *body, size_t len) {
    send_head(fd, code, status, type, (long)len, NULL);
    send_all(fd, body, len);
}

static void send_json(int fd, int code, const char *status, const char *body) {
    send_text(fd, code, status, "application/json", body, strlen(body));
}

/* ------------------------------------------------------------- helpers */

static void url_decode(char *s) {
    char *o = s;
    for (; *s; s++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else if (*s == '+') *o++ = ' ';
        else *o++ = *s;
    }
    *o = 0;
}

static const char *content_type(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return "application/octet-stream";
    if (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, ".png")) return "image/png";
    if (!strcasecmp(dot, ".gif")) return "image/gif";
    if (!strcasecmp(dot, ".bmp")) return "image/bmp";
    return "application/octet-stream";
}

/* A safe file name for the card: letters, digits, '-', '_' and one '.ext'. */
static bool clean_name(const char *in, char *out, size_t cap) {
    char base[48] = "", ext[8] = "";
    const char *dot = strrchr(in, '.');
    const char *slash = strrchr(in, '/');
    if (slash) in = slash + 1;
    size_t k = 0;
    for (const char *p = in; *p && p != dot && k + 1 < sizeof(base); p++) {
        unsigned char c = (unsigned char)*p;
        base[k++] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    base[k] = 0;
    if (!k) snprintf(base, sizeof(base), "PHOTO");
    if (dot) {
        k = 0;
        for (const char *p = dot + 1; *p && k + 1 < sizeof(ext); p++) ext[k++] = (char)tolower((unsigned char)*p);
        ext[k] = 0;
    }
    if (strcmp(ext, "jpg") && strcmp(ext, "jpeg") && strcmp(ext, "png") && strcmp(ext, "gif") && strcmp(ext, "bmp"))
        return false;
    snprintf(out, cap, "%s.%s", base, ext);
    return true;
}

typedef struct { char *buf; size_t len, cap; } Mem;
static void mem_write(void *ctx, void *data, int size) {
    Mem *m = ctx;
    if (m->len + (size_t)size > m->cap) {
        size_t nc = (m->cap + (size_t)size) * 2;
        char *nb = realloc(m->buf, nc);
        if (!nb) return;
        m->buf = nb;
        m->cap = nc;
    }
    memcpy(m->buf + m->len, data, (size_t)size);
    m->len += (size_t)size;
}

/* ------------------------------------------------------------- routes */

static void get_thumb(int fd, uint32_t key) {
    char path[300];
    if (!photos_path_for_key(key, path, sizeof(path))) { send_text(fd, 404, "Not Found", "text/plain", "no", 2); return; }
    uint16_t *tex = memalign(32, THUMB_BYTES);
    if (!tex) { send_text(fd, 503, "Busy", "text/plain", "busy", 4); return; }
    char cache[300];
    snprintf(cache, sizeof(cache), "%s/thumbs/%08x.t", storage_dir(), (unsigned)key);
    bool ok = false;
    FILE *f = fopen(cache, "rb");
    if (f) {
        ok = fseek(f, 32, SEEK_SET) == 0 && fread(tex, 1, THUMB_BYTES, f) == THUMB_BYTES;
        fclose(f);
    }
    if (!ok) {
        PicInfo info;
        ok = pic_info(path, &info) && pic_make_thumb(path, &info, tex);
    }
    if (!ok) { free(tex); send_text(fd, 404, "Not Found", "text/plain", "no", 2); return; }
    uint8_t *rgb = malloc(THUMB_W * THUMB_H * 3);
    Mem m = { NULL, 0, 0 };
    if (rgb) {
        for (int y = 0; y < THUMB_H; y++)
            for (int x = 0; x < THUMB_W; x++) {
                int r, g, b;
                unrgb565(tex[tex_index(THUMB_W, x, y)], &r, &g, &b);
                uint8_t *p = rgb + (y * THUMB_W + x) * 3;
                p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b;
            }
        stbi_write_jpg_to_func(mem_write, &m, THUMB_W, THUMB_H, 3, rgb, 80);
    }
    free(rgb);
    free(tex);
    if (!m.len) { free(m.buf); send_text(fd, 500, "Error", "text/plain", "error", 5); return; }
    send_head(fd, 200, "OK", "image/jpeg", (long)m.len, "Cache-Control: max-age=86400\r\n");
    send_all(fd, m.buf, m.len);
    free(m.buf);
}

static void get_photo(int fd, uint32_t key, bool download) {
    char path[300];
    if (!photos_path_for_key(key, path, sizeof(path))) { send_text(fd, 404, "Not Found", "text/plain", "no", 2); return; }
    FILE *f = fopen(path, "rb");
    if (!f) { send_text(fd, 404, "Not Found", "text/plain", "no", 2); return; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char extra[160] = "";
    if (download) snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
    send_head(fd, 200, "OK", content_type(name), len, extra);
    recent_add(name, (uint32_t)len, true, key);
    char *buf = malloc(16384);
    long sent = 0;
    bool ok = buf != NULL;
    while (ok && sent < len) {
        size_t n = fread(buf, 1, 16384, f);
        if (n == 0) break;
        ok = send_all(fd, buf, n);
        sent += (long)n;
        recent_update(name, (uint32_t)sent, 1);
    }
    recent_update(name, (uint32_t)sent, ok && sent == len ? 2 : 3);
    free(buf);
    fclose(f);
}

static void post_upload(int fd, const char *query, long length, const char *pre, int pre_len) {
    char raw[200] = "", name[64];
    const char *q = strstr(query, "name=");
    if (q) {
        snprintf(raw, sizeof(raw), "%s", q + 5);
        char *amp = strchr(raw, '&');
        if (amp) *amp = 0;
        url_decode(raw);
    }
    if (!clean_name(raw, name, sizeof(name)) || length <= 0 || length > 64L * 1024 * 1024) {
        send_json(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"Only photos (JPEG, PNG, GIF, BMP) can be sent.\"}");
        return;
    }
    storage_mkdirs(UPLOAD_DIR);
    /* don't overwrite: IMG_1.jpg, IMG_1_2.jpg ... */
    char path[300], part[310];
    snprintf(path, sizeof(path), "%s/%s", UPLOAD_DIR, name);
    for (int k = 2; storage_is_file(path) && k < 1000; k++) {
        char base[64];
        snprintf(base, sizeof(base), "%s", name);
        char *dot = strrchr(base, '.');
        if (dot) *dot = 0;
        snprintf(path, sizeof(path), "%s/%s_%d%s", UPLOAD_DIR, base, k, strrchr(name, '.'));
    }
    const char *fname = strrchr(path, '/') + 1;
    snprintf(part, sizeof(part), "%s.part", path);
    FILE *f = fopen(part, "wb");
    if (!f) {
        send_json(fd, 500, "Error", "{\"ok\":false,\"error\":\"Can't write to the SD card.\"}");
        return;
    }
    setvbuf(f, NULL, _IOFBF, 32 * 1024);
    recent_add(fname, (uint32_t)length, false, 0);
    long got = 0;
    bool ok = true;
    if (pre_len > 0) {
        ok = fwrite(pre, 1, (size_t)pre_len, f) == (size_t)pre_len;
        got = pre_len;
    }
    char *buf = malloc(16384);
    ok = ok && buf;
    while (ok && got < length) {
        long want = length - got > 16384 ? 16384 : length - got;
        int n = recv_some(fd, buf, (int)want, 30000);
        if (n <= 0) { ok = false; break; }
        ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n;
        got += n;
        recent_update(fname, (uint32_t)got, 1);
    }
    free(buf);
    ok = (fclose(f) == 0) && ok && got == length;
    if (ok) {
        remove(path);
        ok = rename(part, path) == 0;
    } else {
        remove(part);
    }
    recent_update(fname, (uint32_t)got, ok ? 2 : 3);
    if (ok) {
        thr_lock(&lock);
        if (narrived < 8) snprintf(arrived[narrived++], sizeof(arrived[0]), "%s", path);
        received++;
        thr_unlock(&lock);
        char body[160];
        int n = snprintf(body, sizeof(body), "{\"ok\":true,\"name\":\"%s\"}", fname);
        send_text(fd, 200, "OK", "application/json", body, (size_t)n);
    } else {
        send_json(fd, 500, "Error", "{\"ok\":false,\"error\":\"The photo didn't arrive whole (or the SD card is full).\"}");
    }
}

static void handle(int fd) {
    enum { HDR = 8192 };
    char *req = malloc(HDR + 1);
    if (!req) return;
    int len = 0;
    char *end = NULL;
    while (len < HDR) {
        int n = recv_some(fd, req + len, HDR - len, 15000);
        if (n <= 0) break;
        len += n;
        req[len] = 0;
        if ((end = strstr(req, "\r\n\r\n")) != NULL) break;
    }
    if (!end) { free(req); return; }
    *end = 0;
    char *body = end + 4;
    int body_have = len - (int)(body - req);
    char method[8] = "", target[400] = "";
    sscanf(req, "%7s %399s", method, target);
    long clen = -1;
    for (char *h = strstr(req, "\r\n"); h; h = strstr(h + 2, "\r\n")) {
        if (!strncasecmp(h + 2, "Content-Length:", 15)) clen = atol(h + 17);
    }
    char *query = strchr(target, '?');
    if (query) *query++ = 0;
    else query = (char *)"";

    if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/index.html"))) {
        send_text(fd, 200, "OK", "text/html; charset=utf-8", WEBPAGE, sizeof(WEBPAGE) - 1);
    } else if (!strcmp(method, "GET") && !strcmp(target, "/api/photos")) {
        size_t n;
        char *j = photos_json(&n);
        if (j) { send_text(fd, 200, "OK", "application/json", j, n); free(j); }
        else send_text(fd, 503, "Busy", "text/plain", "busy", 4);
    } else if (!strcmp(method, "GET") && !strncmp(target, "/thumb/", 7)) {
        get_thumb(fd, (uint32_t)strtoul(target + 7, NULL, 16));
    } else if (!strcmp(method, "GET") && !strncmp(target, "/photo/", 7)) {
        get_photo(fd, (uint32_t)strtoul(target + 7, NULL, 16), strstr(query, "dl=1") != NULL);
    } else if (!strcmp(method, "POST") && !strcmp(target, "/upload")) {
        post_upload(fd, query, clen, body, body_have);
    } else if (!strcmp(method, "GET") && !strcmp(target, "/favicon.ico")) {
        send_text(fd, 404, "Not Found", "text/plain", "", 0);
    } else {
        send_text(fd, 404, "Not Found", "text/plain", "Not found", 9);
    }
    free(req);
}

/* ------------------------------------------------------------- startup */

static bool bring_up(void) {
#ifdef GEKKO
    if (g_set.dhcp) ip[0] = mask[0] = gw[0] = 0;
    else {
        snprintf(ip, sizeof(ip), "%s", g_set.ip);
        snprintf(mask, sizeof(mask), "%s", g_set.netmask);
        snprintf(gw, sizeof(gw), "%s", g_set.gateway);
    }
    s32 r = if_config(ip, mask, gw, g_set.dhcp ? true : false);
    if (r < 0) {
        snprintf(msg, sizeof(msg), "No network (error %d). Check the network adapter's cable.", (int)r);
        return false;
    }
    return ip[0] != 0;
#else
    snprintf(ip, sizeof(ip), "192.168.1.42");   /* PC tests: an address like a home network's, for screenshots */
    return true;
#endif
}

static int open_server(int p) {
    int s = SK_SOCKET(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) return -1;
#ifndef GEKKO
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
#ifdef GEKKO
    sa.sin_len = sizeof(sa);
#endif
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)p);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (SK_BIND(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 || SK_LISTEN(s, 8) < 0) {
        SK_CLOSE(s);
        return -1;
    }
    return s;
}

static void *net_main(void *arg) {
    (void)arg;
    snprintf(msg, sizeof(msg), g_set.dhcp ? "Asking your router for an address..." : "Starting the network...");
    if (!bring_up()) {
        state = NET_FAILED;
        if (!msg[0] || !strncmp(msg, "Asking", 6) || !strncmp(msg, "Starting", 8))
            snprintf(msg, sizeof(msg), "No network. Is the network adapter connected?");
        return NULL;
    }
    port = g_set.http_port;
    int s = open_server(port);
    if (s < 0 && port != 8080) { port = 8080; s = open_server(port); }
    if (s < 0) {
        state = NET_FAILED;
        snprintf(msg, sizeof(msg), "Couldn't start the web server.");
        return NULL;
    }
    if (port == 80) snprintf(url, sizeof(url), "http://%s/", ip);
    else snprintf(url, sizeof(url), "http://%s:%d/", ip, port);
    snprintf(msg, sizeof(msg), "Ready on your home network");
    state = NET_UP;
    app_log("transfer: serving %s", url);
    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int c = SK_ACCEPT(s, (struct sockaddr *)&from, &fl);
        if (c < 0) { thr_sleep_ms(50); continue; }
        handle(c);
        SK_CLOSE(c);
    }
    return NULL;
}

void net_start(void) {
    if (state != NET_OFF) return;
    thr_mutex_init(&lock);
    state = NET_STARTING;
    if (!thr_create(&thread, net_main, NULL, 64 * 1024, 50)) {
        state = NET_FAILED;
        snprintf(msg, sizeof(msg), "Couldn't start the network.");
    }
}
