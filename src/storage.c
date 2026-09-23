#include "storage.h"
#include <stdio.h>
#include <string.h>

static StorageResult card_result(CardResult r) {
    switch(r) {
    case CARD_MISSING: return STORAGE_NO_CARD;
    case CARD_TIMEOUT: return STORAGE_TIMEOUT;
    case CARD_CHANGED: return STORAGE_CHANGED;
    case CARD_UNFORMATTED: return STORAGE_UNFORMATTED;
    case CARD_DAMAGED: return STORAGE_CARD_DAMAGED;
    default: return STORAGE_IO;
    }
}
static int identity(const CardFile *file,int *slot,uint32_t *generation) {
    const char *name=file->name;
    if(memcmp(name,"BIJACQUARD",10) || name[20] || name[10]<'0' || name[10]>'1' ||
       name[11]<'0' || name[11]>'9') return 0;
    int number=(name[10]-'0')*10+name[11]-'0';
    if(number<1 || number>15) return 0;
    uint32_t gen=0;
    for(int i=12;i<20;i++) {
        int c=name[i],v=c>='0' && c<='9'?c-'0':c>='A' && c<='F'?c-'A'+10:-1;
        if(v<0) return 0;
        gen=(gen<<4)|v;
    }
    if(!gen) return 0;
    *slot=number; *generation=gen; return 1;
}
static CardResult read_file(Storage *s,const char *name) {
    CardResult r=s->card.check(s->card.context);
    if(r) return r;
    r=s->card.open_read(s->card.context,name);
    if(r) return r;
    CardResult transfer=s->card.read(s->card.context,s->readback,SCORE_FILE_BYTES);
    CardResult closed=s->card.close(s->card.context);
    if(transfer) return transfer;
    if(closed) return closed;
    return s->card.check(s->card.context);
}
static CardResult write_file(Storage *s,const char *name) {
    CardResult r=s->card.check(s->card.context);
    if(r) return r;
    r=s->card.open_create(s->card.context,name);
    if(r) return r;
    CardResult transfer=s->card.write(s->card.context,s->save,SCORE_FILE_BYTES);
    CardResult closed=s->card.close(s->card.context);
    if(transfer) return transfer;
    if(closed) return closed;
    return s->card.check(s->card.context);
}
typedef struct { int file; uint32_t generation, maximum; StorageResult result; } Discovery;
static Discovery discover(Storage *s,int slot) {
    Discovery found={-1,0,0,STORAGE_EMPTY};
    uint32_t newer=0;
    for(int i=0;i<s->file_count;i++) {
        int number; uint32_t generation;
        if(!identity(&s->files[i],&number,&generation) || number!=slot) continue;
        if(generation>found.maximum) found.maximum=generation;
        if(s->files[i].size!=SCORE_FILE_BYTES) {
            if(found.result==STORAGE_EMPTY) found.result=STORAGE_CORRUPT;
            continue;
        }
        CardResult r=read_file(s,s->files[i].name);
        if(r) { found.result=card_result(r); return found; }
        int payload_slot; uint32_t payload_generation;
        FormatResult format=score_format_decode(s->readback,SCORE_FILE_BYTES,
            &s->incoming,&payload_slot,&payload_generation);
        if(format==FORMAT_NEWER) { if(generation>newer) newer=generation; continue; }
        if(format!=FORMAT_OK || payload_slot!=slot || payload_generation!=generation) {
            if(found.result==STORAGE_EMPTY) found.result=STORAGE_CORRUPT;
            continue;
        }
        // Enumeration order is the BIOS directory order. The first valid
        // entry wins ties, as the former lower-directory-index rule did.
        if(found.file<0 || generation>found.generation) {
            found.file=i; found.generation=generation; found.result=STORAGE_SAVED;
        }
    }
    if(newer>=found.generation && newer) found.result=STORAGE_NEWER;
    return found;
}
void storage_init(Storage *s,CardBackend backend) {
    memset(s,0,sizeof(*s)); s->card=backend; s->free_blocks=-1;
}
static StorageResult begin(Storage *s) {
    s->free_blocks=-1; s->file_count=0;
    CardResult r=s->card.begin(s->card.context);
    if(r) return card_result(r);
    int used=0;
    for(int i=0;i<=15;i++) {
        CardFile file;
        r=s->card.list(s->card.context,i,&file);
        if(r==CARD_END) break;
        if(r) { s->card.end(s->card.context); return card_result(r); }
        if(i==15 || file.size<=0 || file.size%SCORE_FILE_BYTES ||
           used+file.size/SCORE_FILE_BYTES>15) {
            s->card.end(s->card.context); return STORAGE_CARD_DAMAGED;
        }
        used+=file.size/SCORE_FILE_BYTES;
        s->files[s->file_count++]=file;
    }
    r=s->card.check(s->card.context);
    if(r) { s->card.end(s->card.context); return card_result(r); }
    s->free_blocks=15-used;
    return STORAGE_SAVED;
}
static StorageResult finish(Storage *s,int slot,StorageResult result) {
    s->card.end(s->card.context);
    s->slots[slot-1]=result;
    return result;
}
StorageResult storage_refresh(Storage *s,int slot) {
    if(slot<1 || slot>15) return STORAGE_IO;
    StorageResult r=begin(s);
    if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot);
    return finish(s,slot,d.result);
}
StorageResult storage_load(Storage *s,int slot) {
    if(slot<1 || slot>15) return STORAGE_IO;
    StorageResult r=begin(s);
    if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot);
    if(d.result!=STORAGE_SAVED) return finish(s,slot,d.result);
    CardResult io=read_file(s,s->files[d.file].name);
    if(io) return finish(s,slot,card_result(io));
    int number; uint32_t generation;
    FormatResult format=score_format_decode(s->readback,SCORE_FILE_BYTES,
        &s->incoming,&number,&generation);
    if(format!=FORMAT_OK || number!=slot || generation!=d.generation)
        return finish(s,slot,format==FORMAT_NEWER?STORAGE_NEWER:STORAGE_CORRUPT);
    return finish(s,slot,STORAGE_SAVED);
}
static CardResult retire(Storage *s,int slot,int keep) {
    for(int i=0;i<s->file_count;i++) if(i!=keep) {
        int number; uint32_t generation;
        if(!identity(&s->files[i],&number,&generation) || number!=slot ||
           s->files[i].size!=SCORE_FILE_BYTES) continue;
        CardResult r=s->card.check(s->card.context);
        if(r) return r;
        r=s->card.erase(s->card.context,s->files[i].name);
        if(r) return r;
        s->free_blocks++;
        // Erased entries are excluded from any later retirement pass.
        s->files[i].name[0]=0;
    }
    return CARD_OK;
}
StorageResult storage_save(Storage *s,int slot,const Score *score) {
    if(slot<1 || slot>15) return STORAGE_IO;
    if(score_format_encode(score,s->save,slot,1)!=FORMAT_OK) return STORAGE_SCORE_FULL;
    StorageResult r=begin(s);
    if(r!=STORAGE_SAVED) return s->slots[slot-1]=r;
    Discovery d=discover(s,slot);
    if(d.result!=STORAGE_EMPTY && d.result!=STORAGE_SAVED && d.result!=STORAGE_CORRUPT)
        return finish(s,slot,d.result);
    if(d.maximum==UINT32_MAX) return finish(s,slot,STORAGE_GENERATION_FULL);
    // Retire only obsolete recognized generations, and only when a verified
    // generation exists. The latest valid generation is never spent for space.
    if(d.file>=0) {
        CardResult io=retire(s,slot,d.file);
        if(io) return finish(s,slot,card_result(io));
    }
    if(!s->free_blocks) return finish(s,slot,STORAGE_NO_SPACE);
    uint32_t generation=d.maximum+1;
    score_format_set_generation(s->save,generation);
    char name[21];
    snprintf(name,sizeof(name),"BIJACQUARD%02d%08lX",slot,(unsigned long)generation);
    CardResult io=write_file(s,name);
    if(io) return finish(s,slot,card_result(io));
    io=read_file(s,name);
    if(io) return finish(s,slot,card_result(io));
    int number; uint32_t read_generation;
    FormatResult format=score_format_decode(s->readback,SCORE_FILE_BYTES,
        &s->incoming,&number,&read_generation);
    if(memcmp(s->save,s->readback,SCORE_FILE_BYTES) || format!=FORMAT_OK ||
       number!=slot || read_generation!=generation)
        return finish(s,slot,STORAGE_CORRUPT);
    s->free_blocks--;
    // A failed retirement leaves the newly verified generation available for
    // recovery. The next save retries safe cleanup before allocating.
    io=retire(s,slot,-1);
    if(io==CARD_CHANGED || io==CARD_MISSING) return finish(s,slot,card_result(io));
    return finish(s,slot,io?STORAGE_CLEANUP:STORAGE_SAVED);
}
const char *storage_message(StorageResult r) {
    static const char *messages[]={"CHECK","EMPTY","SAVED","BUSY","CORRUPT","NEWER VERSION","NO CARD","CARD TIMEOUT","CARD CHANGED","CARD I/O ERROR","UNFORMATTED","CARD DAMAGED","NO FREE BLOCK","SCORE FULL","SAVED / CLEANUP PENDING","GENERATION FULL"};
    return messages[r];
}
