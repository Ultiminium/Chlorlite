/*
 * assetpack.c — shareable .ccpak bundles + per-file asset dispatch (see header).
 *
 * .ccpak layout (little-endian, raw store — no compression dependency):
 *   magic   "CCPAK\0"          6 bytes
 *   version u16                = 1
 *   count   u32                number of entries
 *   [count] directory entries:
 *       name_len u16, name[name_len]   (no NUL)
 *       type     u8                    (CCAssetType)
 *       offset   u64                   (from start of file, into data blob)
 *       size     u64
 *   data blob                  concatenated raw file bytes
 * Simple, portable, inspectable. Loading extracts entries to a temp dir and
 * dispatches each to its real type loader (glTF/texture/font/…).
 */
#include "cc/assetpack.h"
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/ccmodel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* type loaders live in the engine already; declare the ones we dispatch to */
extern CCFont    cc_font_load(CCEngine*, const char*, float);
extern CCTexture cc_texture_load(CCEngine*, const char*);

#define CCPAK_MAGIC "CCPAK\0"
#define CCPAK_VER   1

/* ── extension → type ───────────────────────────────────────────────────── */
static int ieq(const char* a, const char* b){
    for(;*a&&*b;a++,b++){ char ca=*a,cb=*b; if(ca>='A'&&ca<='Z')ca+=32; if(cb>='A'&&cb<='Z')cb+=32; if(ca!=cb)return 0; }
    return *a==*b;
}
static const char* ext_of(const char* path){
    const char* dot=NULL; for(const char* p=path;*p;p++) if(*p=='.')dot=p;
    return dot?dot+1:"";
}
static const char* base_of(const char* path){
    const char* b=path; for(const char* p=path;*p;p++) if(*p=='/'||*p=='\\')b=p+1;
    return b;
}
CCAssetType cc_asset_type_from_path(const char* path){
    if(!path) return CC_ASSET_UNKNOWN;
    const char* e=ext_of(path);
    if(ieq(e,"ttf")||ieq(e,"otf"))                    return CC_ASSET_FONT;
    if(ieq(e,"gltf")||ieq(e,"glb")||ieq(e,"obj"))     return CC_ASSET_MODEL;
    if(ieq(e,"png")||ieq(e,"jpg")||ieq(e,"jpeg")||ieq(e,"tga")||ieq(e,"bmp")) return CC_ASSET_TEXTURE;
    if(ieq(e,"ccscene"))                              return CC_ASSET_SCENE;
    if(ieq(e,"ccpak"))                                return CC_ASSET_PACK;
    return CC_ASSET_UNKNOWN;
}

/* ── single-asset dispatch ──────────────────────────────────────────────── */
CCAssetResult cc_asset_load(CCEngine* eng, const char* path){
    CCAssetResult r; memset(&r,0,sizeof(r));
    r.name = base_of(path);
    r.type = cc_asset_type_from_path(path);
    switch(r.type){
        case CC_ASSET_FONT: {
            CCFont f=cc_font_load(eng,path,48.0f); r.as.font=f; r.ok=(f!=0);
        } break;
        case CC_ASSET_TEXTURE: {
            CCTexture t=cc_texture_load(eng,path); r.as.texture=t; r.ok=(t!=0);
        } break;
        case CC_ASSET_MODEL: {
            CCModel* m = ieq(ext_of(path),"obj") ? ccm_import_obj(path,r.name)
                                                 : ccm_import_gltf(path,r.name);
            r.as.model=m; r.ok=(m!=NULL);
        } break;
        case CC_ASSET_PACK: {
            /* a pack loads many assets; report ok if it opened */
            CCPack* p=cc_pack_open(path); r.ok=(p!=NULL); if(p)cc_pack_close(p);
        } break;
        default: r.ok=false; break;
    }
    return r;
}

/* ── read a whole file ──────────────────────────────────────────────────── */
static unsigned char* read_file(const char* path, uint64_t* size){
    FILE* f=fopen(path,"rb"); if(!f) return NULL;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<0){ fclose(f); return NULL; }
    unsigned char* buf=(unsigned char*)malloc((size_t)n);
    if(buf && fread(buf,1,(size_t)n,f)!=(size_t)n){ free(buf); buf=NULL; }
    fclose(f); if(size)*size=(uint64_t)n; return buf;
}
static void w_u16(FILE* f,uint16_t v){ fputc(v&0xff,f); fputc((v>>8)&0xff,f); }
static void w_u32(FILE* f,uint32_t v){ for(int i=0;i<4;i++)fputc((v>>(i*8))&0xff,f); }
static void w_u64(FILE* f,uint64_t v){ for(int i=0;i<8;i++)fputc((v>>(i*8))&0xff,f); }
static uint16_t r_u16(const unsigned char* p){ return (uint16_t)(p[0]|(p[1]<<8)); }
static uint32_t r_u32(const unsigned char* p){ return (uint32_t)(p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24)); }
static uint64_t r_u64(const unsigned char* p){ uint64_t v=0; for(int i=0;i<8;i++)v|=((uint64_t)p[i])<<(i*8); return v; }

