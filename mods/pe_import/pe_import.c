/* F1 > Import: models from the player's other PlayStation games, into Parasite Eve, by themselves.
 *
 * The player only provides the discs: disc images (.cue with its .bin, or .iso) in
 * <preferences>/import/discs/, loose or one folder deep. When the game starts, a worker thread runs
 * the automatic import (pe_import_job.c): new or changed disc images are read into the store, and
 * everything the source games mark automatic (RE2: Claire, Leon) is built into packs in
 * <preferences>/packs/. On the main thread the Wardrobe then adds the new costumes at once
 * (wardrobe_packs service), no restart. The page shows what's ready and which discs are missing,
 * and still builds any other catalogued model by hand.
 *
 * Other games' files are read only by the importer, never by gameplay. PC and PS5 (stdio, SDL). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "pe_imp.h"
#include "pe_imp_pe.h"
#include "pe_import.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_services.h"
#include "pe1/render_object.h"

#define MOD_ID "pe_import"
#define PAGE "Import"
#define MAX_ITEMS 256

static PeImportJob s_job;
static SDL_Thread *s_thread;
static SDL_AtomicInt s_state;                 /* 0 idle, 1 working, 2 done (to be published) */
static int s_started;
static char s_auto[400] = "starting";
static char s_scan[240] = "-";
static char s_build[240] = "-";

/* ---- folders the player added (import/folders.txt: one per line, # for comments) ----
 * So disc images can stay where they are (a folder of games, one per subfolder). PC adds them with
 * the system's folder picker; on PS5 the file is edited by hand. */
static char s_folders_file[1100];
static SDL_AtomicInt s_picked;                /* 1: s_pick_path holds a folder from the picker */
static char s_pick_path[1100];
static int start_job(void);
static void poll(void);

static void folders_load(void) {
    FILE *f = fopen(s_folders_file, "rb");
    char line[1200];
    s_job.nextra = 0;
    if (!f) return;
    while (fgets(line, sizeof line, f) && s_job.nextra < PE_IMPORT_FOLDERS) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) line[--l] = 0;
        if (!l || line[0] == '#' || l >= sizeof s_job.extra[0]) continue;
        snprintf(s_job.extra[s_job.nextra++], sizeof s_job.extra[0], "%s", line);
    }
    fclose(f);
}

static int folders_save(void) {
    FILE *f = fopen(s_folders_file, "wb");
    if (!f) return -1;
    fputs("# More folders F1 > Import reads disc images from (one per line; loose or one folder deep)\n", f);
    for (int i = 0; i < s_job.nextra; i++) fprintf(f, "%s\n", s_job.extra[i]);
    return fclose(f) ? -1 : 0;
}

/* The picker answers on its own time and maybe its own thread: keep the folder for the frame tick. */
static void SDLCALL on_picked(void *user, const char *const *list, int filter) {
    (void)user, (void)filter;
    if (!list || !list[0] || SDL_GetAtomicInt(&s_picked)) return;
    snprintf(s_pick_path, sizeof s_pick_path, "%s", list[0]);
    SDL_SetAtomicInt(&s_picked, 1);
}

static void add_folder(void) {
    if (s_job.nextra >= PE_IMPORT_FOLDERS) { pe_toast("Import: 8 folders at most; forget some first"); return; }
    SDL_ShowOpenFolderDialog(on_picked, NULL, NULL, NULL, false);
}

/* On the main thread, when no import is running: add it, save, read it. */
static void apply_picked(void) {
    if (!SDL_GetAtomicInt(&s_picked) || SDL_GetAtomicInt(&s_state) != 0) return;
    int have = 0;
    for (int i = 0; i < s_job.nextra; i++) have |= !strcmp(s_job.extra[i], s_pick_path);
    if (!have && s_job.nextra < PE_IMPORT_FOLDERS) snprintf(s_job.extra[s_job.nextra++], sizeof s_job.extra[0], "%s", s_pick_path);
    SDL_SetAtomicInt(&s_picked, 0);
    if (folders_save()) pe_toast("Import: couldn't save the folder list");
    start_job();
    pe_toast("Import: reading the folder's disc images...");
}

