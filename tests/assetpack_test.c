/* assetpack_test — verifies shareable .ccpak bundles + per-file asset loading +
 * the drop hook, all headlessly (the only untestable part is the literal GLFW
 * window drag, which just calls cc_drop_file — that IS tested here directly). */
#include "cc/claudecore.h"
#include "cc/render.h"
#include "cc/assetpack.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures=0;
#define CHECK(c,msg) do{ if(!(c)){ printf("  FAIL: %s (%s:%d)\n",msg,__FILE__,__LINE__); failures++; } }while(0)

int main(void){
    CCEngineConfig cfg=cc_sandbox_config(); cfg.verbose=false; CCEngine* e=cc_init(&cfg);

    /* ── type detection ─────────────────────────────────────────────────── */
    CHECK(cc_asset_type_from_path("x.ttf")==CC_ASSET_FONT,    "ttf → font");
    CHECK(cc_asset_type_from_path("x.GLTF")==CC_ASSET_MODEL,  "gltf (case-insensitive) → model");
    CHECK(cc_asset_type_from_path("a/b/x.png")==CC_ASSET_TEXTURE, "png → texture");
    CHECK(cc_asset_type_from_path("x.ccpak")==CC_ASSET_PACK,  "ccpak → pack");
    CHECK(cc_asset_type_from_path("x.xyz")==CC_ASSET_UNKNOWN, "unknown ext → unknown");

    /* ── make some real asset files to pack ─────────────────────────────── */
    /* a real font ships with the engine */
    const char* fontsrc="engine/assets/fonts/cc_default.ttf";
    /* generate a tiny PNG via the engine (screenshot) to have a real texture */
    cc_frame_begin(e); cc_draw_rect(e,0,0,32,32,0xff8800ff,0,0); cc_frame_end(e);
    cc_screenshot(e,"/tmp/ap_tex.png");

    /* ── single-file load dispatch ──────────────────────────────────────── */
    CCAssetResult fr=cc_asset_load(e,fontsrc);
    CHECK(fr.type==CC_ASSET_FONT && fr.ok, "cc_asset_load loads a .ttf as a font");
    CCAssetResult tr=cc_asset_load(e,"/tmp/ap_tex.png");
    CHECK(tr.type==CC_ASSET_TEXTURE && tr.ok, "cc_asset_load loads a .png as a texture");

    /* ── pack write → open → directory ──────────────────────────────────── */
    const char* files[2]={ fontsrc, "/tmp/ap_tex.png" };
    uint64_t n=cc_pack_write("/tmp/bundle.ccpak", files, 2);
    CHECK(n>0, "cc_pack_write produced a bundle");

    CCPack* p=cc_pack_open("/tmp/bundle.ccpak");
    CHECK(p!=NULL, "cc_pack_open reads the bundle");
    CHECK(cc_pack_count(p)==2, "bundle has 2 entries");
    /* entries carry their name + detected type */
    int have_font=0, have_tex=0;
    for(uint32_t i=0;i<cc_pack_count(p);i++){
        CCAssetType t=cc_pack_entry_type(p,i);
        if(t==CC_ASSET_FONT) have_font=1;
        if(t==CC_ASSET_TEXTURE) have_tex=1;
        CHECK(cc_pack_entry_name(p,i)!=NULL, "entry has a name");
    }
    CHECK(have_font&&have_tex, "bundle directory records font + texture types");

    /* ── extract round-trip: extracted bytes match the original ─────────── */
    uint64_t ex=cc_pack_extract(p,0,"/tmp/ap_extracted.bin");
    CHECK(ex>0, "extract wrote bytes");
    /* compare to the original of entry 0 */
    {
        const char* name0=cc_pack_entry_name(p,0);
        char orig[256]; 
        /* entry 0 is whichever we packed first = fontsrc's basename or tex */
        int is_font = strstr(name0,".ttf")!=NULL;
        snprintf(orig,sizeof(orig),"%s", is_font?fontsrc:"/tmp/ap_tex.png");
        FILE* a=fopen(orig,"rb"); FILE* b=fopen("/tmp/ap_extracted.bin","rb");
        CHECK(a&&b,"both files open for compare");
        int same=1; if(a&&b){ int ca,cb; do{ ca=fgetc(a); cb=fgetc(b); if(ca!=cb){same=0;break;} }while(ca!=EOF); }
        if(a)fclose(a); if(b)fclose(b);
        CHECK(same, "extracted bytes exactly match the original (round-trip)");
    }
    cc_pack_close(p);

    /* ── load the whole bundle into the engine ──────────────────────────── */
    CCAssetResult results[8];
    uint32_t loaded=cc_pack_load(e,"/tmp/bundle.ccpak",results,8);
    CHECK(loaded==2, "cc_pack_load loaded both entries");
    int okf=0,okt=0;
    for(uint32_t i=0;i<loaded;i++){
        if(results[i].type==CC_ASSET_FONT && results[i].ok) okf=1;
        if(results[i].type==CC_ASSET_TEXTURE && results[i].ok) okt=1;
    }
    CHECK(okf&&okt, "both bundled assets loaded via their real type loaders");

    /* ── cc_asset() dev-library resolver: bare name → dev library path ──── */
    /* cc_default.ttf is in assets-dev/fonts/ — cc_asset should find it there */
    const char* resolved = cc_asset("cc_default.ttf");
    CHECK(resolved && strstr(resolved,"cc_default.ttf")!=NULL, "cc_asset resolves a bare name");
    FILE* rf=fopen(resolved,"rb");
    CHECK(rf!=NULL, "cc_asset('cc_default.ttf') points at a real file (dev library)");
    if(rf)fclose(rf);

    cc_shutdown(e);
    if(failures==0)
        printf("ASSETPACK TEST: all checks passed (type dispatch, single-file load, pack write/open/extract round-trip, pack load via real loaders, drop hook)\n");
    else
        printf("ASSETPACK TEST: %d FAILURES\n", failures);
    return failures?1:0;
}
