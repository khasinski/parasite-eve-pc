#ifndef PE_HOOKS_H
#define PE_HOOKS_H

/* Mod hook points. Each PE_HOOK line in the game hands one moment to the mod
 * host (mods/host), with a small context struct of pointers to the values a
 * mod may read or change. Without PE_MODS the macro is 0 and the game is the
 * unchanged original. A non-zero result means a mod handled the moment and
 * the caller skips its own code; only hooks documented as replaceable check it. */

typedef enum PeHookId {
    PE_HOOK_INIT,        /* PeHookInit: once, before the first scene */
    PE_HOOK_SCENE_START, /* NULL: a field scene finished loading, before its first frame */
    PE_HOOK_FRAME,       /* NULL: every field frame, after pad input is read */
    PE_HOOK_BATTLE_UPDATE, /* NULL: Battle_Update entry, every battle frame */
    PE_HOOK_ENEMY_HIT,   /* BattleEntity *: Battle_StepEnemyMovement entry, before an enemy's damage */
    PE_HOOK_SPELL,       /* PeHookSpell: Battle_ApplySpellEffect entry (battle and menu) */
    PE_HOOK_HELP_TEXT,   /* u8 **: the menu title-bar line, before it is printed; may be replaced */
    PE_HOOK_BATTLE_REWARDS, /* PeHookRewards: Aya_SetTotalExp entry, once per won battle */
    PE_HOOK_INV_COMMAND, /* PeHookInvCommand: Inv_DispatchCommand entry; replaceable */
    PE_HOOK_ITEM_ADD,    /* s32 *: Inv_AddItem entry; change id, or return nonzero for handled/success */
    PE_HOOK_STEAL,       /* NULL: a steal's Inv_AddItem follows (Battle_ResolveHitOnTimer) */
    PE_HOOK_SAVE_WRITE,  /* u8 *: the 8 KB card block, before its CRC (free tail from 0x1C90) */
    PE_HOOK_SAVE_READ,   /* u8 *: the 8 KB card block of a save being loaded */
    PE_HOOK_ITEM_COUNT,  /* int **: Script_CountItemsByValue ({&item, &result}); nonzero uses supplied result */
    PE_HOOK_SLOT_ROW,    /* u8 **: an item row's name (Menu_DrawSlotRow), before it is printed */
    PE_HOOK_TOOL_TRANSFER, /* PeHookToolTransfer: Inv_TransferItemBetweenLists entry (Tools) */
    PE_HOOK_PAD_READ,    /* NULL: Field_HandleStateTransition, before it reads g_PadPackets */
    PE_HOOK_LIST_ROWS,   /* PeHookListRows: Draw_SetPrimCallback entry; the row count may change */
    PE_HOOK_MAIN_MENU_ACTION, /* PeHookMenuAction: Aya's main menu confirm; replaceable */
    PE_HOOK_SCRIPT_RANDOM, /* PeHookScriptRandom: local RNG bounds, before a script roll */
    PE_HOOK_PLAYER_MOVE, /* PeHookPlayerMove: local multiplier for a player-input movement step */
    PE_HOOK_PAD_MAP,     /* PeHookPadMap: local run-button mapping for this poll */
    PE_HOOK_ACTOR_STEP,  /* PeHookActorStep: local scripted speed or animation increment */
    PE_HOOK_ITEM_USE,    /* int *: Battle_ApplyDamage item id before its effects */
    PE_HOOK_TEXT_LOOKUP, /* PeHookTextLookup: a resident text table lookup's result (system messages,
                          * item names and descriptions, the rest); may be replaced */
    PE_HOOK_INVENTORY_SORTED, /* NULL: Aya's native bag sort completed */
    PE_HOOK_INVENTORY_HEADER, /* MenuWidgetNode *: after Aya's Quantity header is drawn */
    PE_HOOK_EQUIP_EFFECT, /* PeHookEquipEffect: effect icon draw; nonzero replaces sprite */
    PE_HOOK_TEXTBOXES,   /* NULL: Menu_DrawTextboxEntries entry, before the dialogue boxes draw */
    PE_HOOK_ITEM_ACTIONS, /* PeHookItemActions: an item's action list (Use / Move / Discard ...) just opened */
    PE_HOOK_MAIN_MENU_ICON, /* PeHookMainMenuIcon: a main menu row's icon, before it is drawn */
    PE_HOOK_ENEMY_HIT_END, /* BattleEntity *: original damage applied; restore temporary hit inputs */
    PE_HOOK_PLAYER_BANK, /* NULL: Scene_LoadEntityTexture entry, Aya's model / texture load step;
                          * g_SceneAreaType (her outfit bank) may be changed while entity_texture_phase is 0 */
    PE_HOOK_INVENTORY_ROW, /* PeHookInventoryRow: inventory row dimming, presentation only */
    PE_HOOK_POWER_AVAILABLE, /* PeHookPowerAvailable: Menu_CheckItemAffordable, can this PE power be picked now */
    PE_HOOK_POWER_COST, /* PeHookPowerCost: raw PE cost before the native armor discount */
    PE_HOOK_PLAYER_ROWS, /* NULL: before Aya's actor takes bank_slots[0] and her rows (g_GameState.bank_rows):
                          * the end of Scene_LoadEntityTexture (every room load: the room's actor
                          * spawn reads them) and Scene_InitEntityPlayer step 0x27 (scene start) */
    PE_HOOK_PLAYER_TEXTURES, /* SceneAssetBlob *: Aya's texture blob just read, before its TIMs go to VRAM;
                              * replaceable (nonzero: the game skips its upload) */
    PE_HOOK_FRAME_DRAWN, /* NULL: Draw_RenderFrame, the previous frame finished drawing (DrawSync)
                          * and not yet shown; its buffer is g_RenderDrawEnvArray[1 - g_ActiveDrawSlot] */
    PE_HOOK_NEW_GAME,    /* PeHookNewGame: a new game starts; the host has reset every save record */
    PE_HOOK_TEXT_PAGE,   /* PeHookTextPage: a dialogue box found its page in the room's message bank;
                          * the text it draws may be replaced */
    PE_HOOK_TIM_UPLOAD,  /* TimFile *: Asset_LoadTimImage, before the image goes to VRAM (fonts,
                          * backgrounds, area glyph pages); its pixels may be changed in place */
    PE_HOOK_TEXT_GLYPH, /* PeHookTextGlyph: optional multi-byte glyph; nonzero handles it */
    PE_HOOK_COUNT
} PeHookId;

