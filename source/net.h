/* net.h - the network side of Transfer: bringing up the Broadband Adapter
 * (or a WIZnet adapter such as the ETH2GC, through libogc2), and the small
 * web server a phone or computer talks to. Everything runs on its own
 * thread; the screen only reads the state below. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { NET_OFF, NET_STARTING, NET_UP, NET_FAILED } NetState;

void net_start(void);            /* starts the network and the server (once) */
NetState net_state(void);
const char *net_ip(void);        /* "192.168.1.50" when up */
const char *net_gateway(void);   /* the router, when up */
const char *net_message(void);   /* status or problem, for the screen */
const char *net_url(void);       /* "http://192.168.1.50/" (port added if not 80) */

/* Recent transfers, newest first, for the screen. */
#define NET_RECENT 6
typedef struct {
    char name[64];
    uint32_t done, total;
    int state;                   /* 0 waiting, 1 sending, 2 done, 3 failed */
    bool to_phone;               /* a photo the phone downloaded */
    uint32_t photo_key;          /* which photo (see photos_key_of), or 0 */
} NetTransfer;
int  net_recent(NetTransfer *out, int cap);
/* Called on the main thread each frame: adds photos that have arrived. */
void net_tick(void);
/* Total photos received this session. */
int  net_received(void);
