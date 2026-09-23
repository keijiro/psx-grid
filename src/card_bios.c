#include "card.h"
#include "pad.h"
#include "audio.h"
#include <psxapi.h>
#include <psxetc.h>

// Development handoff candidate. Event completion and timeout are observable
// without WaitEvent/_card_wait; the SDK's audio timer remains installed. This
// is not a verified backend selection: compare it with card_psx.c in the
// platform validation before choosing the shipping implementation.
static int events[8], initialized, owned;
static const unsigned specs[]={EvSpIOE,EvSpTIMOUT,EvSpNEW,EvSpERROR};
static void drain(void) { for(int i=0;i<8;i++) TestEvent(events[i]); }
static CardResult completion(int software) {
    int offset=software?4:0;
    AudioTime deadline=audio_platform_time()+SEQUENCER_HZ;
    for(;;) {
        if(TestEvent(events[offset+2])) return CARD_CHANGED;
        if(TestEvent(events[offset+3])) return CARD_IO;
        if(TestEvent(events[offset+1])) return CARD_MISSING;
        if(TestEvent(events[offset])) return CARD_OK;
        if(audio_platform_time()>=deadline) return CARD_TIMEOUT;
    }
}
static CardResult abort_transfer(CardResult result) {
    // Stop the BIOS before returning a failed operation's buffer to its caller.
    // Ownership remains with the coordinator until its unconditional end call.
    StopCARD(); SIO_CTRL(0)=0x40; SIO_CTRL(0)=0;
    return result;
}
static void end(void *context) {
    (void)context;
    StopCARD();
    EnterCriticalSection();
    ChangeClearPAD(0); SIO_CTRL(0)=0x40; SIO_CTRL(0)=0;
    owned=0;
    ExitCriticalSection();
    pad_resume();
}
static CardResult present(void) {
    drain();
    if(!_card_info(0)) return CARD_IO;
    return completion(1);
}
static CardResult begin(void *context) {
    (void)context;
    if(owned) return CARD_IO;
    pad_suspend();
    int first=!initialized;
    if(first) {
        InitCARD(0);
        for(int i=0;i<8;i++) {
            events[i]=OpenEvent(i<4?HwCARD:SwCARD,specs[i%4],EvMdNOINTR,NULL);
            // Valid BIOS handles have the high bit set (0xf1000000 + slot).
            // Only -1 denotes allocation failure, not every negative int.
            if(events[i]==-1) {
                for(int j=0;j<i;j++) CloseEvent(events[j]);
                StopCARD(); pad_resume(); return CARD_IO;
            }
            EnableEvent(events[i]);
        }
        initialized=1;
    }
    drain();
    EnterCriticalSection();
    ChangeClearPAD(1); IRQ_MASK|=1u<<IRQ_SIO0; owned=1;
    ExitCriticalSection();
    StartCARD();
    // _bu_init scans both cards with blocking BIOS waits. A sector backend
    // must not enter that filesystem initialization outside our timeout loop.
    CardResult r=present();
    if(r==CARD_CHANGED) {
        drain();
        r=_card_clear(0)?completion(0):CARD_IO;
        if(!r) r=present();
    }
    if(r) end(NULL);
    return r;
}
static CardResult transfer(unsigned sector,uint8_t *data,int writing) {
    if(!owned || sector>=1024) return CARD_IO;
    // Check insertion state for each transfer, not just cached chooser status.
    // Only begin may acknowledge a new card; a mid-operation change aborts.
    CardResult r=present(); if(r) return abort_transfer(r);
    drain();
    if(!(writing?_card_write(0,sector,data):_card_read(0,sector,data))) return abort_transfer(CARD_IO);
    r=completion(0);
    return r?abort_transfer(r):CARD_OK;
}
static CardResult read_sector(void *context,unsigned sector,uint8_t *data) { (void)context; return transfer(sector,data,0); }
static CardResult write_sector(void *context,unsigned sector,const uint8_t *data) { (void)context; return transfer(sector,(uint8_t *)data,1); }
CardBackend card_platform_backend(void) { return (CardBackend){NULL,begin,end,read_sector,write_sector}; }
