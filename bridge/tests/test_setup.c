/* SPDX-License-Identifier: MIT */
#include "setup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct setup_peer a={.type=1,.addr={1,2,3,4,5,0xc6},.rssi=-42,.name="Sofle <untrusted>"};
static uint8_t cmd[SETUP_WIRE_SIZE];
static void make(uint8_t op, const struct setup_peer *p) {
    memset(cmd,0,sizeof(cmd)); cmd[0]=SETUP_REPORT_ID; cmd[1]=SETUP_VERSION; cmd[2]=op;
    if(p) { cmd[4]=p->type; memcpy(cmd+5,p->addr,6); }
}
static uint8_t run(struct setup_state *s,uint64_t t,bool conn,bool bond) {
    assert(setup_feature_set(s,t,conn,bond,0x0340,33,cmd,33));
    return s->reply[3];
}
static void ready(struct setup_state *s, uint64_t t) {
    memset(s,0,sizeof(*s)); make(SETUP_SCAN,NULL); assert(run(s,t,0,0)==SETUP_OK);
    setup_observe(s,t+1,&a); assert(s->count==1);
    make(SETUP_SELECT,&a); assert(run(s,t+2,0,0)==SETUP_OK);
    make(SETUP_ARM,&a); assert(run(s,t+3,0,0)==SETUP_OK);
}
int main(void) {
    struct setup_state s={0};
    assert(!setup_target_allowed(&s,0,a.type,a.addr));
    make(SETUP_ARM,&a); assert(run(&s,1,0,0)==SETUP_EXPIRED);
    for(unsigned v=0;v<65536;++v) {
        assert(setup_feature_get_valid((uint16_t)v,33)==(v==0x0340));
        assert(!setup_feature_get_valid((uint16_t)v,32));
    }
    make(SETUP_SCAN,NULL);
    for(unsigned n=0;n<70;++n) assert(setup_feature_set(&s,1,0,0,0x0340,33,cmd,n)==(n==33));
    assert(!setup_feature_set(&s,1,0,0,0x0340,33,NULL,33));
    cmd[0]=1; assert(!setup_feature_set(&s,1,0,0,0x0340,33,cmd,33));
    for(unsigned op=0;op<256;++op) {
        struct setup_state fresh={0}; make((uint8_t)op,NULL);
        if(op==SETUP_SCAN || op==SETUP_READ || op==SETUP_STATUS) continue;
        assert(run(&fresh,1,0,0)==SETUP_INVALID);
        assert(!fresh.pair_until);
    }
    for(unsigned i=11;i<33;++i) { make(SETUP_SCAN,NULL); cmd[i]=1; assert(run(&s,2,0,0)==SETUP_INVALID); }
    make(SETUP_SCAN,NULL); cmd[1]=2; assert(run(&s,2,0,0)==SETUP_INVALID);
    for(unsigned type=2;type<256;++type) { make(SETUP_SELECT,&a); cmd[4]=(uint8_t)type; assert(run(&s,2,0,0)==SETUP_INVALID); }
    make(SETUP_ARM,&a); memset(cmd+5,0,6); assert(run(&s,2,0,0)==SETUP_INVALID);
    memset(cmd+5,255,6); assert(run(&s,2,0,0)==SETUP_INVALID);
    /* First advertiser and duplicate names are NOT a selection policy. */
    memset(&s,0,sizeof(s)); make(SETUP_SCAN,NULL); assert(run(&s,10,0,0)==SETUP_OK);
    struct setup_peer neighbor=a; neighbor.addr[0]=99;
    setup_observe(&s,11,&neighbor); setup_observe(&s,12,&a);
    assert(!setup_target_allowed(&s,13,neighbor.type,neighbor.addr));
    assert(!setup_target_allowed(&s,13,a.type,a.addr));
    make(SETUP_SELECT,&a); assert(run(&s,13,0,0)==SETUP_OK);
    assert(!setup_target_allowed(&s,14,a.type,a.addr));
    make(SETUP_ARM,&neighbor); assert(run(&s,14,0,0)==SETUP_NOT_SELECTED);
    make(SETUP_ARM,&a); assert(run(&s,14,0,0)==SETUP_OK);
    assert(!setup_claim(&s,15,neighbor.type,neighbor.addr));
    assert(setup_claim(&s,15,a.type,a.addr));
    ready(&s,100);
    assert(setup_target_allowed(&s,104,a.type,a.addr));
    assert(!setup_target_allowed(&s,104,0,a.addr));
    struct setup_peer b=a; b.addr[0]++;
    assert(!setup_target_allowed(&s,104,b.type,b.addr));
    assert(!setup_claim(&s,104,b.type,b.addr));
    assert(setup_claim(&s,104,a.type,a.addr));
    assert(!setup_claim(&s,105,a.type,a.addr));
    assert(setup_target_allowed(&s,20102,a.type,a.addr));
    assert(!setup_target_allowed(&s,20103,a.type,a.addr)); /* deadline even before tick */
    setup_expire(&s,20103); assert(!s.pair_until && !s.selected);
    ready(&s,200); setup_close(&s); assert(!setup_target_allowed(&s,204,a.type,a.addr));
    make(SETUP_SCAN,NULL); assert(run(&s,300,1,0)==SETUP_BUSY);
    assert(run(&s,300,0,0)==SETUP_OK);
    setup_observe(&s,301,&a); setup_observe(&s,302,&a); assert(s.count==1);
    make(SETUP_SELECT,&b); assert(run(&s,303,0,0)==SETUP_NOT_SEEN);
    make(SETUP_ARM,&a); assert(run(&s,303,0,0)==SETUP_NOT_SELECTED);
    make(SETUP_SELECT,&a); assert(run(&s,303,0,1)==SETUP_BONDED);
    assert(run(&s,303,1,0)==SETUP_BUSY);
    assert(run(&s,303,0,0)==SETUP_OK);
    make(SETUP_ARM,&b); assert(run(&s,304,0,0)==SETUP_NOT_SELECTED);
    make(SETUP_ARM,&a); assert(run(&s,304,0,1)==SETUP_BONDED);
    assert(run(&s,304,0,0)==SETUP_OK);
    assert(run(&s,305,0,0)==SETUP_BUSY); /* cannot extend window */
    make(SETUP_SCAN,NULL); assert(run(&s,305,0,0)==SETUP_BUSY);
    make(SETUP_STATUS,NULL); assert(run(&s,305,1,1)==SETUP_OK);
    make(SETUP_READ,NULL); assert(run(&s,305,1,1)==SETUP_OK);
    assert(s.reply[7]==a.type && !memcmp(s.reply+8,a.addr,6));
    cmd[3]=255; assert(run(&s,305,0,0)==SETUP_NOT_SEEN);
    setup_close(&s); make(SETUP_SCAN,NULL); assert(run(&s,400,0,0)==SETUP_OK);
    for(unsigned i=0;i<255;++i) { b.addr[0]=(uint8_t)i; setup_observe(&s,401,&b); }
    assert(s.count==SETUP_MAX_CANDIDATES);
    make(SETUP_SELECT,&a); assert(run(&s,30400,0,0)==SETUP_EXPIRED);
    /* Independently walk the vendor descriptor: one TLC, only feature64/body32. */
    unsigned page=0,id=0,size=0,count=0,bits=0,tlc=0;
    for(size_t i=0;i<sizeof(setup_report_desc);) {
        uint8_t p=setup_report_desc[i++]; unsigned n=p&3; if(n==3)n=4;
        assert(i+n<=sizeof(setup_report_desc)); unsigned v=0;
        for(unsigned j=0;j<n;++j) v|=(unsigned)setup_report_desc[i+j]<<(j*8); i+=n;
        switch(p&0xfc) {
        case 0x04: page=v; break;
        case 0x84: id=v; break;
        case 0x74: size=v; break;
        case 0x94: count=v; break;
        case 0xa0: assert(page==0xff31 && v==1); ++tlc; break;
        case 0xb0: assert(id==0x40); bits+=size*count; break;
        case 0x80: case 0x90: assert(!"no input/output on setup interface");
        }
    }
    assert(tlc==1 && bits==256);
    puts("PASS: setup feature envelope/type/ID/size/version/reserved/op validation, wildcard rejection, scan dedupe/capacity, exact address+type selection, busy/bond gate, one attempt, hard deadline, expiry/USB close, isolated vendor descriptor");
}
