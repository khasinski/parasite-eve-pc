#include <string.h>
#include <stdio.h>
#include "pe_checkpoint_game.h"
#include "pe_extension_store.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/game_state.h"
#include "pe1/inventory.h"
#include "pe1/memcard.h"
#include "pe1/save.h"
#include "pe1/save_blob.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#define PE_RELOAD_SAME 0x800u
int pe_checkpoint_build_card(void) {
    static SaveRuntimeState header;
    char path[0x40];
    BattleStateTail *action = g_AyaBattleState.action;
    BattleAttributes *attributes = g_AyaBattleState.attributes;
    const int transfer = D_800A5D50;
    int r;

    snprintf(path, sizeof path, "BASLUS-00662PEQUICKSAVE000A");
    memcpy(&header, &g_SaveRuntimeState, sizeof header);
    Save_BuildHeader();
    if (action && action != &g_SavedBattleStateTail) g_AyaBattleState.action = &g_SavedBattleStateTail;
    if (attributes && attributes != &g_BattleEquipStateBlock) g_AyaBattleState.attributes = &g_BattleEquipStateBlock;
    r = Save_BuildCardFile(path);
    g_AyaBattleState.action = action;
    g_AyaBattleState.attributes = attributes;
    memcpy(&g_SaveRuntimeState, &header, sizeof header);
    D_800A5D50 = transfer;
    return r;
}


int pe_checkpoint_native_valid(const unsigned char *card) {
    const unsigned at=0x100+0x12e4+0x8a8;unsigned crc=0xffff,expected;
    memcpy(&expected,card+at,4);
    for(unsigned i=0;i<8192;i++){unsigned v=i>=at&&i<at+4?0:card[i];crc^=v<<8;
        for(unsigned j=0;j<8;j++)crc=crc&0x8000?(crc<<1)^0x1021:crc<<1;}
    if((~crc&0xffff)!=expected)return 0;
    unsigned links[2];memcpy(links,card+0x100+0x12e4+0x818+0x68,8);
    if((links[0]&&links[0]!=0x800b0cb0u)||(links[1]&&links[1]!=0x8009d1b0u))return 0;
    SaveRuntimeState candidate;memcpy(&candidate,card+0x100,sizeof candidate);
    return Inv_ValidateSavedInventory(&candidate);
}
int pe_checkpoint_capture_portable(unsigned char *out,unsigned cap,unsigned *used) {
    if(!pe_free_field()||pe_checkpoint_build_card()<0){pe_mod_save_cancelled();return -1;}
    int result=pe_checkpoint_pack(out,cap,used,g_MemCardFileBuffer);
    pe_mod_save_cancelled();return result;
}
int pe_checkpoint_prepare_portable(const unsigned char *in,unsigned bytes) {
    /* Native structural checks precede callbacks and live changes. */
    if(bytes<32+8192||!pe_checkpoint_native_valid(in+32))return -1;
    unsigned char candidate[8192];
    if(pe_checkpoint_unpack(in,bytes,"BASLUS-00662PEQUICKSAVE000A",candidate))return -1;
    memcpy(g_MemCardFileBuffer,candidate,8192);return 0;
}
int pe_checkpoint_commit_portable(void) {
    int token,*args[1]={&token};
    /* Save_LoadCardFileIntoRuntime's commit, without the card menu's dialogs. */
    g_SaveIoCursor = g_MemCardFileBuffer + 0x100 + 0x12E4;
    if (Save_DeserializeTail() != 0) { pe_toast("Quick load failed: the save's records don't match"); return -1; }
    memcpy(&g_SaveRuntimeState, &g_MemCardSaveStateBuffer, sizeof g_SaveRuntimeState);
    g_SaveIoCursor += 4;
    Save_RestoreHeader();
    PE_HOOK(PE_HOOK_SAVE_READ, g_MemCardFileBuffer);
    /* Into the saved room, the way a field script changes rooms. The frame loop only ends for a
     * new token, so a save of the room Aya is in also asks for the game's same-room reload (the
     * Wardrobe's, script command 2101). */
    token = g_SceneDispatchToken;
    (void)Script_EncodeBarcode(args);
    if (token == g_SceneDispatchCur) g_GameState.flags |= PE_RELOAD_SAME;
    return 0;
}
