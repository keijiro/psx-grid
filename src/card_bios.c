#include "card.h"
#include <psxapi.h>
#include <psxetc.h>
#include <sys/fcntl.h>
#include <string.h>

enum { EVENT_COUNT=8, WAIT_TICKS=5*4233600 };
static const unsigned specs[4]={EvSpIOE,EvSpTIMOUT,EvSpNEW,EvSpERROR};
// BIOS event handles have their high bit set, so only -1 denotes no event.
static int events[EVENT_COUNT], file_event=-1, fd=-1, owned, started, poisoned, initialized;
static struct DIRENTRY directory_entry;

// Timer 0 is stopped while the BIOS owns IRQ dispatch. Timer 2 remains a
// free-running read-only deadline source; restoration rebases its audio sample
// so any number of wraps spent in a card session cannot advance music.
static uint16_t tick(void) { return (uint16_t)TIMER_VALUE(2); }
static void drain(void) {
    for(int i=0;i<EVENT_COUNT;i++) TestEvent(events[i]);
    if(file_event!=-1) TestEvent(file_event);
}
static CardResult wait_card(int file,int hardware) {
    int offset=hardware?0:4;
    uint16_t last=tick();
    unsigned elapsed=0;
    for(;;) {
        if(TestEvent(events[offset+2])) return CARD_CHANGED;
        if(TestEvent(events[offset+3])) return CARD_IO;
        if(TestEvent(events[offset+1])) return CARD_MISSING;
        if(file!=-1) {
            if(TestEvent(file)) return CARD_OK;
        } else if(TestEvent(events[offset])) return CARD_OK;
        uint16_t now=tick();
        elapsed+=(uint16_t)(now-last); last=now;
        if(elapsed>=WAIT_TICKS) return CARD_TIMEOUT;
    }
}
static CardResult path(char out[26],const char *name) {
    if(strlen(name)>20 || !name[0]) return CARD_IO;
    memcpy(out,"bu00:",5);
    strcpy(out+5,name);
    return CARD_OK;
}
static CardResult probe(int initial) {
    drain();
    if(!_card_info(0)) return CARD_IO;
    CardResult r=wait_card(-1,0);
    if(r==CARD_CHANGED && initial) {
        // Only session entry may acknowledge insertion. A later NEW event
        // means that the card used for discovery may have been replaced.
        drain();
        if(!_card_clear(0)) return CARD_IO;
        r=wait_card(-1,1);
        if(r) return r;
        drain();
        if(!_card_info(0)) return CARD_IO;
        r=wait_card(-1,0);
    }
    return r;
}
static CardResult check(void *context) {
    (void)context;
    if(!owned || poisoned) return CARD_CHANGED;
    CardResult r=probe(0);
    if(r) poisoned=1;
    return r;
}
static void end(void *context) {
    (void)context;
    // Once replacement or timeout is seen, stop card traffic before releasing
    // a BIOS handle; do not let close issue another transfer to a new card.
    if(poisoned && started) { StopCARD(); started=0; }
    if(fd>=0) { close(fd); fd=-1; }
    if(file_event!=-1) { CloseEvent(file_event); file_event=-1; }
    if(started) StopCARD();
    for(int i=0;i<EVENT_COUNT;i++) if(events[i]!=-1) {
        CloseEvent(events[i]); events[i]=-1;
    }
    owned=started=poisoned=0;
}
static CardResult begin(void *context) {
    (void)context;
    if(owned) return CARD_IO;
    for(int i=0;i<EVENT_COUNT;i++) events[i]=-1;
    file_event=fd=-1;
    poisoned=started=0;
    for(int i=0;i<EVENT_COUNT;i++) {
        events[i]=OpenEvent(i<4?HwCARD:SwCARD,specs[i%4],EvMdNOINTR,NULL);
        if(events[i]==-1) { end(NULL); return CARD_IO; }
        EnableEvent(events[i]);
    }
    // _bu_init installs the BIOS filesystem driver. StopCARD suspends card
    // service, but does not unload that driver between modal sessions.
    if(!initialized) InitCARD(0);
    StartCARD();
    started=1;
    if(!initialized) { _bu_init(); initialized=1; }
    owned=1;
    CardResult r=probe(1);
    if(!r) {
        drain();
        if(!_card_load(0)) r=CARD_IO;
        else {
            r=wait_card(-1,0);
            if(r==CARD_CHANGED) r=CARD_UNFORMATTED;
        }
    }
    if(r) { poisoned=1; end(NULL); }
    return r;
}
static CardResult list(void *context,int index,CardFile *file) {
    (void)context;
    if(!owned || fd>=0) return CARD_IO;
    CardResult r=check(NULL);
    if(r) return r;
    struct DIRENTRY *entry=index?nextfile(&directory_entry):
        firstfile("bu00:*",&directory_entry);
    if(!entry) {
        r=check(NULL);
        return r?r:CARD_END;
    }
    // A fixed 20-byte BIOS field need not contain a trailing NUL when full.
    // OpenBIOS writes that terminator over attr, so the enumerator's file
    // selection and storage's name/size checks must not depend on attr.
    memcpy(file->name,entry->name,20); file->name[20]=0;
    file->size=entry->size;
    return check(NULL);
}
static CardResult open_file(const char *name,int mode) {
    if(!owned || fd>=0) return CARD_IO;
    char filename[26];
    CardResult r=path(filename,name);
    if(r) return r;
    r=check(NULL); if(r) return r;
    // FASYNC moves the long payload transfer onto BIOS events. Open and
    // metadata operations remain synchronous and need platform validation.
    fd=open(filename,mode|FASYNC);
    if(fd<0) return CARD_IO;
    file_event=OpenEvent((unsigned)fd,EvSpIOE,EvMdNOINTR,NULL);
    if(file_event==-1) { close(fd); fd=-1; return CARD_IO; }
    EnableEvent(file_event);
    return check(NULL);
}
static CardResult open_read(void *context,const char *name) {
    (void)context;
    return open_file(name,FREAD);
}
static CardResult open_create(void *context,const char *name) {
    (void)context;
    return open_file(name,FWRITE|FCREATE|FNBLOCKS(1));
}
static CardResult transfer(uint8_t *data,int size,int writing) {
    if(fd<0 || size<=0 || size%128) return CARD_IO;
    CardResult r=check(NULL); if(r) return r;
    drain();
    int count=writing?write(fd,data,size):read(fd,data,size);
    // An asynchronous BIOS transfer returns zero when queued, then reports
    // completion through the file event. A positive count must still be exact.
    if(count!=0 && count!=size) { poisoned=1; return CARD_IO; }
    r=wait_card(file_event,0);
    if(r) { poisoned=1; return r; }
    return check(NULL);
}
static CardResult read_file(void *context,uint8_t *data,int size) {
    (void)context; return transfer(data,size,0);
}
static CardResult write_file(void *context,const uint8_t *data,int size) {
    (void)context; return transfer((uint8_t *)data,size,1);
}
static CardResult close_file(void *context) {
    (void)context;
    if(fd<0) return CARD_IO;
    int handle=fd;
    fd=-1;
    if(poisoned && started) { StopCARD(); started=0; }
    if(file_event!=-1) { CloseEvent(file_event); file_event=-1; }
    int result=close(handle);
    if(result<0) return CARD_IO;
    if(poisoned) return CARD_IO;
    return check(NULL);
}
static CardResult erase_file(void *context,const char *name) {
    (void)context;
    if(!owned || fd>=0) return CARD_IO;
    char filename[26];
    CardResult r=path(filename,name);
    if(r) return r;
    r=check(NULL); if(r) return r;
    if(!erase(filename)) return CARD_IO;
    return check(NULL);
}
CardBackend card_platform_backend(void) {
    return (CardBackend){NULL,begin,end,check,list,open_read,open_create,
        read_file,write_file,close_file,erase_file};
}