static void forget_folders(void) {
    if (SDL_GetAtomicInt(&s_state) != 0) { pe_toast("Import: still working, try again in a moment"); return; }
    if (!s_job.nextra) { pe_toast("Import: no added folders"); return; }
    if (pe_menu_confirm("Forget the added folders?", "Disc images stay where they are; costumes already built stay.") != 1) return;
    s_job.nextra = 0;
    folders_save();
    pe_toast("Import: added folders forgotten");
}

static void text_folders(char *out, unsigned size) {
    if (!s_job.nextra) { snprintf(out, size, "none"); return; }
    out[0] = 0;
    for (int i = 0; i < s_job.nextra; i++) {
        size_t u = strlen(out);
        snprintf(out + u, size - u, "%s%s", i ? "; " : "", s_job.extra[i]);
    }
}

/* ---- the list of what can be built by hand ---- */
typedef struct { PeImpAsset a; char label[96]; int target; } Item;
static Item s_items[MAX_ITEMS];
static int s_nitems, s_pick, s_listed;

static void refresh(void) {
    char games[32][16];
    int ng = 0;
    s_nitems = 0;
    for (int r = 0; r < pe_imp_readers() && ng < 32; r++) {
        int seen = 0;
        for (int g = 0; g < ng; g++) seen |= !strcmp(games[g], pe_imp_reader(r)->game);
        if (!seen) snprintf(games[ng++], 16, "%s", pe_imp_reader(r)->game);
    }
    for (int g = 0; g < ng; g++) {
        PeImpAsset *a;
        int n = pe_imp_catalog_assets(s_job.store, games[g], &a);
        for (int i = 0; i < n && s_nitems < MAX_ITEMS; i++)
            for (int t = 0; t < pe_imp_targets(); t++) {
                const PeImpTarget *tg = pe_imp_target(t);
                uint32_t type = !strcmp(a[i].type, "Model") ? PE_IMP_MODEL : 0;
                if (!(tg->consumes & type) || (tg->needs_rig && !pe_imp_rig_for(a[i].id))) continue;
                Item *it = &s_items[s_nitems++];
                it->a = a[i], it->target = t;
                snprintf(it->label, sizeof it->label, "%s (%s)", a[i].name, pe_imp_game_title(games[g]));
                break;
            }
        free(a);
    }
    if (s_pick >= s_nitems) s_pick = 0;
    s_listed = 1;
}

static void listed(void) { if (!s_listed && SDL_GetAtomicInt(&s_state) == 0) refresh(); }
static unsigned item_count(void) { listed(); return s_nitems ? (unsigned)s_nitems : 1u; }
static const char *item_name(unsigned i) {
    listed();
    return s_nitems ? s_items[i < (unsigned)s_nitems ? i : 0].label : "nothing yet: add disc images";
}

/* ---- progress: the worker writes it, the page and the frame tick read it (under s_lock) ---- */
static SDL_Mutex *s_lock;
static struct { char job[200]; int jd, jt; char step[64]; int sd, st, builds; } s_prog;
static int s_builds_said;                       /* main thread: build messages already shown */

static void on_job(void *user, const char *what, int done, int total) {
    (void)user;
    if (s_lock) SDL_LockMutex(s_lock);
    snprintf(s_prog.job, sizeof s_prog.job, "%s", what);
    s_prog.jd = done, s_prog.jt = total, s_prog.step[0] = 0, s_prog.sd = s_prog.st = 0;
    if (!strncmp(what, "building ", 9)) s_prog.builds++;
    if (s_lock) SDL_UnlockMutex(s_lock);
}

static void on_step(void *user, const char *what, int done, int total) {
    (void)user;
    if (s_lock) SDL_LockMutex(s_lock);
    snprintf(s_prog.step, sizeof s_prog.step, "%s", what);
    s_prog.sd = done, s_prog.st = total;
    if (s_lock) SDL_UnlockMutex(s_lock);
}

static const PeImpProgress k_job_progress = {on_job, NULL}, k_step_progress = {on_step, NULL};

