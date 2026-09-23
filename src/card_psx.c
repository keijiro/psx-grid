#include "card.h"
#include "pad.h"
#include "audio.h"
#include <psxapi.h>
#include <psxetc.h>
#include <string.h>

// This provisional backend keeps the SDK timer dispatcher installed. All byte
// waits run on the main thread; IRQ7 is masked by pad_suspend, so ACK status
// belongs to this transfer rather than the pad callback. Emulator timing is
// measured by the storage fixture; hardware timing remains unverified.
static int owned, guard_change;
static uint16_t counter(void) { return (uint16_t)TIMER_VALUE(2); }
#define BYTE_TIMEOUT 8468u
#define SETTLE 128u
static void settle(void) { uint16_t at=counter(); while((uint16_t)(counter()-at)<SETTLE) {} }
static CardResult exchange(uint8_t out,uint8_t *in,int last) {
    uint16_t at=counter();
    while(!(SIO_STAT(0)&1)) if((uint16_t)(counter()-at)>=BYTE_TIMEOUT) return CARD_TIMEOUT;
    SIO_CTRL(0)=0x1013; SIO_DATA(0)=out;
    at=counter();
    while(!(SIO_STAT(0)&2)) if((uint16_t)(counter()-at)>=BYTE_TIMEOUT) return CARD_TIMEOUT;
    *in=SIO_DATA(0);
    if(!last) {
        at=counter();
        while(!(SIO_STAT(0)&0x200)) if((uint16_t)(counter()-at)>=BYTE_TIMEOUT) return CARD_TIMEOUT;
        SIO_CTRL(0)=0x1013; settle();
    }
    return CARD_OK;
}
static CardResult transfer(unsigned sector,uint8_t *data,int writing) {
    if(!owned || sector>=1024) return CARD_IO;
    uint8_t tx[140]={0},rx[140]; int length=writing?138:140;
    tx[0]=0x81; tx[1]=writing?'W':'R'; tx[4]=sector>>8; tx[5]=sector;
    if(writing) {
        memcpy(tx+6,data,128); tx[134]=tx[4]^tx[5];
        for(int i=0;i<128;i++) tx[134]^=data[i];
    }
    SIO_CTRL(0)=0x40; SIO_MODE(0)=0x0d; SIO_BAUD(0)=0x88; SIO_CTRL(0)=0x1003; settle();
    CardResult result=CARD_OK;
    for(int i=0;i<length;i++) {
        result=exchange(tx[i],&rx[i],i==length-1);
        if(result) { if(i<2) result=CARD_MISSING; break; }
        if(i==1 && guard_change && (rx[1]&8)) { result=CARD_CHANGED; break; }
    }
    SIO_CTRL(0)=0; settle();
    if(result) return result;
    if(rx[2]!=0x5a || rx[3]!=0x5d || rx[length-1]!=0x47) return CARD_IO;
    if(writing) {
        if(rx[135]!=0x5c || rx[136]!=0x5d) return CARD_IO;
    } else {
        if(rx[6]!=0x5c || rx[7]!=0x5d || rx[8]!=tx[4] || rx[9]!=tx[5]) return CARD_IO;
        uint8_t sum=rx[8]^rx[9]; for(int i=0;i<128;i++) sum^=rx[10+i];
        if(sum!=rx[138]) return CARD_IO;
        memcpy(data,rx+10,128);
    }
    return CARD_OK;
}
static void end(void *context) { (void)context; SIO_CTRL(0)=0x40; SIO_CTRL(0)=0; owned=0; pad_resume(); }
static CardResult begin(void *context) {
    (void)context;
    if(owned) return CARD_IO;
    pad_suspend(); owned=1; guard_change=0;
    uint8_t data[128]; CardResult r=transfer(0,data,0);
    if(!r && (data[0]!='M' || data[1]!='C')) r=CARD_UNFORMATTED;
    // Reset the card's insertion flag using its reserved write-test sector,
    // preserving those bytes. Subsequent insertion flags abort the session,
    // including a replacement between directory discovery and the first write.
    if(!r) r=transfer(63,data,0);
    if(!r) r=transfer(63,data,1);
    if(!r) { guard_change=1; r=transfer(63,data,0); }
    if(r) end(NULL);
    return r;
}
static CardResult read_sector(void *context,unsigned sector,uint8_t *data) { (void)context; return transfer(sector,data,0); }
static CardResult write_sector(void *context,unsigned sector,const uint8_t *data) { (void)context; return transfer(sector,(uint8_t *)data,1); }
CardBackend card_platform_backend(void) { return (CardBackend){NULL,begin,end,read_sector,write_sector}; }
