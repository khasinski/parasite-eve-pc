/* Separately link the transport and command modules against a stand-in game: map numbers
 * to room ids, warp's refusals, script variables, actors, find-script, place / place-near, and
 * save-block with and without a companion file. Commands go in and answers come out through the
 * real files (<folder>/cmd, <folder>/out), in a folder under the test's working directory. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define TEST_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TEST_MKDIR(p) mkdir(p, 0700)
#endif
#include "../mods/host/debug/pe_debug_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pe_game.h"
#include "pe_hooks.h"
#include "pe_mod_host.h"
#include "pe_save.h"
#include "pe1/asset_table.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_start.h"
#include "pe1/random.h"
#include "pe1/battle_context.h"
#include "pe1/battle_modifiers.h"
#include "pe1/battle_saved_state.h"
#include "pe1/weapon_list_output.h"
#include "pe_mod_menu.h"
#include "pe1/field_actor.h"
#include "pe1/game_state.h"
#include "pe1/game_timers.h"
#include "pe1/inventory_state.h"
#include "pe1/inventory_queries.h"
#include "pe1/memcard.h"
#include "pe1/player_entity.h"
#include "pe1/scene_dispatch.h"
#include "pe1/script_queue.h"
#include "psyz/system.h"
#include "pe_mod_state.h"
#include "pe1/menu_dialog.h"
#include "pe1/menu_state.h"
#include "pe1/save.h"
#include "pe1/textbox.h"
#include "pe1/textbox_open.h"


#define PE_SCRIPT_SCAN 0x8000
#define TEST_SAVE_EVENT PE_HOOK_COUNT        /* pe_event_id("save.committed") */
static PeHookFn s_hooks[PE_HOOK_COUNT + 1];
static void *s_hook_users[PE_HOOK_COUNT + 1];
u32 g_BootHostVBlankCount;
s8 g_MenuEquipSlotIndex;
int pe_event_id(const char *name) { return !strcmp(name, PE_EVENT_SAVE_COMMITTED) ? TEST_SAVE_EVENT : -1; }
static void frame(void) { s_hooks[PE_HOOK_FRAME](NULL, s_hook_users[PE_HOOK_FRAME]); }
static void fire(int hook) { s_hooks[hook](NULL, s_hook_users[hook]); }


#define DIR "pe-debug-control-test"

/* ---- the game around it ---- */
u8 g_Base32CharTable[32] = "0123456789abcdefghiklmnoprstuvwy";   /* src/main/cdrom/scene_assets.c */
int g_SceneDispatchToken, g_SceneDispatchCur;
u32 g_ScriptVariableBank[512];
Pe1GameState g_GameState;
SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
FieldActor *g_FieldActorListHead, *g_PlayerEntity;
int g_BattleFrameCounter, g_BattleModeState;
u32 g_FrameRngCounter;
s8 g_BattleTargetIndex;
u8 g_BattleActionQueueCount;
BattleEnemySlot g_BattleEnemySlots[7];
Combatant *g_ActiveActor;
s32 g_ItemDropRngIndex;
u8 g_ItemDropRngTable[521];
TextboxEntry g_TextboxEntries[4];
u8 g_DialogueChoiceCursor;
unsigned char g_MenuActiveMode;
int g_InvSelectedItemIndex, g_MenuActionSubmenuOpen, g_MemCardDialogState, g_SaveSelectedSlot;
unsigned int g_MenuActionSubmenuSel;
static const char *s_current_mod;
static int s_mod_calls;
const char *pe_mod_current(void) { return s_current_mod; }
void pe_mod_call_begin(const char *mod, const char *what, const char *detail) { (void)mod; (void)what; (void)detail; ++s_mod_calls; }
void pe_mod_call_end(void) {}
static void wallet_state(PeStateWriter *w, void *user) {
    pe_state_int(w, "junk", *(int *)user);
    pe_state_text(w, "name", "a \"quoted\" one");
    pe_state_begin_list(w, "list");
    pe_state_int(w, NULL, 1);
    pe_state_begin_object(w, NULL);
    pe_state_bool(w, "ok", 1);
    /* left open: the reader closes the object and the list */
}
static ItemDataRecord s_weapon;
static PeMenuRow s_row;
static int s_given;
static u32 s_levels[99];
ItemDataRecord *pe_base_item(unsigned id) { return id && id <= 255 ? &s_weapon : NULL; }
int Inv_AddItem(int id) { s_given = id; return 0; }
void Inv_SelectActiveList(int mode) { (void)mode; }
void *Aya_GetLevelExpTable(void) { return s_levels; }
void BattleCmd_LoadWeaponModifiers(void) {}
void Inv_RecalcSlotStats(void) {}
void Inv_BuildWeaponList(int mode, WeaponListOutput *out) { (void)mode; (void)out; }
void Inv_BuildArmorList(BattleAttributes *out) { (void)out; }
BattleStateTail g_SavedBattleStateTail;
BattleAttributes g_BattleEquipStateBlock;
static unsigned s_rng;
void Psyz_Srand(unsigned seed) { s_rng = seed; }
unsigned Psyz_RandState(void) { return s_rng; }
ItemDataRecord *pe_equipped(int armor) { return armor ? NULL : &s_weapon; }
int pe_mod_is_running(const char *name) { return !strcmp(name, "test"); }
int pe_menu_row_count(void) { return 1; }
PeMenuRow *pe_menu_row(int i) { (void)i; return &s_row; }
int pe_menu_row_active(const PeMenuRow *r) { (void)r; return 1; }
void pe_menu_row_value_text(PeMenuRow *r, char *out, unsigned size) { (void)r; snprintf(out, size, "wallet 12"); }
void pe_menu_row_accept(PeMenuRow *r) { if (r->kind == PE_ROW_ACTION) r->action(); }
static int s_pressed;
static void test_press(void) { ++s_pressed; }
static unsigned test_three(void) { return 3; }
u8 g_MemCardFileBuffer[0x2000];
const char *g_MemCardFilenameFormatPtr = "bu%d0:BASLUS-00662000000%c%c";

