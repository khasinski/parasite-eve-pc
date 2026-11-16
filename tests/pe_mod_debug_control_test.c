/* The debug control's commands (mods/host/pe_debug_control.c) against a stand-in game: map numbers
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
#include "../mods/host/pe_debug_control.c"

#define DIR "pe-debug-control-test"

/* ---- the game around it ---- */
u8 g_Base32CharTable[32] = "0123456789abcdefghiklmnoprstuvwy";   /* src/main/cdrom/scene_assets.c */
int g_SceneDispatchToken, g_SceneDispatchCur;
u32 g_ScriptVariableBank[512];
Pe1GameState g_GameState;
SaveRuntimeState g_SaveRuntimeState;
GameStateBlock g_GameStateBlock;
FieldActor *g_FieldActorListHead, *g_PlayerEntity;
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
    (void)id; (void)fn; (void)user; (void)priority; (void)mod;
    return 0;
}
void pe_log(const char *tag, const char *fmt, ...) { (void)tag; (void)fmt; }
void pe_mod_log(const char *mod, const char *fmt, ...) { (void)mod; (void)fmt; }

/* ---- a command in, its answer out ---- */
static char s_answer[4096];
static const char *ask(const char *cmd) {
    FILE *f = fopen(DIR "/cmd", "w");
    assert(f);
    fprintf(f, "%s\n", cmd);
    fclose(f);
    remove(DIR "/out");
    pe_ctl_poll();
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
    snprintf(s_dir, sizeof s_dir, "%s", DIR);

    /* Map numbers -> room ids, as pe_warp's checkpoints and the game's own rooms have them. */
    assert(pe_ctl_room_id(48) == 0xA8004448u);
    assert(pe_ctl_room_id(52) == 0xA8005148u);
    assert(pe_ctl_room_id(195) == 0xA80292C8u);
    assert(pe_ctl_room_id(374) == 0xA8067248u);
    assert(pe_ctl_room_id(1000) == 0 && pe_ctl_room_id(-1) == 0);

    assert(starts(ask("ping"), "ok ping "));
    assert(!strcmp(ask("nonsense"), "err nonsense unknown command"));

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
    assert(!strcmp(ask("find-script 1040A8 0 14"), "ok find-script 0@1A98"));
    assert(!strcmp(ask("find-script 1040A8 0 15"), "ok find-script none"));
    assert(!strcmp(ask("find-script zz"), "err find-script find-script WORD [WORD ...]"));

    /* place / place-near: beside Wayne on Aya's side, facing him (yaw 0x800 = toward -z) */
    assert(!strcmp(ask("place-near 0"), "ok place-near 0,1572864,2048"));
    assert(s_actor[1].pos_z == 0x180000 && s_actor[1].base_z == 0x180000 && s_actor[1].rot_y == 0x800);
    assert(!strcmp(ask("place-near 0 0x100000"), "ok place-near 0,1048576,2048"));
    assert(starts(ask("place-near 1"), "err place-near"));      /* Aya herself */
    assert(starts(ask("place-near 9"), "err place-near"));      /* nobody */
    assert(!strcmp(ask("place 100 -200 1024"), "ok place 100,-200"));
    assert(s_actor[1].pos_x == 100 && s_actor[1].pos_z == -200 && s_actor[1].rot_y == 1024);

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

    puts("Debug control: room ids, warp, variables, actors, find-script, place, save-block with companion");
    return 0;
}