/* "[########------------] 40%  building Leon (Resident Evil 2), 2 of 2: room animations 340 of 900" */
static void progress_text(char *out, unsigned size) {
    if (s_lock) SDL_LockMutex(s_lock);
    double f = 0;
    if (s_prog.jt > 0) f = ((s_prog.jd > 0 ? s_prog.jd - 1 : 0) + (s_prog.st > 0 ? (double)s_prog.sd / s_prog.st : 0)) / s_prog.jt;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    char bar[24];
    int fill = (int)(f * 20 + 0.5);
    for (int i = 0; i < 20; i++) bar[i] = i < fill ? '#' : '-';
    bar[20] = 0;
    /* a spinner from the clock: it turns even while one step takes a while (the panel repaints
     * when this text changes) */
    static const char spin[4] = {'|', '/', '-', '\\'};
    const char c = spin[(SDL_GetTicks() / 150) % 4];
    if (!s_prog.job[0]) snprintf(out, size, "%c [%s] starting", c, bar);
    else if (s_prog.step[0])
        snprintf(out, size, "%c [%s] %d%%  %s, %d of %d: %s %d of %d", c, bar, (int)(f * 100), s_prog.job, s_prog.jd,
                 s_prog.jt, s_prog.step, s_prog.sd, s_prog.st);
    else snprintf(out, size, "%c [%s] %d%%  %s, %d of %d", c, bar, (int)(f * 100), s_prog.job, s_prog.jd, s_prog.jt);
    if (s_lock) SDL_UnlockMutex(s_lock);
}

/* On the main thread: a message as each costume starts building, so it shows outside F1 too. */
static void say_builds(void) {
    char msg[240] = "";
    if (s_lock) SDL_LockMutex(s_lock);
    if (s_prog.builds != s_builds_said) {
        s_builds_said = s_prog.builds;
        snprintf(msg, sizeof msg, "Import: %s (%d of %d)...", s_prog.job, s_prog.jd, s_prog.jt);
    }
    if (s_lock) SDL_UnlockMutex(s_lock);
    if (msg[0]) pe_toast(msg);
}

/* ---- the automatic import, on a worker thread ---- */
static int worker(void *user) {
    (void)user;
    pe_import_job_run(&s_job);
    SDL_SetAtomicInt(&s_state, 2);
    return 0;
}

static int start_job(void) {
    if (SDL_GetAtomicInt(&s_state) != 0) return 0;
    SDL_SetAtomicInt(&s_state, 1);
    snprintf(s_auto, sizeof s_auto, "working: reading your disc images and building...");
    if (!s_lock) s_lock = SDL_CreateMutex();
    if (s_lock) SDL_LockMutex(s_lock);
    memset(&s_prog, 0, sizeof s_prog);
    s_builds_said = 0;
    if (s_lock) SDL_UnlockMutex(s_lock);
    s_job.progress = &k_job_progress, s_job.step = &k_step_progress;
    if (!(s_thread = SDL_CreateThread(worker, "pe_import", NULL))) {
        pe_import_job_run(&s_job);                                    /* no threads: do it here */
        SDL_SetAtomicInt(&s_state, 2);
    }
    return 1;
}

