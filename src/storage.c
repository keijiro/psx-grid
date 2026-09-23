#include "storage.h"
#include <stdio.h>
#include <string.h>
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint8_t xor_bytes(const uint8_t *p) { uint8_t v=0; for(int i=0;i<127;i++) v^=p[i]; return v; }
static StorageResult card_result(CardResult r) {
    static const StorageResult results[]={STORAGE_SAVED,STORAGE_NO_CARD,STORAGE_TIMEOUT,STORAGE_CHANGED,STORAGE_IO,STORAGE_UNFORMATTED,STORAGE_CARD_DAMAGED};
    return results[r];
}
static unsigned mapped(const Storage *s,unsigned sector) {
    for(unsigned i=0;i<20;i++) if(s->broken[i]==sector) return 36+i;
    return sector;
}
static CardResult read_sector(Storage *s,unsigned sector,uint8_t *data) { return s->card.read(s->card.context,mapped(s,sector),data); }
static CardResult write_sector(Storage *s,unsigned sector,const uint8_t *data) { return s->card.write(s->card.context,mapped(s,sector),data); }
static int free_entry(const uint8_t *d) { return get32(d)>=0xa0 && get32(d)<=0xa3; }
static CardResult directory(Storage *s) {
    uint8_t data[128];
    s->directory_ready=0;
    CardResult header=s->card.read(s->card.context,0,data);
    if(header) return header;
    if(memcmp(data,"MC",2)) return CARD_UNFORMATTED;
    for(unsigned i=0;i<20;i++) {
        CardResult r=s->card.read(s->card.context,16+i,data); if(r) return r;
        if(xor_bytes(data)!=data[127]) return CARD_DAMAGED;
        unsigned sector=get32(data); s->broken[i]=sector;
        if(sector==UINT32_MAX) continue;
        if(sector>=1024 || (sector>=16 && sector<56)) return CARD_DAMAGED;
        for(unsigned j=0;j<i;j++) if(s->broken[j]==sector) return CARD_DAMAGED;
    }
    for(int i=0;i<16;i++) {
        CardResult r=read_sector(s,i,s->directory[i]); if(r) return r;
    }
    if(memcmp(s->directory[0],"MC",2)) return CARD_UNFORMATTED;
    if(xor_bytes(s->directory[0])!=s->directory[0][127]) return CARD_DAMAGED;
    // Torn directory publication must not hide an older intact generation.
    // Read-only discovery may inspect individually valid entries, but any
    // directory damage disables all allocation and retirement on this card.
    s->directory_ready=1;
    for(int i=1;i<16;i++) if(xor_bytes(s->directory[i])!=s->directory[i][127]) return CARD_DAMAGED;
    s->free_blocks=0;
    uint8_t owned[16]={0};
    // Validate every allocated chain before selecting a free block. An entry
    // marked free is unsafe if an unrelated file still points into it.
    for(int i=1;i<16;i++) {
        uint8_t *d=s->directory[i]; uint32_t state=get32(d);
        if(free_entry(d)) { s->free_blocks++; continue; }
        if(state!=0x51 && state!=0x52 && state!=0x53) return CARD_DAMAGED;
        if(state!=0x51) continue;
        uint32_t bytes=get32(d+4); if(!bytes || bytes>15*SCORE_FILE_BYTES || bytes%SCORE_FILE_BYTES) return CARD_DAMAGED;
        int block=i, count=bytes/SCORE_FILE_BYTES;
        for(int n=0;n<count;n++) {
            if(block<1 || block>15 || owned[block]) return CARD_DAMAGED;
            owned[block]=1; const uint8_t *entry=s->directory[block];
            if(get32(entry)!=(unsigned)(n==0?0x51:n==count-1?0x53:0x52)) return CARD_DAMAGED;
            unsigned next=entry[8]|entry[9]<<8;
            if(n==count-1) { if(next!=65535) return CARD_DAMAGED; }
            else block=next+1;
        }
    }
    for(int i=1;i<16;i++) if(!free_entry(s->directory[i]) && !owned[i]) return CARD_DAMAGED;
    return CARD_OK;
}
static int identity(const uint8_t *d,int *slot,uint32_t *generation) {
    if(xor_bytes(d)!=d[127] || get32(d)!=0x51 || get32(d+4)!=SCORE_FILE_BYTES || d[8]!=255 || d[9]!=255 || memcmp(d+10,"BIJACQUARD",10) || d[30]) return 0;
    if(d[20]<'0' || d[20]>'1' || d[21]<'0' || d[21]>'9') return 0;
    int number=(d[20]-'0')*10+d[21]-'0'; if(number<1 || number>15) return 0;
    uint32_t gen=0;
    for(int i=22;i<30;i++) {
        int c=d[i],v=c>='0' && c<='9'?c-'0':c>='A' && c<='F'?c-'A'+10:-1;
        if(v<0) return 0;
        gen=(gen<<4)|v;
    }
    if(!gen) return 0;
    *slot=number; *generation=gen; return 1;
}
static CardResult read_block(Storage *s,int block) {
    for(int i=0;i<64;i++) { CardResult r=read_sector(s,block*64+i,s->readback+i*128); if(r) return r; }
    return CARD_OK;
}
typedef struct { int block; uint32_t generation, maximum; StorageResult result; } Discovery;
static Discovery discover(Storage *s,int slot) {
    Discovery found={0,0,0,STORAGE_EMPTY};
    uint32_t newer=0;
    for(int block=1;block<16;block++) {
        int number; uint32_t generation;
        if(!identity(s->directory[block],&number,&generation) || number!=slot) continue;
        if(generation>found.maximum) found.maximum=generation;
        CardResult r=read_block(s,block); if(r) { found.result=card_result(r); return found; }
        int payload_slot; uint32_t payload_generation;
        FormatResult format=score_format_decode(s->readback,sizeof(s->readback),&s->incoming,&payload_slot,&payload_generation);
        if(format==FORMAT_NEWER) { if(generation>newer) newer=generation; continue; }
        if(format!=FORMAT_OK || payload_slot!=slot || payload_generation!=generation) {
            if(found.result==STORAGE_EMPTY) found.result=STORAGE_CORRUPT;
            continue;
        }
        // Equal generations use the lower directory index, independent of
        // scan caching. Never fall back past a newer musical schema.
        if(!found.block || generation>found.generation) { found.block=block; found.generation=generation; found.result=STORAGE_SAVED; }
    }
    if(newer>=found.generation && newer) found.result=STORAGE_NEWER;
    return found;
}
void storage_init(Storage *s,CardBackend backend) { memset(s,0,sizeof(*s)); s->card=backend; s->free_blocks=-1; }
static StorageResult begin(Storage *s,int writing) {
    s->free_blocks=-1;
    CardResult r=s->card.begin(s->card.context);
    if(r) return card_result(r);
    r=directory(s);
    if(r) {
        s->free_blocks=-1;
        if(!writing && r==CARD_DAMAGED && s->directory_ready) return STORAGE_SAVED;
        s->card.end(s->card.context); return card_result(r);
    }
    return STORAGE_SAVED;
}
static StorageResult finish(Storage *s,int slot,StorageResult result) {
    s->card.end(s->card.context); s->slots[slot-1]=result; return result;
}
StorageResult storage_refresh(Storage *s,int slot) {
    if(slot<1 || slot>15) return STORAGE_IO;
    StorageResult r=begin(s,0); if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot); return finish(s,slot,d.result);
}
StorageResult storage_load(Storage *s,int slot) {
    if(slot<1 || slot>15) return STORAGE_IO;
    StorageResult r=begin(s,0); if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot);
    if(d.result!=STORAGE_SAVED) return finish(s,slot,d.result);
    CardResult io=read_block(s,d.block);
    if(io) return finish(s,slot,card_result(io));
    int number; uint32_t generation;
    FormatResult format=score_format_decode(s->readback,sizeof(s->readback),&s->incoming,&number,&generation);
    if(format!=FORMAT_OK || number!=slot || generation!=d.generation) return finish(s,slot,format==FORMAT_NEWER?STORAGE_NEWER:STORAGE_CORRUPT);
    return finish(s,slot,STORAGE_SAVED);
}
static CardResult publish_entry(Storage *s,int block,uint8_t *entry) {
    uint8_t check[128]; entry[127]=xor_bytes(entry);
    CardResult r=write_sector(s,block,entry); if(r) return r;
    r=read_sector(s,block,check); if(r) return r;
    if(memcmp(entry,check,sizeof(check))) return CARD_IO;
    memcpy(s->directory[block],entry,128); return CARD_OK;
}
StorageResult storage_save(Storage *s,int slot,const Score *score) {
    if(slot<1 || slot>15) return STORAGE_IO;
    if(score_format_encode(score,s->save,slot,1)!=FORMAT_OK) return STORAGE_SCORE_FULL;
    StorageResult r=begin(s,1); if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot);
    if(d.result!=STORAGE_EMPTY && d.result!=STORAGE_SAVED && d.result!=STORAGE_CORRUPT) return finish(s,slot,d.result);
    if(d.maximum==UINT32_MAX) return finish(s,slot,STORAGE_GENERATION_FULL);
    if(d.result==STORAGE_SAVED) for(int i=1;i<16;i++) if(i!=d.block) {
        int number; uint32_t old;
        if(!identity(s->directory[i],&number,&old) || number!=slot) continue;
        uint8_t entry[128]; memcpy(entry,s->directory[i],128); entry[0]=0xa1;
        CardResult io=publish_entry(s,i,entry);
        if(io) return finish(s,slot,card_result(io));
        s->free_blocks++;
    }
    int block=0; for(int i=1;i<16;i++) if(free_entry(s->directory[i])) { block=i; break; }
    if(!block) return finish(s,slot,STORAGE_NO_SPACE);
    uint32_t generation=d.maximum+1;
    score_format_set_generation(s->save,generation);
    // Write the unallocated block first. A valid directory entry is the final
    // commit evidence; a partial payload is never advertised as a new save.
    for(int i=0;i<64;i++) {
        CardResult io=write_sector(s,block*64+i,s->save+i*128);
        if(io) return finish(s,slot,card_result(io));
    }
    CardResult io=read_block(s,block); if(io) return finish(s,slot,card_result(io));
    if(memcmp(s->save,s->readback,SCORE_FILE_BYTES) || score_format_decode(s->readback,SCORE_FILE_BYTES,&s->incoming,NULL,NULL)!=FORMAT_OK)
        return finish(s,slot,STORAGE_CORRUPT);
    uint8_t entry[128]={0}; entry[0]=0x51; entry[5]=0x20; entry[8]=entry[9]=255;
    snprintf((char *)entry+10,21,"BIJACQUARD%02d%08lX",slot,(unsigned long)generation);
    io=publish_entry(s,block,entry); if(io) return finish(s,slot,card_result(io));
    s->free_blocks--;
    // Only this slot's recognized one-block files can be retired. Failure now
    // is a completed save with cleanup pending, not a failed payload write.
    for(int i=1;i<16;i++) if(i!=block) {
        int number; uint32_t old;
        if(!identity(s->directory[i],&number,&old) || number!=slot) continue;
        memcpy(entry,s->directory[i],128); entry[0]=0xa1;
        io=publish_entry(s,i,entry);
        if(io) return finish(s,slot,STORAGE_CLEANUP);
        s->free_blocks++;
    }
    return finish(s,slot,STORAGE_SAVED);
}
const char *storage_message(StorageResult r) {
    static const char *messages[]={"CHECK","EMPTY","SAVED","BUSY","CORRUPT","NEWER VERSION","NO CARD","CARD TIMEOUT","CARD CHANGED","CARD I/O ERROR","UNFORMATTED","CARD DAMAGED","NO FREE BLOCK","SCORE FULL","SAVED / CLEANUP PENDING","GENERATION FULL"};
    return messages[r];
}
