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


#define PE_SCRIPT_SCAN 0x8000
static PeHookFn s_hooks[PE_HOOK_COUNT];
static void *s_hook_users[PE_HOOK_COUNT];


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
void Entity_FindFloor(struct BattleEntity *actor) {
    FieldActor *field = (FieldActor *)actor;
    assert(!field->collision_face && !field->collision_face_mirror);
    field->collision_face = field->collision_face_mirror = (void *)s_script;
    ++s_floor_refreshes;
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
    puts("Debug control: room ids, warp, variables, actors, bounded find-script, floor refresh, save-block and queued commands");
    return 0;
}
