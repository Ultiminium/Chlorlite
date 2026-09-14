/* dialogue_test — branching conversation with gated choices and actions, driven
 * through a CCSaveState-backed flag/action hook (showing dialogue + save compose).
 * Data-only (no render). Walks two playthroughs of the same dialogue: one where
 * a gated choice is hidden, one where a prior action unlocked it. */
#include "cc/dialogue.h"
#include "cc/save.h"
#include <stdio.h>
#include <string.h>

static int fails=0;
#define CHECK(c,m) do{ if(!(c)){ printf("FAIL: %s\n",m); fails++; } }while(0)

/* hooks: back flags + actions with a CCSaveState */
static bool flag_q(const char* flag, void* ud){
    CCSaveState* s=(CCSaveState*)ud;
    return cc_save_get_bool(s, flag, false);
}
static void action_do(const char* action, void* ud){
    CCSaveState* s=(CCSaveState*)ud;
    /* interpret "set <flag>" */
    if(!strncmp(action,"set ",4)) cc_save_set_bool(s, action+4, true);
}

static const char* DLG =
    "ccdlg 1\n"
    "node root\n"
    "  speaker Keeper\n"
    "  text You reached the gate. It's sealed.\n"
    "  choice Ask about the seal -> lore\n"
    "  choice [has_sigil] Press the sigil -> open\n"   /* gated */
    "node lore\n"
    "  speaker Keeper\n"
    "  text Only one bearing the sigil may pass.\n"
    "  action set has_sigil\n"                          /* grants the flag */
    "  goto root\n"
    "node open\n"
    "  speaker Keeper\n"
    "  text The gate grinds open. Go.\n"
    "  action set gate_open\n"
    "  end\n";

int main(void){
    CCDialogue* d=cc_dialogue_parse(DLG);
    CHECK(d!=NULL,"parse");
    if(!d){ printf("DIALOGUE TEST: FAILED parse\n"); return 1; }
    CHECK(cc_dialogue_node_count(d)==3,"node count");

    /* ── playthrough: sigil not yet held → gated choice hidden ── */
    CCSaveState* save=cc_save_new();
    CCDialogueHooks hooks={ .flag_query=flag_q, .action_apply=action_do, .userdata=save };

    CCDialogueRunner* r=cc_dialogue_start(d,"root");
    cc_dialogue_set_hooks(r,&hooks);
    CHECK(!strcmp(cc_dialogue_speaker(r),"Keeper"),"speaker");
    CHECK(strstr(cc_dialogue_text(r),"sealed")!=NULL,"root text");
    /* only 1 visible choice (sigil gate hidden) */
    CHECK(cc_dialogue_choice_count(r)==1,"gated choice hidden initially");
    CHECK(!strcmp(cc_dialogue_choice_text(r,0),"Ask about the seal"),"visible choice text");

    /* choose to learn the lore → fires 'set has_sigil' → goto root */
    CHECK(cc_dialogue_choose(r,0),"choose lore");
    /* lore node auto-advances via goto back to root; advance it */
    CHECK(strstr(cc_dialogue_text(r),"sigil may pass")!=NULL,"lore text");
    CHECK(cc_save_get_bool(save,"has_sigil",false),"lore action set flag");
    cc_dialogue_advance(r);   /* goto root */
    CHECK(strstr(cc_dialogue_text(r),"sealed")!=NULL,"back at root");
    /* NOW the gated choice is visible (flag set) */
    CHECK(cc_dialogue_choice_count(r)==2,"gated choice now visible");
    CHECK(!strcmp(cc_dialogue_choice_text(r,1),"Press the sigil"),"gated choice text");

    /* press the sigil → open node fires 'set gate_open' then ends */
    CHECK(cc_dialogue_choose(r,1),"choose sigil");
    CHECK(strstr(cc_dialogue_text(r),"grinds open")!=NULL,"open text");
    CHECK(cc_save_get_bool(save,"gate_open",false),"open action set flag");
    /* advancing the end node finishes the conversation */
    CHECK(!cc_dialogue_advance(r),"advance past end returns false");
    CHECK(cc_dialogue_finished(r),"finished after end");

    cc_dialogue_free_runner(r);
    cc_save_free(save);

    /* ── second playthrough: pre-set the flag → sigil available immediately ── */
    CCSaveState* save2=cc_save_new();
    cc_save_set_bool(save2,"has_sigil",true);
    CCDialogueHooks h2={ .flag_query=flag_q, .action_apply=action_do, .userdata=save2 };
    CCDialogueRunner* r2=cc_dialogue_start(d,"root");
    cc_dialogue_set_hooks(r2,&h2);
    CHECK(cc_dialogue_choice_count(r2)==2,"both choices visible when flag preset");
    cc_dialogue_free_runner(r2);
    cc_save_free(save2);

    cc_dialogue_free(d);
    if(fails){ printf("DIALOGUE TEST: %d FAILURE(S)\n",fails); return 2; }
    printf("DIALOGUE TEST: all checks passed (branching + gating + actions via save hooks)\n");
    return 0;
}