/* The job's results, on the main thread: tell the Wardrobe, say what happened. */
static void publish(void) {
    if (s_thread) SDL_WaitThread(s_thread, NULL), s_thread = NULL;
    const PeImportJob *j = &s_job;
    char added[400] = "";
    int n = 0;
    if (j->built) {
        const PeWardrobePacksApi *w = pe_service_get("wardrobe_packs", 1);
        if (w && w->struct_size >= sizeof *w) n = w->add_new(added, sizeof added);
    }
    int ready = j->built + j->current;
    if (j->failed) snprintf(s_auto, sizeof s_auto, "%d costume%s ready; problem: %s", ready, ready == 1 ? "" : "s", j->problem);
    else if (j->missing) snprintf(s_auto, sizeof s_auto, "%d costume%s ready; for the rest, add: %s", ready, ready == 1 ? "" : "s", j->missing_discs);
    else snprintf(s_auto, sizeof s_auto, "all %d costume%s ready", ready, ready == 1 ? "" : "s");
    snprintf(s_scan, sizeof s_scan, "%d new disc%s read, %d unchanged (%s); %d not supported yet", j->discs_read,
             j->discs_read == 1 ? "" : "s", j->unchanged, j->games[0] ? j->games : "none", j->unknown);
    pe_mod_log(MOD_ID, "automatic import: %s; scan: %s; built: %s", s_auto, s_scan, j->built_names[0] ? j->built_names : "nothing new");
    if (n) {
        char msg[480];
        snprintf(msg, sizeof msg, "New in the Wardrobe: %s", added);
        pe_toast(msg);
    } else if (j->built) {
        pe_toast("Imported costumes are ready: restart the game to find them in the Wardrobe");
    } else if (j->failed) {
        pe_toast("Import: a problem, see F1 > Import");
    }
    SDL_SetAtomicInt(&s_state, 0);
    refresh();
}

/* Main thread, whenever it can: hand finished results to the Wardrobe, take a picked folder.
 * Called every field frame and from the page's own lines (frames stop while F1 is open; the
 * game sends no title-screen frames to mods). */
static void poll(void) {
    if (!s_started) s_started = 1, start_job();
    say_builds();
    if (SDL_GetAtomicInt(&s_state) == 2) publish();
    apply_picked();
}

static int on_frame(void *ctx, void *user) {
    (void)ctx, (void)user;
    poll();
    return 0;
}

static void scan_again(void) {
    if (!start_job()) pe_toast("Import: already working");
    else pe_toast("Import: reading your disc images...");
}

/* ---- by hand: any catalogued model ---- */
static void build(void) {
    if (SDL_GetAtomicInt(&s_state) != 0) { pe_toast("Import: still working, try again in a moment"); return; }
    listed();
    if (!s_nitems) { pe_toast("Import: add disc images first"); return; }
    Item *it = &s_items[s_pick < s_nitems ? s_pick : 0];
    const PeImpTarget *tg = pe_imp_target(it->target);
    char why[240] = "", folder[1100], log[160] = "", builder[64], added[200] = "";
    snprintf(builder, sizeof builder, "pe_import 1, %s %u", tg->module, tg->version);
    PeImpPe *pe = pe_import_game_source(why, sizeof why);
    int rc = !pe;
    if (pe) {
        PeImpBuild ctx = {s_job.store, pe, (const int16_t (*)[4])g_RenderNormalTable};
        rc = pe_imp_pack_character(s_job.packs, &it->a, builder, folder, sizeof folder, why, sizeof why) ||
             tg->build(&ctx, it->a.id, folder, log, sizeof log, why, sizeof why);
        pe_imp_pe_close(pe);
    }
    if (rc) snprintf(s_build, sizeof s_build, "%s: %s", it->label, why);
    else {
        const PeWardrobePacksApi *w = pe_service_get("wardrobe_packs", 1);
        int n = w && w->struct_size >= sizeof *w ? w->add_new(added, sizeof added) : 0;
        snprintf(s_build, sizeof s_build, "%s built (%s). %s", it->label, log,
                 n ? "It's in the Wardrobe now." : "Rebuilt: it shows when Aya next changes clothes or the game restarts.");
    }
    pe_mod_log(MOD_ID, "build: %s", s_build);
    pe_toast(s_build);
}

static void open_folder(void) {
    char url[1400];
    unsigned n = (unsigned)snprintf(url, sizeof url, "file:///");
    pe_imp_mkdirs(s_job.discs);
    for (unsigned i = 0; s_job.discs[i] && n + 4 < sizeof url; ++i) {
        char c = s_job.discs[i];
        if (c == '\\') url[n++] = '/';
        else if (c == ' ') { memcpy(url + n, "%20", 3); n += 3; }
        else url[n++] = c;
    }
    url[n] = 0;
    if (!SDL_OpenURL(url)) pe_toast("Import: couldn't open the folder; it is shown on this page");
}