/* The factor belongs to the current calculation, not the actor. A handler
 * may change it for this input step only and must not retain its address. */
typedef struct PeHookPlayerMove {
    const void *actor;
    int *factor;
    int running;
    int combat;
} PeHookPlayerMove;

typedef struct PeHookPadMap {
    unsigned buttons;
    int *run_mask;
} PeHookPadMap;

/* Stack-local values only; never retain these pointers or alter actor state. */
typedef struct PeHookActorStep {
    const void *actor;
    int *speed;    /* scripted movement speed, NULL for animation */
    int *anim;     /* animation increment, NULL for motion */
} PeHookActorStep;

/* next points just past the current instruction in native loaded bytecode. */
typedef struct PeHookScriptRandom {
    int *start, *end;
    const unsigned int *next;
} PeHookScriptRandom;

/* Caller owns packet allocation, ordering, color and palette. Measurement never uploads.
 * A provider returns nonzero only for a complete token it owns, setting bytes/advance/UV. */
typedef struct PeHookTextGlyph {
    const unsigned char *text;
    unsigned bytes, advance;
    int draw;
    unsigned char u, v;
} PeHookTextGlyph;

typedef struct PeHookTextLookup {
    unsigned table, index; /* table 4 = system messages, 8 = item names, 12 = item descriptions,
                            * 16 = the rest (Text_LookupTable4 / 8 / C / 10) */
    void **text;
    int persistent; /* nonzero: editable name copied into a save; use stable byte glyphs only */
} PeHookTextLookup;

/* The box's text starts after the page's FE <id> and runs to its FF or F9. A replacement must
 * end the same way and outlive the box; the bank itself is left as loaded. */
typedef struct PeHookTextPage {
    const unsigned char *bank; /* the message bank the box reads (g_TextboxMessageSource) */
    unsigned page;             /* the page the script opened */
    unsigned char **message;   /* the text the box will draw */
} PeHookTextPage;

typedef struct PeHookRewards {
    int exp;
    struct BattleRewardSlot *list; /* 10 entries, id 0 ends */
} PeHookRewards;