static int s_free = 1, s_warped, s_cancelled, s_extlink;
static unsigned s_warp_arg;
int pe_free_field(void) { return s_free; }
int pe_room_number(void) { return 48; }
int Text_EncodeBase32(char *out, unsigned int v) {
    int i;
    for (i = 0; i < 6; ++i) out[i] = (char)g_Base32CharTable[(v >> ((5 - i) * 5 + 2)) & 0x1F];
    return 0;
}
static int s_day_calls;
int Scene_SetStoryDay(int day) { g_GameState.current_story_day = (s8)day; ++s_day_calls; return 0; }
int Script_EncodeBarcode(int **args) { s_warp_arg = (unsigned)*args[0]; ++s_warped; return 1; }
int Inv_GetAyaSlotLimit(void) { return 30; }
void Save_BuildHeader(void) {}
int Save_BuildCardFile(char *path) { (void)path; memset(g_MemCardFileBuffer, 0x5A, sizeof g_MemCardFileBuffer); return 0; }
void pe_save_crash_tail(uint8_t *block) { (void)block; }
int pe_save_peek(const uint8_t *block, const char *name, unsigned *version, void *out, unsigned capacity, unsigned *size) {
    unsigned char link[20] = { 1, 1, 0, 0, 4, 0, 0, 0 };   /* generation 4 */
    (void)block;
    if (!s_extlink || strcmp(name, "pe_host.extlink") || capacity < 20) return 0;
    memcpy(out, link, 20);
    *version = 1;
    *size = 20;
    return 1;
}
void Psyz_AdjustPath(char *dst, const char *src, int maxlen) {   /* "bu00:NAME" -> DIR/cards/NAME */
    const char *colon = strchr(src, ':');
    snprintf(dst, (size_t)maxlen, DIR "/cards/%s", colon ? colon + 1 : src);
}
void pe_mod_save_cancelled(void) { ++s_cancelled; }
int pe_hook_add(int id, PeHookFn fn, void *user, int priority, const char *mod) {
    s_hooks[id] = fn; s_hook_users[id] = user; (void)priority; (void)mod;
    return 0;
}
static int s_enqueue_during_dispatch;
void pe_log(const char *tag, const char *fmt, ...) {
    (void)fmt;
    if (s_enqueue_during_dispatch && !strcmp(tag, "debug_control")) {
        FILE *f = fopen(DIR "/cmd", "w");
        assert(f); fputs("ping\n", f); fclose(f);
        s_enqueue_during_dispatch = 0;
    }
}
void pe_mod_log(const char *mod, const char *fmt, ...) { (void)mod; (void)fmt; }

