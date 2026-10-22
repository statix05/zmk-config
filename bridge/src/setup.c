/* SPDX-License-Identifier: MIT */
#include "setup.h"
#include <string.h>
/* Separate HID interface: vendor page FF31, usage 74. No keyboard/mouse TLC,
 * no shared report IDs, no input/output reports, no Studio RPC transport. */
const uint8_t setup_report_desc[23] = {
    0x06,0x31,0xff, 0x09,0x74, 0xa1,0x01, 0x85,SETUP_REPORT_ID,
    0x15,0x00, 0x26,0xff,0x00, 0x75,0x08, 0x95,SETUP_BODY_SIZE,
    0x09,0x75, 0xb1,0x02, 0xc0
};
static bool address_valid(uint8_t type, const uint8_t *addr) {
    if (type > 1) return false; /* no ANY, identity aliases, wildcard or unspecified */
    unsigned zeros=0, ones=0;
    for (unsigned i=0; i<6; ++i) { zeros += addr[i]==0; ones += addr[i]==255; }
    return zeros != 6 && ones != 6;
}
static bool same(const struct setup_peer *p, uint8_t type, const uint8_t *addr) {
    return p->type == type && !memcmp(p->addr, addr, 6);
}
void setup_close(struct setup_state *s) {
    s->scan_until = s->pair_until = 0;
    s->selected = s->claimed = false;
    memset(&s->target, 0, sizeof(s->target));
}
void setup_expire(struct setup_state *s, uint64_t now) {
    if (s->scan_until && now >= s->scan_until) {
        s->scan_until = 0;
        if (!s->pair_until) s->selected = false;
    }
    if (s->pair_until && now >= s->pair_until) setup_close(s);
}
void setup_observe(struct setup_state *s, uint64_t now, const struct setup_peer *p) {
    if (!s->scan_until || now >= s->scan_until || !address_valid(p->type,p->addr)) return;
    unsigned i;
    for (i=0; i<s->count; ++i) if (same(&s->peers[i],p->type,p->addr)) break;
    if (i==s->count) {
        if (s->count == SETUP_MAX_CANDIDATES) return;
        ++s->count;
    }
    s->peers[i] = *p;
    /* Names are untrusted advertisements, not an identity/authentication claim. */
    for (unsigned j=0;j<sizeof(p->name);++j)
        if ((unsigned char)s->peers[i].name[j] < 32 || (unsigned char)s->peers[i].name[j] > 126)
            s->peers[i].name[j] = ' ';
}
bool setup_target_allowed(const struct setup_state *s, uint64_t now, uint8_t type, const uint8_t addr[6]) {
    return s->selected && s->pair_until && now < s->pair_until && same(&s->target,type,addr);
}
bool setup_claim(struct setup_state *s, uint64_t now, uint8_t type, const uint8_t addr[6]) {
    if (s->claimed || !setup_target_allowed(s,now,type,addr)) return false;
    s->claimed = true; /* one connection attempt per explicit ARM */
    return true;
}
static void reply(struct setup_state *s, uint8_t op, uint8_t status, uint8_t index,
                  const struct setup_peer *peer) {
    memset(s->reply,0,sizeof(s->reply));
    s->reply[0] = SETUP_REPORT_ID;
    uint8_t *r = s->reply+1;
    r[0]=SETUP_VERSION; r[1]=op; r[2]=status;
    r[3]=(s->scan_until ? 1:0) | (s->selected ? 2:0) | (s->pair_until ? 4:0) | (s->claimed ? 8:0);
    r[4]=index; r[5]=s->count;
    if (peer) {
        r[6]=peer->type; memcpy(r+7,peer->addr,6); r[13]=(uint8_t)peer->rssi;
        memcpy(r+14,peer->name,18);
    }
}
bool setup_feature_get_valid(uint16_t value, uint16_t requested) {
    return value == (0x0300 | SETUP_REPORT_ID) && requested == SETUP_WIRE_SIZE;
}
bool setup_feature_set(struct setup_state *s, uint64_t now, bool connected, bool bonded,
                       uint16_t value, uint16_t requested, const uint8_t *data, size_t len) {
    if (!setup_feature_get_valid(value,requested) || len != SETUP_WIRE_SIZE ||
        !data || data[0] != SETUP_REPORT_ID) return false;
    setup_expire(s,now);
    const uint8_t *c=data+1;
    uint8_t op=c[1], status=SETUP_OK, index=c[2];
    const struct setup_peer *peer=NULL;
    bool target_op=op==SETUP_SELECT || op==SETUP_ARM;
    bool valid=c[0]==SETUP_VERSION && op>=SETUP_SCAN && op<=SETUP_UNBOND;
    for (unsigned i=10;i<SETUP_BODY_SIZE;++i) if(c[i]) valid=false;
    if (op!=SETUP_READ && index) valid=false;
    if (target_op) { if (!address_valid(c[3],c+4)) valid=false; }
    else { for(unsigned i=3;i<10;++i) if(c[i]) valid=false; }
    if (!valid) status=SETUP_INVALID;
    else if (op==SETUP_STATUS) peer=s->selected ? &s->target : NULL;
    else if (op==SETUP_READ) {
        if(index>=s->count) status=SETUP_NOT_SEEN;
        else peer=&s->peers[index];
    } else if (connected || s->pair_until) status=SETUP_BUSY;
    else if (op==SETUP_SCAN) {
        setup_close(s); s->count=0; s->scan_until=now+SETUP_SCAN_MS;
    } else if (op==SETUP_UNBOND) {
        /* Clearing the store is explicit, never implicit; the caller performs
         * the Bluetooth unpair outside this pure-logic module. */
        setup_close(s); status=SETUP_OK;
    } else if (bonded) status=SETUP_BONDED; /* no replacement, no overwrite */
    else if (!s->scan_until) status=SETUP_EXPIRED;
    else if (op==SETUP_SELECT) {
        unsigned i;
        for(i=0;i<s->count;++i) if(same(&s->peers[i],c[3],c+4)) break;
        if(i==s->count) status=SETUP_NOT_SEEN;
        else { s->target=s->peers[i]; s->selected=true; peer=&s->target; }
    } else if (op==SETUP_ARM) {
        if(!s->selected || !same(&s->target,c[3],c+4)) status=SETUP_NOT_SELECTED;
        else { s->pair_until=now+SETUP_PAIR_MS; s->scan_until=0; s->claimed=false; peer=&s->target; }
    }
    reply(s,op,status,index,peer);
    s->reply[4] |= (bonded ? 16:0) | (connected ? 32:0);
    return true;
}