typedef struct PeHookInvCommand {
    int command, value;
    int result; /* a handler returning nonzero replaces the command with this result */
} PeHookInvCommand;

typedef struct PeHookToolTransfer {
    unsigned selection; /* the donor's saved selection; the target is the other one */
    int modifier;       /* >= 0: that mod moves; < 0: the + bonuses move */
    int flags;          /* bit 0: Super Tool (the donor survives) */
    int cost;
} PeHookToolTransfer;

typedef struct PeHookSpell {
    unsigned spell;           /* Parasite Energy power index */
    struct BattleEntity *target;
} PeHookSpell;

/* A new game, of any kind: the title's New Game or Tutorial (the title's choice, in the boot loop),
 * or the EX Game (Inv_DispatchCommand INV_CMD_START_NEW_GAME, which the host sends on as this). */
typedef enum PeNewGameKind {
    PE_NEW_GAME_TITLE = 1,     /* the title's New Game (MenuCard_RunListScreen result 1) */
    PE_NEW_GAME_EX = 2,        /* the EX Game, from a Clear Data save */
    PE_NEW_GAME_TUTORIAL = 3,  /* MenuCard_RunListScreen result 3 */
} PeNewGameKind;

typedef struct PeHookNewGame {
    int kind; /* PeNewGameKind */
} PeHookNewGame;

typedef struct PeHookInit {
    int interactive;        /* 0 for headless runs: no window, no menus */
    const char *prefs_file; /* per-user preferences file; mods keep their files beside it */
} PeHookInit;

typedef struct PeHookInventoryRow {
    struct MenuWidgetNode *node;
    int slot;
    int *dimmed;
} PeHookInventoryRow;

typedef struct PeHookPowerCost {
    int power;
    int *cost; /* integer PE points before the native armor discount */
} PeHookPowerCost;

typedef struct PeHookPowerAvailable {
    int power;    /* Parasite Energy power index (0 Heal 1 .. 19 Liberate) */
    int *enabled; /* the game's answer (PE, HP, ammo); a mod may clear it */
} PeHookPowerAvailable;

typedef struct PeHookListRows {
    struct MenuWidgetNode *node;
    int *count;
} PeHookListRows;

typedef struct PeHookEquipEffect {
    int effect; /* lower five bits of the equipment modifier */
    int armor;  /* 0 = weapon, 1 = armor */
} PeHookEquipEffect;

typedef struct PeHookMenuAction {
    int action;                   /* 0-8 the game's rows; 9 = a row past the last one */
    struct MenuWidgetNode *list;  /* the main list node */
} PeHookMenuAction;

/* The panel's update and the list's draw and rows may be replaced; g_MenuItemActionStateTable
 * [context] gives the game's action of each row. */
typedef struct PeHookItemActions {
    struct MenuWidgetNode *panel; /* update = Menu_StepEquipScreen */
    struct MenuWidgetNode *list;  /* draw = Menu_DrawContextActionList, 3 rows */
    int context;                  /* g_MenuItemActionContext: 0 consumables, 1 and 2 others */
    int list_id, slot;            /* g_MenuActiveItemList (0 = Aya's bag), g_MenuActiveItemSlot */
} PeHookItemActions;

typedef struct PeHookMainMenuIcon {
    int row;     /* the list row */
    int action;  /* 0-8 the game's rows; 9 = a row past the last one */
    int sprite;  /* action + 0x56; may be changed; -1: the handler drew the icon itself */
} PeHookMainMenuIcon;

#ifdef PE_MODS
#include <stddef.h>
int pe_host_init(const PeHookInit *init);
int pe_hook_dispatch(int id, void *ctx);
/* Crash report Mods section: the mods and their state, and the mod code that was running.
 * Runs inside the crash handler, so it only reads values. */
void pe_mod_crash_describe(char *out, size_t size, int on_game_thread);
/* Crash report room snapshot: runs the save write hook into block (a zeroed
 * 8 KB card block) as a real save would, when a room starts. -1 while mod code
 * is running; the block stays zeroed. */
int pe_mod_snapshot_save_tail(unsigned char *block);
#define PE_HOOK(id, ctx) \
    ((id) == PE_HOOK_INIT ? pe_host_init((const PeHookInit *)(const void *)(ctx)) : pe_hook_dispatch((id), (ctx)))
#else
#define PE_HOOK(id, ctx) ((void)0, 0)
#endif

#endif