/* ---- a command in, its answer out ---- */
static char s_answer[4096];
static const char *ask(const char *cmd) {
    FILE *f = fopen(DIR "/cmd", "w");
    assert(f);
    fprintf(f, "%s\n", cmd);
    fclose(f);
    remove(DIR "/out");
    pe_debug_control_poll();
    f = fopen(DIR "/out", "r");
    assert(f && fgets(s_answer, sizeof s_answer, f));
    fclose(f);
    s_answer[strcspn(s_answer, "\n")] = 0;
    assert(!fopen(DIR "/cmd", "r"));                            /* read and removed */
    return s_answer;
}
static int starts(const char *s, const char *prefix) { return !strncmp(s, prefix, strlen(prefix)); }

static FieldActor s_actor[3];
static unsigned char s_script[3][PE_SCRIPT_SCAN];
static size_t s_script_bytes[3] = {PE_SCRIPT_SCAN, PE_SCRIPT_SCAN, PE_SCRIPT_SCAN};
static unsigned s_floor_refreshes;
size_t Script_TaskBytesAvailable(const void *address) {
    for (unsigned i = 0; i < 3; ++i) if (address == s_script[i]) return s_script_bytes[i];
    return 0;
}
static int s_no_floor;          /* the next spot is off the walkable mesh: no triangle found */
void Entity_FindFloor(struct BattleEntity *actor) {
    FieldActor *field = (FieldActor *)actor;
    assert(!field->collision_face && !field->collision_face_mirror);
    ++s_floor_refreshes;
    if (s_no_floor) return;      /* as the game's: the face stays as it was (null here) */
    field->collision_face = field->collision_face_mirror = (void *)s_script;
}