static void text_auto(char *out, unsigned size) {                    /* F1 is open: no frames, so poll here */
    poll();
    if (SDL_GetAtomicInt(&s_state) == 1) progress_text(out, size);
    else snprintf(out, size, "%s", s_auto);
}
static void text_folder(char *out, unsigned size) { snprintf(out, size, "%s", s_job.discs); }
static void text_scan(char *out, unsigned size) { poll(); snprintf(out, size, "%s", s_scan); }
static void text_build(char *out, unsigned size) { snprintf(out, size, "%s", s_build); }
static void text_asset(char *out, unsigned size) {
    listed();
    if (!s_nitems) { snprintf(out, size, "-"); return; }
    const Item *it = &s_items[s_pick < s_nitems ? s_pick : 0];
    snprintf(out, size, "%s, %s %s", it->a.id, it->a.platform, it->a.path);
}

static int import_init(void) {
    if (pe_mod_path(s_job.discs, sizeof s_job.discs, "import/discs") || pe_mod_path(s_job.store, sizeof s_job.store, "import/store") ||
        pe_mod_path(s_job.packs, sizeof s_job.packs, "packs"))
        return 1;
    s_job.pe_source = pe_import_game_source;
    s_job.normals = (const int16_t (*)[4])g_RenderNormalTable;
    pe_imp_mkdirs(s_job.discs);
    if (pe_mod_path(s_folders_file, sizeof s_folders_file, "import/folders.txt")) return 1;
    folders_load();
    pe_menu_begin(PAGE);
    pe_menu_top_page(PAGE, "Costumes from your other PlayStation games: put their disc images in the folder below.", 60);
    pe_menu_register_info(0, "Automatic import", "Costumes built by themselves from the discs in the folder, and the discs still missing.", text_auto);
    pe_menu_register_info(0, "Disc images folder", "Put your disc images here (.cue with its .bin, or .iso), loose or one folder deep.", text_folder);
    pe_menu_register_action(0, "Open the folder", "Opens the disc images folder.", open_folder);
    pe_menu_register_info(0, "Added folders", "More folders disc images are read from, where they already are.", text_folders);
    pe_menu_register_action(0, "Add a folder", "Pick a folder that holds disc images (loose or one game per subfolder); it's read at once and at every start.", add_folder);
    pe_menu_register_action(0, "Forget added folders", "Stop reading the added folders. Costumes already built stay.", forget_folders);
    pe_menu_register_action(0, "Scan again", "Reads new disc images and builds what's missing. Runs by itself when the game starts.", scan_again);
    pe_menu_register_info(0, "Last scan", "What the last scan found.", text_scan);
    pe_menu_page("Build by hand", "Any other model from your scanned discs, built as a costume.", 10);
    pe_menu_register_dynamic_choice("Build by hand", "Model", "A model from your scanned discs.", &s_pick, MAX_ITEMS, item_count, item_name);
    pe_menu_register_info("Build by hand", "From", "Where the selected model comes from.", text_asset);
    pe_menu_register_action("Build by hand", "Build as a costume", "Builds the selected model as a Wardrobe costume, in a pack of your own. For personal use only.", build);
    pe_menu_register_info("Build by hand", "Last build", "What the last build did.", text_build);
    pe_hook_add(PE_HOOK_FRAME, on_frame, NULL, PE_PRIORITY_LATE, MOD_ID);
    /* The game binds its discs before the mods start (main.c), so the first run starts now, on its
     * thread, while the title loads; its results reach the Wardrobe on the first field frame, or
     * at once on this page (the menu stops frames). */
    s_started = 1;
    start_job();
    return 0;
}

static void import_shutdown(void) {
    if (s_thread) SDL_WaitThread(s_thread, NULL), s_thread = NULL;  /* never leave a build half-written */
    if (s_lock) SDL_DestroyMutex(s_lock), s_lock = NULL;
}

static const char *const k_requires[] = {"pe_wardrobe", "pe_src_re2", "pe_src_re3", NULL};
const PeMod pe_mod_pe_import = {MOD_ID, "Import from other games", "0.2", k_requires, import_init, import_shutdown};
