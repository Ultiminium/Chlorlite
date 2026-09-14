/* save_test — game save/load round-trip. Writes a realistic save (player stats,
 * position, current level, quest flags, inventory) covering every value type,
 * serializes to a .ccsave text file, reads it back, and asserts every value
 * survives. Also checks defaults for missing keys and key enumeration. */
#include "cc/save.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)
static int feq(float a,float b){ return fabsf(a-b)<1e-4f; }

int main(void){
    /* build a realistic save */
    CCSaveState* s=cc_save_new();
    cc_save_set_int(s,"player.hp",87);
    cc_save_set_int(s,"player.gold",1240);
    cc_save_set_float(s,"player.stamina",0.63f);
    cc_save_set_bool(s,"quest.intro.done",true);
    cc_save_set_bool(s,"quest.crypt.started",false);
    cc_save_set_str(s,"level","crypt_02.cclist");
    cc_save_set_str(s,"player.name","Ada Wren");        /* string with a space */
    cc_save_set_vec3(s,"player.pos",12.5f,3.0f,-48.25f);
    cc_save_set_int(s,"inventory.keys",3);

    uint32_t n_before=cc_save_count(s);
    CHECK(n_before==9,"count after inserts");

    /* overwrite semantics */
    cc_save_set_int(s,"player.hp",92);
    CHECK(cc_save_count(s)==9,"overwrite must not grow count");
    CHECK(cc_save_get_int(s,"player.hp",0)==92,"overwrite value");

    /* write to disk */
    CHECK(cc_save_write(s,"/tmp/slot1.ccsave"),"write");

    /* read back into a fresh state */
    CCSaveState* r=cc_save_read("/tmp/slot1.ccsave");
    CHECK(r!=NULL,"read");
    if(!r){ printf("SAVE TEST: FAILED (no read)\n"); return 1; }

    CHECK(cc_save_get_int(r,"player.hp",0)==92,"hp roundtrip");
    CHECK(cc_save_get_int(r,"player.gold",0)==1240,"gold roundtrip");
    CHECK(feq(cc_save_get_float(r,"player.stamina",0),0.63f),"stamina roundtrip");
    CHECK(cc_save_get_bool(r,"quest.intro.done",false)==true,"bool true roundtrip");
    CHECK(cc_save_get_bool(r,"quest.crypt.started",true)==false,"bool false roundtrip");
    CHECK(!strcmp(cc_save_get_str(r,"level","?"),"crypt_02.cclist"),"level string");
    CHECK(!strcmp(cc_save_get_str(r,"player.name","?"),"Ada Wren"),"name string w/ space");
    CHECK(cc_save_get_int(r,"inventory.keys",0)==3,"keys roundtrip");
    float x,y,z; cc_save_get_vec3(r,"player.pos",&x,&y,&z);
    CHECK(feq(x,12.5f)&&feq(y,3.0f)&&feq(z,-48.25f),"vec3 roundtrip");
    CHECK(cc_save_count(r)==9,"count after read");

    /* defaults for missing keys */
    CHECK(cc_save_get_int(r,"does.not.exist",777)==777,"missing int default");
    CHECK(!strcmp(cc_save_get_str(r,"nope","fallback"),"fallback"),"missing str default");
    CHECK(!cc_save_has(r,"nope"),"has() false for missing");
    CHECK(cc_save_has(r,"player.hp"),"has() true for present");

    /* remove */
    cc_save_remove(r,"inventory.keys");
    CHECK(!cc_save_has(r,"inventory.keys"),"remove");
    CHECK(cc_save_count(r)==8,"count after remove");

    /* enumerate keys */
    int seen_level=0;
    for(uint32_t i=0;i<cc_save_count(r);i++){
        const char* k=cc_save_key_at(r,i);
        if(k && !strcmp(k,"level")) seen_level=1;
    }
    CHECK(seen_level,"key enumeration finds 'level'");

    cc_save_free(s); cc_save_free(r);
    if(fails){ printf("SAVE TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("SAVE TEST: all checks passed (9 typed values round-tripped through .ccsave)\n");
    return 0;
}