int main(void) {
    static const u32 junk_count[3] = { 0x001040A8u, 0, 0x14 };
    FILE *f;
    TEST_MKDIR(DIR);
    TEST_MKDIR(DIR "/cards");
    TEST_MKDIR(DIR "/cards/.pe-ext");
    remove(DIR "/cards/.pe-ext/BASLUS-006620000000A.00000004.pex");   /* a run before this one's */
    remove(DIR "/companion.pex");
    remove(DIR "/block.bin");
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(!pe_debug_control_start(NULL));
    assert(!pe_debug_control_start(""));
    assert(!s_hooks[PE_HOOK_FRAME]);
    assert(pe_debug_control_start(DIR));
    assert(!pe_debug_control_start(DIR));  /* no repeated hook installation */
    {
        size_t count, m, c, other, index;
        const PeDebugModule *const *modules = pe_debug_modules(&count);
        assert(count > 1);
        for (m = 0; m < count; ++m) {
            assert(modules[m]->name && modules[m]->commands && modules[m]->count);
            for (c = 0; c < modules[m]->count; ++c) {
                const PeDebugCommand *command = &modules[m]->commands[c];
                assert(command->name && *command->name && command->run);
                assert(command->arguments >= PE_DEBUG_EXACT && command->arguments <= PE_DEBUG_OPTIONAL);
                for (other = m; other < count; ++other)
                    for (index = other == m ? c + 1 : 0; index < modules[other]->count; ++index)
                        assert(strcmp(command->name, modules[other]->commands[index].name));
            }
        }
    }

    /* Map-number conversion is checked through the actual registered warp handler. */
    assert(!strcmp(ask("warp 195"), "ok warp token=A80292C8"));
    assert(!strcmp(ask("warp 374"), "ok warp token=A8067248"));
    assert(starts(ask("warp -1"), "err warp"));
    s_warped = 0;

    assert(starts(ask("ping"), "ok ping "));
    assert(!strcmp(ask("nonsense"), "err nonsense unknown command"));
    assert(!strcmp(ask("battle-state-more"), "err battle-state-more unknown command"));
    assert(!strcmp(ask("rng-get extra"), "err rng-get extra unknown command"));
    assert(!strcmp(ask("trace on more"), "err trace on more unknown command"));
    assert(!strcmp(ask("ping "), "err ping  unknown command"));
    {
        char snapshot[1200], command[1300];
        int value = 0;
        s_row.kind = PE_ROW_TOGGLE; s_row.value = &value;
        s_row.label = "Enabled"; s_row.mod_id = "test";
        snprintf(s_row.page, sizeof s_row.page, "Test");
        assert(!strcmp(ask("ini-set Test/Enabled 1"), "ok ini-set Test/Enabled=1 restart_required=0") && value == 1);
        assert(starts(ask("ini-set Test/Enabled 2"), "err ini-set") && value == 1);
        assert(starts(ask("ini-set unknown 0"), "err ini-set"));
        assert(strstr(ask("mod-state test"), "\"running\":true"));
        assert(!strstr(s_answer, "\"live\""));                  /* no providers yet */
        assert(starts(ask("mod-state unknown"), "err mod-state"));
        /* A choice whose list comes from the mod (F1 > Fights), and an action row pressed as Enter. */
        s_row.kind = PE_ROW_DYNAMIC_CHOICE; s_row.label = "Fight"; s_row.count_fn = test_three; s_row.count = 10;
        assert(!strcmp(ask("ini-set Test/Fight 2"), "ok ini-set Test/Fight=2 restart_required=0") && value == 2);
        assert(starts(ask("ini-set Test/Fight 3"), "err ini-set") && value == 2);
        s_row.kind = PE_ROW_ACTION; s_row.label = "Start"; s_row.action = test_press; s_row.value = NULL;
        assert(starts(ask("ini-set Test/Start 1"), "err ini-set"));
        assert(!strcmp(ask("mod-action Test/Start"), "ok mod-action Test/Start") && s_pressed == 1);
        assert(!strcmp(ask("mod-action Test/Other"), "err mod-action unknown action") && s_pressed == 1);
        s_row.kind = PE_ROW_TOGGLE; s_row.label = "Enabled"; s_row.value = &value;
        {
            static int junk = 12;
            assert(pe_mod_state_provider("wallet", wallet_state, &junk) == -1);   /* outside init */
            s_current_mod = "test";
            assert(pe_mod_state_provider("wallet", wallet_state, &junk) == 0);
            assert(pe_mod_state_provider("wallet", wallet_state, &junk) == -1);  /* same name */
            assert(pe_mod_state_provider("", wallet_state, &junk) == -1);
            s_current_mod = "stopped";
            assert(pe_mod_state_provider("wallet", wallet_state, &junk) == 0);   /* other owner */
            s_current_mod = NULL;
            s_mod_calls = 0;
            assert(strstr(ask("mod-state test"),
                          "\"live\":{\"wallet\":{\"junk\":12,\"name\":\"a \\\"quoted\\\" one\",\"list\":[1,{\"ok\":true}]}}}"));
            assert(s_mod_calls == 1);
            /* A stopped mod's provider is never called; it still answers, with live null. */
            assert(strstr(ask("mod-state stopped"), "\"running\":false,\"rows\":[],\"live\":null}"));
            assert(s_mod_calls == 1);
        }
        {
            /* ui-state: one dialogue box waiting on a two-way choice ("Yes"), and a page the
             * box opened earlier, read through the late TEXT_PAGE hook. */
            static unsigned char yes[] = { 0x28, 0x34, 0x42, 0xFF };
            static unsigned char hello[] = { 0x17, 0x34, 0x3B, 0x3B, 0x3E, 0xFF };
            unsigned char *message = hello;
            PeHookTextPage page = { NULL, 23, &message };
            assert(s_hooks[PE_HOOK_TEXT_PAGE]);
            s_hooks[PE_HOOK_TEXT_PAGE](&page, s_hook_users[PE_HOOK_TEXT_PAGE]);
            g_TextboxEntries[1].state = 2;
            g_TextboxEntries[1].page_id = 24;
            g_TextboxEntries[1].message = yes;
            g_TextboxEntries[1].control.flags = 0x200000 | (2 << 0x16);
            g_DialogueChoiceCursor = 1;
            g_SaveSelectedSlot = 3;
            ask("ui-state");
            assert(starts(s_answer, "ok ui-state {\"menu\":{\"open\":false"));
            assert(strstr(s_answer, "\"save_slot\":3"));
            assert(strstr(s_answer, "{\"box\":1,\"state\":\"waiting\",\"page\":24,"));
            assert(strstr(s_answer, "\"text\":\"Yes\",\"choices\":2,\"choice\":1}"));
            assert(strstr(s_answer, "\"recent_pages\":[{\"frame\":0,\"map\":48,\"page\":23,\"text\":\"Hello\"}]"));
            /* A mod rewrites the open box of page 23: the page's entry follows what it draws. */
            g_TextboxEntries[1].page_id = 23;
            s_hooks[PE_HOOK_FRAME](NULL, s_hook_users[PE_HOOK_FRAME]);
            assert(strstr(ask("ui-state"), "\"page\":23,\"text\":\"Yes\"}]"));
            g_TextboxEntries[1].state = 0;
        }
        {
            /* snapshot: one JSON file holding every part, with the commands that led here. */
            char *json;
            long size;
            assert(starts(ask("snapshot a/b"), "err snapshot"));
            assert(!strcmp(ask("snapshot fail-1"), "ok snapshot snapshot-fail-1.json"));
            f = fopen(DIR "/snapshot-fail-1.json", "rb");
            assert(f);
            fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
            json = calloc(1, (size_t)size + 1);
            assert(json && fread(json, 1, (size_t)size, f) == (size_t)size);
            fclose(f);
            assert(starts(json, "{\"version\":1,"));
            assert(strstr(json, "\"room\":{\"map\":48"));
            assert(strstr(json, "\"ui\":{\"menu\""));
            assert(strstr(json, "\"battle\":{\"active\""));
            assert(strstr(json, "\"bag\":["));
            assert(strstr(json, "\"actors\":["));
            assert(strstr(json, "\"mods\":{\"test\":{\"wallet\":{\"junk\":12"));
            assert(!strstr(json, "\"stopped\""));
            assert(strstr(json, "\"line\":\"> snapshot fail-1\"}]"));
            assert(strstr(json, "\"line\":\"err snapshot "));
            free(json);
            assert(!strcmp(ask("snapshot"), "ok snapshot snapshot.json"));
            assert(!fopen(DIR "/snapshot.json.tmp", "r"));
        }
        Psyz_Srand(123); g_FrameRngCounter = 456; g_ItemDropRngIndex = 520;
        memset(g_ItemDropRngTable, 0xab, sizeof g_ItemDropRngTable);
        snprintf(snapshot, sizeof snapshot, "%s", ask("rng-get") + 11);
        Psyz_Srand(789); g_FrameRngCounter = 0; g_ItemDropRngIndex = 0;
        memset(g_ItemDropRngTable, 0, sizeof g_ItemDropRngTable);
        snprintf(command, sizeof command, "rng-set %s", snapshot);
        assert(starts(ask(command), "ok rng-set 123 456 520 ab"));
        assert(Psyz_RandState() == 123 && g_FrameRngCounter == 456 && g_ItemDropRngTable[520] == 0xab);
        assert(starts(ask("rng-set 0 0 521 00"), "err rng-set"));
        assert(Psyz_RandState() == 123 && g_ItemDropRngIndex == 520);
        assert(strstr(ask("battle-state"), "\"active\":false"));
        assert(!strcmp(ask("trace on"), "ok trace on"));
        s_hooks[PE_HOOK_FRAME](NULL, s_hook_users[PE_HOOK_FRAME]);
        assert(!strcmp(ask("trace off"), "ok trace off"));
    }

    /* room */
    g_SceneDispatchToken = g_SceneDispatchCur = (int)0xA8004448u;
    g_ScriptVariableBank[74] = 0x1F4;
    g_GameState.current_story_day = 5;
    assert(!strcmp(ask("room"), "ok room map=48 name=m0048i token=A8004448 story=1F4 day=5 free=1"));

    /* warp: the refusals, then a transfer with story progress set first */
    assert(!strcmp(ask("warp 1000"), "err warp no such room"));
    assert(!strcmp(ask("warp 0x12345678"), "err warp no such room"));
    assert(!strcmp(ask("warp 48"), "err warp already in that room") && !s_warped);
    s_free = 0;
    assert(!strcmp(ask("warp 52"), "err warp not in free field") && !s_warped);
    s_free = 1;
    assert(!strcmp(ask("warp 52 1D0"), "ok warp token=A8005148"));
    assert(s_warped == 1 && s_warp_arg == 0xA8005148u && g_ScriptVariableBank[74] == 0x1D0);
    assert(!strcmp(ask("warp 0xA80292C8"), "ok warp token=A80292C8") && g_ScriptVariableBank[74] == 0x1D0);

    /* script variables */
    assert(!strcmp(ask("set-var 48 12C"), "ok set-var 48=12C") && g_ScriptVariableBank[48] == 300);
    assert(!strcmp(ask("get-var 48"), "ok get-var 48=12C"));
    assert(starts(ask("get-var 512"), "err get-var"));
    assert(starts(ask("set-var 3"), "err set-var"));

    /* set-day: the game's own day change, 1..8, free field only */
    assert(!strcmp(ask("set-day 3"), "ok set-day day=3") && s_day_calls == 1);
    assert(starts(ask("set-day 9"), "err set-day") && starts(ask("set-day x"), "err set-day") && s_day_calls == 1);
    s_free = 0;
    assert(!strcmp(ask("set-day 2"), "err set-day not in free field") && s_day_calls == 1);
    s_free = 1;
    g_GameState.current_story_day = 5;

    /* actors: Wayne (2.0) holds his Junk count at +1A98; Aya is the second actor */
    s_actor[0].next = &s_actor[1];
    s_actor[1].next = &s_actor[2];
    s_actor[0].type_id = 2;  s_actor[0].pos_x = 0;        s_actor[0].pos_z = 0;        s_actor[0].script_base = s_script[0];
    s_actor[1].type_id = 0;  s_actor[1].pos_x = 0;        s_actor[1].pos_z = 0x400000; s_actor[1].script_base = s_script[1];
    s_actor[2].type_id = 6;  s_actor[2].pos_x = -0x10000; s_actor[2].pos_z = 5;        s_actor[2].script_base = NULL;
    memcpy(s_script[0] + 0x1A98, junk_count, sizeof junk_count);
    g_FieldActorListHead = &s_actor[0];
    g_PlayerEntity = &s_actor[1];
    assert(!strcmp(ask("actors"), "ok actors 0:2.0@0,0,f0 1:0.0*@0,4194304,f0 2:6.0@-65536,5,f0"));
    {
        static Combatant player;
        static FieldActorState npc;
        player.curHP = 45; player.maxHP = 100; player.atb = 9000;
        player.currentPe = 123 << 16; player.maxPe = 200 << 16;
        g_ActiveActor = &player;
        s_actor[1].state = (FieldActorState *)&player;
        s_actor[0].state = (FieldActorState *)&g_BattleEnemySlots[0].combatant;
        s_actor[2].state = &npc;
        g_BattleEnemySlots[0].active = 1;
        g_BattleEnemySlots[0].combatant.hp = 35;
        g_BattleEnemySlots[0].combatant.maxHp = 35;
        g_BattleEnemySlots[0].combatant.coreFlags = 0x18000;
        g_GameStateFlags |= 2;
        assert(strstr(ask("battle-state"), "\"hp\":35,\"max_hp\":35"));
        assert(!strstr(s_answer, "\"actor\":2"));  /* NPC state is not an enemy core */
        remove(DIR "/battle.jsonl");
        ask("trace on");
        s_hooks[PE_HOOK_ENEMY_HIT]((BattleEntity *)&s_actor[0], s_hook_users[PE_HOOK_ENEMY_HIT]);
        g_BattleEnemySlots[0].combatant.hp = -19;
        s_hooks[PE_HOOK_ENEMY_HIT_END]((BattleEntity *)&s_actor[0], s_hook_users[PE_HOOK_ENEMY_HIT_END]);
        {
            char event[1024];
            f = fopen(DIR "/battle.jsonl", "r");
            assert(f && fgets(event, sizeof event, f)); fclose(f);
            assert(strstr(event, "\"loss\":54") && strstr(event, "\"critical\":true"));
        }
        ask("trace off");
        g_GameStateFlags &= ~2u;
        assert(!strcmp(ask("set-stat hp 80"), "ok set-stat hp=80") && player.curHP == 80);
        assert(starts(ask("set-stat hp 101"), "err set-stat") && player.curHP == 80);
        assert(!strcmp(ask("give 14"), "ok give 14") && s_given == 0x14);
        assert(starts(ask("give 100"), "err give"));
        g_AyaInventoryItems[0] = 0x100;
        g_InventoryRuntime.equipment[0].iconId = 1;
        g_InventoryRuntime.equipment[0].kind = 4;
        g_InventoryRuntime.equipment[0].itemId = 0x44;
        assert(!strcmp(ask("equip 0"), "ok equip slot=0 id=44"));
        s_free = 0;
        assert(starts(ask("give 14"), "err give"));
        assert(starts(ask("set-stat bp 99"), "err set-stat"));
        assert(starts(ask("equip 0"), "err equip"));
        s_free = 1;
    }
    assert(!strcmp(ask("find-script 1040A8 0 14"), "ok find-script 0@1A98"));
    assert(!strcmp(ask("find-script 1040A8 0 15"), "ok find-script none"));
    assert(!strcmp(ask("find-script zz"), "err find-script find-script WORD [WORD ...]"));
    /* A signature beyond the payload must not be read, even when the storage
     * happens to continue with another task. */
    s_script_bytes[0] = 0x1A98 + sizeof junk_count - 1;
    assert(!strcmp(ask("find-script 001040A8 0 14"), "ok find-script none"));
    s_script_bytes[0] = PE_SCRIPT_SCAN;

    /* place / place-near: beside Wayne on Aya's side, facing him (yaw 0x800 = toward -z) */
    assert(!strcmp(ask("place-near 0"), "ok place-near 0,1572864,2048"));
    assert(s_actor[1].pos_z == 0x180000 && s_actor[1].base_z == 0x180000 && s_actor[1].rot_y == 0x800);
    assert(!strcmp(ask("place-near 0 0x100000"), "ok place-near 0,1048576,2048"));
    assert(starts(ask("place-near 1"), "err place-near"));      /* Aya herself */
    assert(starts(ask("place-near 9"), "err place-near"));      /* nobody */
    assert(!strcmp(ask("place 100 -200 1024"), "ok place 100,-200"));
    assert(s_actor[1].pos_x == 100 && s_actor[1].pos_z == -200 && s_actor[1].rot_y == 1024);
    assert(s_floor_refreshes == 3 && s_actor[1].collision_face == (void *)s_script);
    /* Off the walkable mesh: refused, and Aya stays where she was, on her floor (a null floor
     * triangle crashed the field's movement). */
    s_no_floor = 1;
    assert(starts(ask("place 999999999 999999999 7"), "err place no floor"));
    assert(s_actor[1].pos_x == 100 && s_actor[1].pos_z == -200 && s_actor[1].rot_y == 1024);
    assert(s_actor[1].collision_face == (void *)s_script && s_actor[1].collision_face_mirror == (void *)s_script);
    assert(starts(ask("place-near 0 0x7FFFFFF"), "err place-near no floor"));
    assert(s_actor[1].pos_x == 100 && s_actor[1].pos_z == -200 && s_actor[1].collision_face == (void *)s_script);
    s_no_floor = 0;

    /* save-block: no companion, then with one (copied, named, and the staged one dropped) */
    assert(!strcmp(ask("save-block 1"), "ok save-block BASLUS-006620000000A") && s_cancelled == 1);
    s_extlink = 1;
    assert(!strcmp(ask("save-block 1"), "err save-block can't copy the companion file") && s_cancelled == 2);
    f = fopen(DIR "/cards/.pe-ext/BASLUS-006620000000A.00000004.pex", "wb");
    assert(f && fwrite("PEEX-companion", 1, 14, f) == 14);
    fclose(f);
    assert(!strcmp(ask("save-block 1"), "ok save-block BASLUS-006620000000A companion=BASLUS-006620000000A.00000004.pex"));
    assert(s_cancelled == 3);
    {
        char got[32] = { 0 };
        f = fopen(DIR "/companion.pex", "rb");
        assert(f && fread(got, 1, sizeof got, f) == 14 && !memcmp(got, "PEEX-companion", 14));
        fclose(f);
        f = fopen(DIR "/block.bin", "rb");
        assert(f && fread(got, 1, 4, f) == 4 && got[0] == 0x5A);
        fclose(f);
    }
    s_free = 0;
    assert(!strcmp(ask("save-block 1"), "err save-block not in free field"));
    assert(!strcmp(ask("save-block"), "err save-block not in free field"));
    assert(!strcmp(ask("save-block "), "err save-block slot 1..15"));

    /* Publishing a next request during processing must survive cleanup. */
    f = fopen(DIR "/cmd", "w"); assert(f); fputs("ping\n", f); fclose(f);
    s_enqueue_during_dispatch = 1;
    pe_debug_control_poll();
    f = fopen(DIR "/cmd", "r"); assert(f); fclose(f);
    pe_debug_control_poll();
    assert(!fopen(DIR "/cmd", "r") && !fopen(DIR "/cmd.processing", "r"));

    /* Event log: changes only, one line each, in the order they happened. */
    {
        static unsigned char hi[] = { 0x17, 0x38, 0xFF };          /* "Hi" */
        static unsigned char hi_more[] = { 0x17, 0x38, 0x2B, 0xFF }; /* "Hi!" */
        static const char *const expected[] = {
            "\"event\":\"dialogue_opened\",\"box\":2,\"page\":7,\"choices\":2,\"text\":\"Hi\"",
            "\"event\":\"dialogue_waiting\",\"box\":2,\"page\":7,\"choices\":2,\"text\":\"Hi\"",
            "\"event\":\"dialogue_changed\",\"box\":2,\"page\":7,\"choices\":3,\"text\":\"Hi!\"",
            "\"event\":\"dialogue_closed\",\"box\":2,\"page\":7,\"choices\":3,\"answer\":1",
            "\"event\":\"room\",\"map\":48,\"token\":\"A8005148\"",
            "\"event\":\"battle_started\"",
            "\"event\":\"player_down\"",
            "\"event\":\"battle_ended\",\"hp\":0",
            "\"event\":\"menu_opened\"",
            "\"event\":\"no_control\"",
            "\"event\":\"title\"",
            "\"event\":\"save_loaded\"",
            "\"event\":\"title_left\"",
            "\"event\":\"scene_loaded\",\"map\":48",
            "\"event\":\"new_game\"",
            "\"event\":\"save_committed\"",
            "\"event\":\"battle_started\"",
            "\"event\":\"battle_ended\",\"hp\":80,\"left\":true",
            "\"event\":\"to_title\",\"from_map\":48",
        };
        char line[1024], answer[64];
        unsigned n = 0, seq;
        Combatant *aya = (Combatant *)g_PlayerEntity->state;
        s_free = 1;
        frame();                                  /* settle: the state as it is now */
        remove(DIR "/events.jsonl");
        assert(sscanf(ask("events"), "ok events %u", &seq) == 1);
        frame();
        assert(!fopen(DIR "/events.jsonl", "r"));  /* nothing changed: nothing written */
        g_TextboxEntries[2].state = 1; g_TextboxEntries[2].page_id = 7; g_TextboxEntries[2].message = hi;
        g_TextboxEntries[2].control.flags = 0x200000 | (2 << 0x16);
        g_MenuEquipSlotIndex = -1;
        frame(); frame();                         /* opened once, not twice */
        g_TextboxEntries[2].state = 2; frame();
        g_TextboxEntries[2].message = hi_more;              /* a mod adds a row */
        g_TextboxEntries[2].control.flags = 0x200000 | (3 << 0x16);
        frame(); frame();
        g_MenuEquipSlotIndex = 1; g_TextboxEntries[2].state = 0; frame();
        g_SceneDispatchToken = (int)0xA8005148u; frame();
        g_GameStateFlags |= 2; frame();
        aya->curHP = 0; frame(); frame();
        g_GameStateFlags &= ~2u; frame();
        g_GameStateFlags |= 4; frame();
        s_free = 0; frame();
        fire(PE_HOOK_TITLE_FRAME); fire(PE_HOOK_TITLE_FRAME);
        fire(PE_HOOK_SAVE_READ);
        frame();
        fire(PE_HOOK_SCENE_START); fire(PE_HOOK_NEW_GAME); fire(TEST_SAVE_EVENT);
        /* A game over: Aya down in a battle, the field loop ends for the title. Its last frame's
         * FRAME_DRAWN sees the title token; no field frame follows. */
        aya->curHP = 80;
        g_GameStateFlags |= 2; frame();
        g_SceneDispatchToken = (int)0xA9400048u;
        fire(PE_HOOK_FRAME_DRAWN); fire(PE_HOOK_FRAME_DRAWN);
        g_GameStateFlags &= ~6u; aya->curHP = 80; s_free = 1;
        g_SceneDispatchToken = (int)0xA8005148u;
        f = fopen(DIR "/events.jsonl", "r");
        assert(f);
        while (fgets(line, sizeof line, f)) {
            char want[32];
            assert(n < sizeof expected / sizeof *expected);
            snprintf(want, sizeof want, "{\"seq\":%u,", seq + n + 1);
            assert(starts(line, want));
            assert(strstr(line, expected[n]));
            ++n;
        }
        fclose(f);
        assert(n == sizeof expected / sizeof *expected);
        snprintf(answer, sizeof answer, "ok events %u", seq + n);
        assert(!strcmp(ask("events"), answer));
    }
    puts("Debug control: room ids, warp, variables, actors, bounded find-script, floor refresh, save-block, queued commands, ui-state, snapshot, live mod state and the event log");
    return 0;
}