/* ── write a .ccpak ─────────────────────────────────────────────────────── */
uint64_t cc_pack_write(const char* out_path, const char* const* files, uint32_t nfiles){
    if(!out_path||!files||nfiles==0) return 0;
    /* load all file bytes first (need sizes for the directory offsets) */
    unsigned char** blobs=(unsigned char**)calloc(nfiles,sizeof(void*));
    uint64_t* sizes=(uint64_t*)calloc(nfiles,sizeof(uint64_t));
    const char** names=(const char**)calloc(nfiles,sizeof(char*));
    uint8_t* types=(uint8_t*)calloc(nfiles,1);
    if(!blobs||!sizes||!names||!types){ free(blobs);free(sizes);free(names);free(types); return 0; }
    for(uint32_t i=0;i<nfiles;i++){
        blobs[i]=read_file(files[i],&sizes[i]);
        names[i]=base_of(files[i]);
        types[i]=(uint8_t)cc_asset_type_from_path(files[i]);
        if(!blobs[i]){ for(uint32_t j=0;j<=i;j++)free(blobs[j]);
            free(blobs);free(sizes);free(names);free(types); return 0; }
    }
    FILE* f=fopen(out_path,"wb");
    if(!f){ for(uint32_t i=0;i<nfiles;i++)free(blobs[i]); free(blobs);free(sizes);free(names);free(types); return 0; }
    fwrite(CCPAK_MAGIC,1,6,f);
    w_u16(f,CCPAK_VER);
    w_u32(f,nfiles);
    /* compute data offsets: after the full directory. First measure dir size. */
    uint64_t dir_bytes=0;
    for(uint32_t i=0;i<nfiles;i++) dir_bytes += 2+strlen(names[i]) + 1 + 8 + 8;
    uint64_t data_start = 6+2+4 + dir_bytes;
    uint64_t off=data_start;
    for(uint32_t i=0;i<nfiles;i++){
        uint16_t nl=(uint16_t)strlen(names[i]);
        w_u16(f,nl); fwrite(names[i],1,nl,f);
        fputc(types[i],f);
        w_u64(f,off); w_u64(f,sizes[i]);
        off+=sizes[i];
    }
    uint64_t written=data_start;
    for(uint32_t i=0;i<nfiles;i++){ fwrite(blobs[i],1,(size_t)sizes[i],f); written+=sizes[i]; free(blobs[i]); }
    fclose(f);
    free(blobs);free(sizes);free(names);free(types);
    return written;
}

/* ── open / read directory ──────────────────────────────────────────────── */
typedef struct { char name[256]; uint8_t type; uint64_t off,size; } PakEntry;
struct CCPack { char path[1024]; uint32_t count; PakEntry* entries; };

CCPack* cc_pack_open(const char* path){
    uint64_t fsz=0; unsigned char* buf=read_file(path,&fsz);
    if(!buf||fsz<12) { free(buf); return NULL; }
    if(memcmp(buf,CCPAK_MAGIC,6)!=0){ free(buf); return NULL; }
    uint16_t ver=r_u16(buf+6); (void)ver;
    uint32_t count=r_u32(buf+8);
    CCPack* p=(CCPack*)calloc(1,sizeof(CCPack));
    if(!p){ free(buf); return NULL; }
    strncpy(p->path,path,sizeof(p->path)-1);
    p->count=count;
    p->entries=(PakEntry*)calloc(count?count:1,sizeof(PakEntry));
    const unsigned char* q=buf+12;
    const unsigned char* end=buf+fsz;
    for(uint32_t i=0;i<count;i++){
        if(q+2>end) break;
        uint16_t nl=r_u16(q); q+=2;
        if(q+nl+1+16>end) break;
        uint16_t cp = nl<255?nl:255;
        memcpy(p->entries[i].name,q,cp); p->entries[i].name[cp]=0; q+=nl;
        p->entries[i].type=*q++;
        p->entries[i].off=r_u64(q); q+=8;
        p->entries[i].size=r_u64(q); q+=8;
    }
    free(buf);
    return p;
}
void cc_pack_close(CCPack* p){ if(p){ free(p->entries); free(p); } }
uint32_t cc_pack_count(const CCPack* p){ return p?p->count:0; }
const char* cc_pack_entry_name(const CCPack* p, uint32_t i){ return (p&&i<p->count)?p->entries[i].name:NULL; }
CCAssetType cc_pack_entry_type(const CCPack* p, uint32_t i){ return (p&&i<p->count)?(CCAssetType)p->entries[i].type:CC_ASSET_UNKNOWN; }

uint64_t cc_pack_extract(const CCPack* p, uint32_t i, const char* dest){
    if(!p||i>=p->count||!dest) return 0;
    FILE* src=fopen(p->path,"rb"); if(!src) return 0;
    if(fseek(src,(long)p->entries[i].off,SEEK_SET)!=0){ fclose(src); return 0; }
    uint64_t n=p->entries[i].size;
    unsigned char* buf=(unsigned char*)malloc(n?n:1);
    if(!buf||fread(buf,1,(size_t)n,src)!=(size_t)n){ free(buf); fclose(src); return 0; }
    fclose(src);
    FILE* out=fopen(dest,"wb"); if(!out){ free(buf); return 0; }
    fwrite(buf,1,(size_t)n,out); fclose(out); free(buf);
    return n;
}

/* ── load every entry into the engine ───────────────────────────────────── */
uint32_t cc_pack_load(CCEngine* eng, const char* path, CCAssetResult* out, uint32_t max){
    CCPack* p=cc_pack_open(path); if(!p) return 0;
    /* extract each to a temp dir, then dispatch by real loader */
    const char* tmpdir="/tmp/ccpak_extract";
    char cmd[1100]; snprintf(cmd,sizeof(cmd),"mkdir -p %s",tmpdir); if(system(cmd)!=0){}
    uint32_t loaded=0;
    for(uint32_t i=0;i<p->count;i++){
        char dest[1200]; snprintf(dest,sizeof(dest),"%s/%s",tmpdir,p->entries[i].name);
        if(cc_pack_extract(p,i,dest)==0) continue;
        CCAssetResult r=cc_asset_load(eng,dest);
        if(out && loaded<max) out[loaded]=r;
        loaded++;
    }
    cc_pack_close(p);
    return loaded;
}
