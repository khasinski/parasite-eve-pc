#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../mods/pe_wardrobe/pe_pack_pe1_costumes.c"
#include "../mods/pe_wardrobe/pe_style_runtime.c"

static unsigned mapped[PE_STYLE_COUNT], registered;
static int collect_costume(const PeCostume *c) {
  mapped[pe_style_for_costume(c->key)]++;
  return (int)registered++;
}
const PeCostumesApi g_pe_costumes_api = {.add = collect_costume};
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
Pe1GameState g_GameState;
GameStateBlock g_GameStateBlock;
Combatant *g_ActiveActor;
BattleEnemySlot g_BattleEnemySlots[7];
int g_BattleModeState;
s8 g_BattleLiveEnemyCount;
u8 g_BattleEnemyEscapeFlag;
u16 g_BattleHitActionPending;
RenderPagedSpritePacket g_BattleAtbMarkers[2];
RenderPagedSpritePacket g_BattlePeLabels[2];
FieldActor *g_FieldActorListHead;
int g_ActiveDrawSlot;
BattleGaugePrim g_BattleStatusPointerSprites[2],
    g_BattleTargetHighlightSprites[2][3];
BattleStatusPointerPrim g_BattleStatusPointerTriangles[2];
BattleHudOrigin g_HudOrigin;
/* Camera and player stand-ins for the Striker point-blank ring. */
GeomScrollRecord g_GeomScroll;
int g_BattleAttackAnimFrame;
FieldActor *g_PlayerEntity;
static unsigned screen_offset_sets, screen_offset_resets;
int Render_SetGteScreenOffset(void) {
  screen_offset_sets++;
  return 0;
}
int Render_ResetGteScreenOffset(void) {
  screen_offset_resets++;
  return 0;
}
/* GPU stand-ins: record what the Rally "+N" submits. */
static unsigned prims_added, tiles_set, sprites_inited;
static const void *last_prim;
void AddPrim(void *ot, void *prim) {
  (void)ot;
  prims_added++;
  last_prim = prim;
}
u_short GetTPage(int tp, int abr, int x, int y) {
  (void)tp;
  (void)abr;
  return (u_short)(x + y);
}
u_short GetClut(int x, int y) { return (u_short)(x ^ y); }
void SetTile(void *tile) {
  (void)tile;
  tiles_set++;
}
void Draw_InitDrawModeSprtPacket(void *packet, int tpage) {
  (void)packet;
  (void)tpage;
  sprites_inited++;
}
static Combatant aya;
static BattleAction weapon_action;
static ItemDataRecord weapon;
static BattleEntity player, target;
static const PeCostume *worn;
static const PeCostume *browsed;
static PeMenuText preview_text, benefit_text, tradeoff_text;
static int battle, ready = 1, half, saves;
static PeHookFn hooks[PE_HOOK_COUNT];
static PeCostume quick = {.key = "pe1/t_shirt"}, guard = {.key = "pe1/guard"},
                 striker = {.key = "pe1/daniel"},
                 channeler = {.key = "pe1/evening_dress"},
                 future = {.key = "other/new"};
ItemDataRecord *pe_equipped(int armor) { return armor ? NULL : &weapon; }
int pe_in_battle(void) { return battle; }
Combatant *pe_aya_core(void) { return &aya; }
BattleEntity *pe_player(void) { return &player; }
const PeCostume *pe_wardrobe_worn(void) { return worn; }
const PeCostume *pe_wardrobe_browsed(void) { return browsed; }
int pe_skin_appearance_ready(const PeCostume *c) {
  (void)c;
  return ready;
}
void pe_toast(const char *s) { (void)s; }
void pe_log(const char *t, const char *f, ...) {
  (void)t;
  (void)f;
}
void pe_menu_save_settings(void) { saves++; }
int pe_menu_register_toggle(const char *p, const char *l, const char *d, int *v,
                            PeMenuPerFrame tick) {
  (void)p;
  (void)l;
  (void)d;
  (void)v;
  (void)tick;
  return 0;
}
int pe_menu_register_info(const char *p, const char *l, const char *d,
                          PeMenuText t) {
  if (!strcmp(l, "Selected style"))
    preview_text = t;
  if (!strcmp(l, "Style benefit"))
    benefit_text = t;
  if (!strcmp(l, "Style tradeoff"))
    tradeoff_text = t;
  (void)p;
  (void)l;
  (void)d;
  (void)t;
  return 0;
}
int pe_hook_add(int id, PeHookFn f, void *u, int priority, const char *m) {
  (void)u;
  (void)priority;
  (void)m;
  assert(!hooks[id]);
  hooks[id] = f;
  return 0;
}
static ParasiteSpellEntry spell_table[20];
void *Aya_GetParasiteSpellUnlockTable(void) { return spell_table; }
static int cost_override = -1;
int pe_hook_dispatch(int id, void *ctx) {
  /* Stands in for another mod (e.g. Rebalance) changing a PE cost. */
  if (id == PE_HOOK_POWER_COST && cost_override >= 0)
    *((PeHookPowerCost *)ctx)->cost = cost_override;
  return 0;
}
static int partial(void) { return half; }
const void *pe_service_get(const char *name, int version) {
  static const PeBattleActionPolicyApi api = {partial};
  (void)version;
  return !strcmp(name, "battle_action_policy") ? &api : NULL;
}
static void fire(int id, void *ctx) {
  assert(hooks[id]);
  hooks[id](ctx, NULL);
}
static void start(const PeCostume *c) {
  battle = 0;
  settings_tick();
  worn = c;
  requested = 1;
  settings_tick();
  memset(&aya, 0, sizeof aya);
  memset(g_BattleEnemySlots, 0, sizeof g_BattleEnemySlots);
  aya.curHP = 100;
  aya.actionMode12 = 4;
  player.actionMode = 4;
  g_ActiveActor = &aya;
  g_GameStateFlags = 2;
  g_BattleModeState = 0;
  g_BattleLiveEnemyCount = 2;
  g_BattleEnemyEscapeFlag = 1;
  g_BattleHitActionPending = 0;
  g_BattleEnemySlots[1].active = 1;
  g_BattleEnemySlots[1].combatant.hpAlive = 100;
  battle = 1;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
}
static int loss(int damage, unsigned source) {
  PeHookHpLoss q = {&aya, 100, &damage, source};
  fire(PE_HOOK_HP_LOSS, &q);
  return damage;
}
static void weapon_hit(int amount, int ordinary) {
  EnemyCombatant *enemy = target.core;
  fire(PE_HOOK_ENEMY_HIT, &target);
  PeHookEnemyDamage q = {&aya, &target, &amount, ordinary};
  fire(PE_HOOK_ENEMY_DAMAGE, &q);
  enemy->hpAlive -= amount;
  fire(PE_HOOK_ENEMY_HIT_END, &target);
}
int main(void) {
  char label[64];
  assert(pe_styles_set_enabled(1) && pe_styles_enabled());
  pe_styles_describe(quick.key, label, sizeof label);
  assert(!strcmp(label, "Quickstep (on)"));
  assert(pe_styles_set_enabled(0) && !pe_styles_enabled());
  pe_styles_describe(quick.key, label, sizeof label);
  assert(!strcmp(label, "Quickstep (off)"));
  pe_styles_describe(future.key, label, sizeof label);
  assert(!strcmp(label, "Original (native)"));
  half = 1;
  assert(pe_styles_set_enabled(1));
  pe_styles_describe(guard.key, label, sizeof label);
  assert(!strcmp(label, "Bulwark (blocked)"));
  half = 0;
  battle = 1;
  int saved_count = saves;
  assert(!pe_styles_set_enabled(0) && pe_styles_enabled() &&
         saves == saved_count);
  battle = 0;
  assert(pe_styles_set_enabled(0));
  saves = 0;
  pe_pack_pe1_costumes_register();
  assert(registered == 40 && mapped[PE_STYLE_ORIGINAL] == 1);
  assert(mapped[PE_STYLE_QUICKSTEP] == 10 && mapped[PE_STYLE_BULWARK] == 8);
  assert(mapped[PE_STYLE_STRIKER] == 10 && mapped[PE_STYLE_CHANNELER] == 11);
  pe_styles_register("test");
  assert(!pe_styles_enabled());
  assert(pe_style_for_costume("pe1/opera_doublet") == PE_STYLE_QUICKSTEP);
  assert(pe_style_for_costume("pe1/opera_knight") == PE_STYLE_BULWARK);
  assert(pe_style_for_costume("pe1/opera_actress") == PE_STYLE_ORIGINAL);
  assert(pe_style_for_costume("other/guard") == PE_STYLE_ORIGINAL);
  start(&quick);
  assert(session.style == PE_STYLE_QUICKSTEP);
  browsed = &guard;
  preview_text(label, sizeof label);
  assert(!strcmp(label, "Bulwark (on)") && worn == &quick &&
         session.style == PE_STYLE_QUICKSTEP);
  benefit_text(label, sizeof label);
  assert(!strcmp(label, pe_style_definition(PE_STYLE_BULWARK)->benefit));
  tradeoff_text(label, sizeof label);
  assert(!strcmp(label, pe_style_definition(PE_STYLE_BULWARK)->limitation));
  browsed = &future;
  benefit_text(label, sizeof label);
  assert(!strcmp(label, "Native combat"));
  assert(worn == &quick && session.style == PE_STYLE_QUICKSTEP);
  target.core = &g_BattleEnemySlots[0].combatant;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  ((EnemyCombatant *)target.core)->hpAlive = 20;
  fire(PE_HOOK_ENEMY_HIT, &target);
  ((EnemyCombatant *)target.core)->hpAlive = -1;
  fire(PE_HOOK_ENEMY_HIT_END, &target);
  /* Duplicate hit notifications on an already-dead target do not multiply. */
  fire(PE_HOOK_ENEMY_HIT, &target);
  fire(PE_HOOK_ENEMY_HIT_END, &target);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 1800); /* 20% of a full bar */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 1800);
  /* Momentum: battle movement is 25% faster during the burst. */
  int factor = 0x10000;
  PeHookPlayerMove move = {&player, &factor, 0, 1};
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x14000);
  factor = 0x10000;
  move.combat = 0; /* field movement is never touched */
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x10000);
  move.combat = 1;
  int kill_key;
  aya.hpAlive = 8500;
  pe_quickstep_hit(&session, &kill_key, 10, 0, 0);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 9000); /* clamped at a full bar */
  aya.hpAlive = 0;
  pe_quickstep_hit(&session, &kill_key, 10, 0, 0);
  g_BattleEnemySlots[1].combatant.hpAlive = 0;
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 0 && !session.qs.pending); /* encounter-ending */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  factor = 0x10000;
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x10000); /* burst ends when the next action begins */
  assert(loss(100, 1) == 100); /* and Overextended with it */
  aya.stateFlags = 0x80000;
  ((EnemyCombatant *)target.core)->hpAlive = 10;
  fire(PE_HOOK_ENEMY_HIT, &target);
  ((EnemyCombatant *)target.core)->hpAlive = 0;
  fire(PE_HOOK_ENEMY_HIT_END, &target);
  assert(!session.qs.pending); /* PE kills never refund */
  aya.stateFlags = 0;

  /* ---- Quickstep half-HP breakpoint, burst, Overextended and HP pip ---- */
  g_BattleEnemySlots[1].combatant.hpAlive = 100; /* another enemy remains */
  BattleEntity qa = {0}, qb = {0};
  EnemyCombatant *fa = &g_BattleEnemySlots[2].combatant,
                 *fb = &g_BattleEnemySlots[3].combatant;
  qa.core = fa;
  qb.core = fb;
  fa->hpAlive = fa->maxHp = 100;
  fb->hpAlive = fb->maxHp = 100;
#define QS_HIT(e, hp)                                                          \
  do {                                                                         \
    fire(PE_HOOK_ENEMY_HIT, &(e));                                             \
    ((EnemyCombatant *)(e).core)->hpAlive = (hp);                              \
    fire(PE_HOOK_ENEMY_HIT_END, &(e));                                         \
  } while (0)
  aya.hpAlive = 0;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  QS_HIT(qa, 60);
  assert(!session.qs.pending); /* above half: nothing yet */
  QS_HIT(qa, 50);
  assert(session.qs.pending); /* exactly half counts as crossed */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 1800 && session.qs.burst == 90);
  /* Healing never re-arms a crossed breakpoint. */
  aya.hpAlive = 0;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  fa->hpAlive = 90;
  QS_HIT(qa, 40);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 0 && !session.qs.burst);
  /* Several breakpoints and a kill in one action: still one refund. */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  QS_HIT(qb, 30);
  QS_HIT(qa, 0);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 1800);
  /* A boss phase that rewrites max HP gets a fresh breakpoint. */
  aya.hpAlive = 0;
  fb->maxHp = 200;
  fb->hpAlive = 200;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  QS_HIT(qb, 90);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 1800);
  /* A miss or a non-damaging hit earns nothing. */
  aya.hpAlive = 0;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  QS_HIT(qb, 90);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 0);
  /* Burst: 90 controllable frames; hit reactions and menus don't count. */
  aya.hpAlive = 0;
  fb->maxHp = 300;
  fb->hpAlive = 300;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  QS_HIT(qb, 100);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.qs.burst == 90);
  assert(loss(100, 1) == 120 && loss(1, 1) == 1); /* Overextended x1.20 */
  assert(loss(100, 4) == 100); /* status ticks are not eligible */
  g_BattleHitActionPending = 1;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.qs.burst == 90); /* frozen while not controllable */
  g_BattleHitActionPending = 0;
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  g_BattleAtbMarkers[0].sprite.color.bytes.r = 17;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0xff &&
         g_BattleAtbMarkers[0].sprite.color.bytes.g == 0x50); /* rush cue */
  for (int i = 0; i < 76; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(pe_quickstep_burst(&session) && session.qs.burst == 1);
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(!pe_quickstep_burst(&session)); /* 3 s of controllable time */
  assert(loss(100, 1) == 100);
  factor = 0x10000;
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x10000);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 17); /* restored */
  /* HP pip: the target marker darkens once the enemy is at half or below. */
  BattleEntity fresh = {0};
  EnemyCombatant *ff = &g_BattleEnemySlots[4].combatant;
  fresh.core = ff;
  ff->maxHp = 100;
  ff->hpAlive = 60;
  assert(!pe_quickstep_spent(&session, ff, ff->hpAlive, ff->maxHp));
  assert(pe_quickstep_spent(&session, fb, fb->hpAlive, fb->maxHp));
  ff->hpAlive = 50;
  assert(pe_quickstep_spent(&session, ff, ff->hpAlive, ff->maxHp));
  ff->hpAlive = 60;
  fresh.next = &qb;
  g_FieldActorListHead = (FieldActor *)&fresh;
  g_ActiveDrawSlot = 0;
  g_BattleStatusPointerSprites[0].sprite.color.bytes.r = 17;
  PeHookTargetPreview pip = {&fresh.renderObject, 0, 1, 1};
  fire(PE_HOOK_TARGET_PREVIEW, &pip); /* fresh enemy: no pip, even far */
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  pip.object = &qb.renderObject;
  fire(PE_HOOK_TARGET_PREVIEW, &pip);
  fire(PE_HOOK_TARGET_PREVIEW, &pip); /* twice a frame keeps the native */
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 0x30);
  pip.phase = 0;
  fire(PE_HOOK_TARGET_PREVIEW, &pip);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  g_FieldActorListHead = NULL;
#undef QS_HIT
  memset(&g_BattleEnemySlots[2], 0, sizeof g_BattleEnemySlots[0] * 3);
  /* Session and mode cannot change mid-encounter. */
  worn = &guard;
  requested = 0;
  settings_tick();
  assert(requested == 1 && saves == 1 && session.style == PE_STYLE_QUICKSTEP);
  fire(PE_HOOK_SCENE_START, NULL);
  assert(!session.active && !session.qs.pending && !session.qs.burst &&
         !session.qs.marks);
  start(&guard);
  aya.maxHP = 200;
  assert(guarded());
  assert(loss(100, 1) == 75 && loss(1, 1) == 1);
  g_BattleAtbMarkers[0].sprite.color.bytes.r = 17;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0x60);
  assert(loss(100, 4) == 100);
  assert(session.bw.store == 76 && session.bw.pool == 76);
  aya.hpAlive = 9000;
  assert(!guarded() && bulwark_phase() == PE_BW_EXPOSED);
  assert(loss(100, 1) == 115); /* Exposed: full bar, free to move */
  assert(session.bw.store == 76 && session.bw.pool == 76); /* feeds nothing */
  /* Exposed with a loaded counter: red, alternating with orange. */
  frame_count = 0;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0xff &&
         g_BattleAtbMarkers[0].sprite.color.bytes.g == 0x30);
  frame_count = EXPOSED_ALTERNATE_FRAMES;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.g == 0x90);
  /* Hit reactions, actions and forced control: neither guarded nor exposed. */
  g_BattleHitActionPending = 1;
  assert(bulwark_phase() == PE_BW_OFF && loss(100, 1) == 100);
  g_BattleHitActionPending = 0;
  aya.hpAlive = 0;
  player.actionMode = 0;
  assert(guarded()); /* normal movement, not only the idle pose */
  player.actionMode = 0xE;
  assert(!guarded());
  player.actionMode = 4;
  g_BattleHitActionPending = 1;
  assert(!guarded());
  g_BattleHitActionPending = 0;
  assert(guarded());
  unsigned sum = 0;
  for (int i = 0; i < 10; i++) {
    int inc = 1;
    PeHookAtCharge q = {&aya, &inc};
    fire(PE_HOOK_AT_CHARGE, &q);
    sum += inc;
  }
  assert(sum == 10); /* Revision 5: native charge speed */
  int inc = 0;
  PeHookAtCharge q = {&aya, &inc};
  fire(PE_HOOK_AT_CHARGE, &q);
  assert(inc == 0);
  assert(aya.scaledOffense == 0); /* derived stats never rewritten */
  /* Counter store and Rally pool: guarded losses (75 + 1); periodic source 4,
   * the Exposed hit and the hit-reaction hit added nothing. */
  assert(session.bw.store == 76 && session.bw.pool == 76);
  PeHookHpLoss lethal_q = {&aya, 10, &(int){40}, 1};
  fire(PE_HOOK_HP_LOSS, &lethal_q);
  assert(session.bw.store == 86 &&
         session.bw.pool == 86); /* never more HP than Aya had */
  session.bw.store = 76;
  session.bw.pool = 76;
  assert(pe_bulwark_rally_left(&session) == 38); /* half the pool */
  /* "+N" beside the HP row: "+", then the digits, in the native strip. */
  prims_added = 0;
  g_HudOrigin.x = 16;
  g_HudOrigin.y = 180;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(sprites_inited == 2 * RALLY_DIGITS && tiles_set == 4);
  assert(prims_added == 4 && last_prim == &rally_digits[0][0]);
  assert(rally_plus[0][0].x0 == 16 + RALLY_X &&
         rally_digits[0][1].sprite.u == 3 * 8 &&
         rally_digits[0][0].sprite.u == 8 * 8 &&
         rally_digits[0][0].sprite.y == 180 + RALLY_Y &&
         rally_digits[0][0].sprite.color.bytes.g == 0x90);
  BattleEntity foe = {0};
  foe.core = &g_BattleEnemySlots[0].combatant;
  ((EnemyCombatant *)foe.core)->hpAlive = 100;
  aya.action = &weapon_action;
  weapon.kind = 1;
  fire(PE_HOOK_PLAYER_ACTION, &aya); /* a weapon action that misses */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.bw.store == 76 && session.bw.pool == 76); /* both kept */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  int counter_dealt = 100;
  PeHookEnemyDamage counter_hit = {&aya, &foe, &counter_dealt, 1};
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 150 && session.bw.store == 26); /* +50% cap */
  /* Rally: the 50 bonus wins back HP up to half the pool (38). */
  assert(aya.curHP == 138 && pe_bulwark_rally_left(&session) == 0);
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.bw.pool == 76); /* our own gain is not a heal */
  counter_dealt = 9;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 14 && session.bw.store == 21); /* ceil 4.5 */
  assert(aya.curHP == 138); /* Rally already at its cap */
  counter_hit.ordinary = 0;
  counter_dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 100); /* PE / instant effects never counter */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.bw.store == 0 && session.bw.pool == 0 &&
         !session.bw.regained); /* leftovers lost after a counter */
  session.bw.store = 30;
  aya.action = NULL; /* non-weapon actions neither spend nor clear it */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  counter_hit.ordinary = 1;
  counter_dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(counter_dealt == 100 && session.bw.store == 30);
  /* Rally never raises HP above max. */
  aya.curHP = 195;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  session.bw.pool = 100;
  aya.action = &weapon_action;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  counter_dealt = 20;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 30 && aya.curHP == 200 && session.bw.store == 20);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  /* A heal empties the pool but keeps the store. */
  session.bw.store = 40;
  session.bw.pool = 40;
  aya.curHP = 150;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.bw.pool == 40 && pe_bulwark_rally_left(&session) == 20);
  aya.curHP = 180; /* an item or PE heal */
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.bw.pool == 0 && session.bw.store == 40 &&
         !pe_bulwark_rally_left(&session));
  prims_added = 0;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(prims_added == 0); /* no "+N" with nothing to win back */
  session.bw.store = 0;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0x60); /* guarded */
  aya.hpAlive = 9000;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.g == 0x30); /* Exposed */
  g_BattleHitActionPending = 1; /* neither guarded nor exposed */
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 17); /* restored */
  g_BattleHitActionPending = 0;
  aya.hpAlive = 0;
  half = 1;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.style == PE_STYLE_ORIGINAL && !guarded());
  half = 0;
  fire(PE_HOOK_BATTLE_UPDATE, NULL);
  assert(session.style ==
         PE_STYLE_ORIGINAL); /* no reactivation until next encounter */
  half = 1;
  start(&guard);
  assert(session.style == PE_STYLE_ORIGINAL && !guarded());
  half = 0;
  ready = 0;
  start(&quick);
  assert(session.style == PE_STYLE_ORIGINAL);
  ready = 1;
  start(&future);
  assert(session.style == PE_STYLE_ORIGINAL);
  start(&striker);
  assert(session.style == PE_STYLE_STRIKER);
  /* Edge of point-blank for a radius-100 enemy: its radius + Aya's 63 + the
   * tuned gap. Follows the catalog value. */
#define PB_EDGE (163 + (int)pe_style_definition(PE_STYLE_STRIKER)->striker.point_blank_gap)
  assert(pe_striker_point_blank(&session, PB_EDGE, 0, 100));
  assert(!pe_striker_point_blank(&session, PB_EDGE + 1, 0, 100));
  assert(pe_striker_point_blank(&session, PB_EDGE + 200, 0,
                                300)); /* equal surface gap, larger enemy */
  assert(!pe_striker_point_blank(&session, 0, 0, 0));
  assert(!pe_striker_point_blank(&session, INT_MIN, INT_MIN, 100));
  /* Zones: falloff from 70% of native range, beyond range included. */
  assert(pe_striker_zone(&session, 0, 699, 0, 1000) == PE_ST_NATIVE);
  assert(pe_striker_zone(&session, 0, 700, 0, 1000) == PE_ST_FALLOFF);
  assert(pe_striker_zone(&session, 0, 420, 560, 1000) == PE_ST_FALLOFF);
  assert(pe_striker_zone(&session, 0, 1700, 0, 1000) == PE_ST_FALLOFF);
  assert(pe_striker_zone(&session, 1, 900, 0, 1000) ==
         PE_ST_POINT_BLANK); /* point-blank wins on short guns */
  assert(pe_striker_zone(&session, 0, 5000, 0, 0) ==
         PE_ST_NATIVE); /* unknown range: no falloff, never a guess */
  assert(pe_striker_zone(&session, 0, INT_MIN, INT_MIN, 1000) ==
         PE_ST_FALLOFF);
  assert(pe_striker_percent(&session, PE_ST_POINT_BLANK) == 125 &&
         pe_striker_percent(&session, PE_ST_FALLOFF) == 80 &&
         pe_striker_percent(&session, PE_ST_NATIVE) == 100);
  PeStyleSession capacity;
  pe_style_begin(&capacity, PE_STYLE_STRIKER);
  pe_style_action_begin(&capacity, 1, 1);
  pe_striker_target(&capacity, 63, PE_ST_POINT_BLANK);
  pe_striker_target(&capacity, 64, PE_ST_POINT_BLANK);
  pe_striker_target(&capacity, 1, PE_ST_FALLOFF);
  pe_striker_target(&capacity, 2, 7); /* not a zone: ignored */
  assert(pe_striker_damage(&capacity, 63, 100, 1) == 125);
  assert(pe_striker_damage(&capacity, 64, 100, 1) == 100);
  assert(pe_striker_damage(&capacity, 0, 100, 1) == 100);
  assert(pe_striker_damage(&capacity, 1, 100, 1) == 80);
  assert(pe_striker_damage(&capacity, 1, 1, 1) == 1); /* never zero */
  assert(pe_striker_damage(&capacity, 1, 100, 0) == 100);
  assert(pe_striker_damage(&capacity, 2, 100, 1) == 100);
  pe_style_action_begin(&capacity, 0, 1); /* club: no bonus, no falloff */
  pe_striker_target(&capacity, 1, PE_ST_FALLOFF);
  assert(pe_striker_damage(&capacity, 1, 100, 1) == 100);
  pe_style_begin(&capacity, PE_STYLE_QUICKSTEP);
  assert(pe_striker_zone(&capacity, 1, 0, 0, 1000) == PE_ST_NATIVE);
  BattleEntity far = {0}, unknown = {0};
  target = (BattleEntity){0};
  target.core = &g_BattleEnemySlots[0].combatant;
  ((EnemyCombatant *)target.core)->hpAlive = 100;
  target.moveSpeed = 4096;
  target.renderObject.hit_cylinder.radius = 100;
  target.renderObject.hit_cylinder.value0 = PB_EDGE;
  far = target;
  far.core = &g_BattleEnemySlots[1].combatant;
  far.renderObject.hit_cylinder.value0 = PB_EDGE + 1;
  target.next = &far;
  g_FieldActorListHead = (FieldActor *)&target;
  aya.action = &weapon_action;
  weapon.kind = 1;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  int dealt = 100;
  PeHookEnemyDamage gun_hit = {&aya, &target, &dealt, 1};
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 125);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 125); /* each ordinary hit */
  target.renderObject.hit_cylinder.value0 = 900;
  far.renderObject.hit_cylinder.value0 = 0;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 125); /* snapshot stays close */
  gun_hit.target = &far;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100); /* far secondary never borrows selected target */
  unknown = target;
  gun_hit.target = &unknown;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  gun_hit.target = &target;
  gun_hit.source = &g_BattleEnemySlots[1].combatant;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  gun_hit.source = &aya;
  void *original_core = target.core;
  target.core = far.core;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  target.core = original_core;
  gun_hit.ordinary = 0;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  gun_hit.ordinary = 1;
  aya.stateFlags |= 0x80000;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  aya.stateFlags &= ~0x180000u;
  weapon.kind = 8;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon.kind = 1;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100); /* club snapshot cannot become gun */
  target.renderObject.hit_cylinder.value0 = PB_EDGE;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = INT_MAX;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == INT_MAX);
  dealt = 2;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 3); /* nearest integer, ties upward */
  dealt = 0;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 0);
  PeHookTargetPreview cue = {&target.renderObject, 0, 0, 1};
  g_BattleStatusPointerSprites[0].sprite.color.bytes.r = 17;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 0x60);
  target.renderObject.hit_cylinder.value0 = PB_EDGE + 1;
  cue.phase = 0;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  cue.phase = 1;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  target.renderObject.hit_cylinder.value0 = 0;
  cue.out_of_range = 1;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(!target_tints[0][0].tinted);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  /* Falloff: native targeting distance against the gun's native range. */
  weapon_action.range = 1000;
  target.renderObject.hit_cylinder.value0 = 2000; /* not point-blank */
  target.renderObject.target_x = 700;
  far.renderObject.hit_cylinder.value0 = 2000;
  far.renderObject.target_x = 699;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  gun_hit.target = &target;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 80);
  gun_hit.target = &far;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100); /* each recipient judged on its own snapshot */
  gun_hit.target = &target;
  target.renderObject.target_x = 0;
  target.renderObject.hit_cylinder.value0 = 0;
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 80); /* moving closer after commit changes nothing */
  /* Preview uses the same zones: cyan, untouched, dim. */
  g_BattleStatusPointerSprites[0].sprite.color.bytes.r = 17;
  cue.out_of_range = 0;
  cue.phase = 1;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 0x60);
  cue.phase = 0;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  target.renderObject.hit_cylinder.value0 = 2000;
  target.renderObject.target_x = 500;
  cue.phase = 1;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  target.renderObject.target_x = 800;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 0x5a &&
         g_BattleStatusPointerTriangles[0].r == 0x5a);
  cue.phase = 0;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  cue.phase = 1;
  cue.out_of_range = 1; /* native dark marker already says "far" */
  target.renderObject.target_x = 1500;
  fire(PE_HOOK_TARGET_PREVIEW, &cue);
  assert(g_BattleStatusPointerSprites[0].sprite.color.bytes.r == 17);
  cue.out_of_range = 0;
  /* Point-blank ring while aiming a gun: drawn with the native dome, at
   * Aya's footprint + the point-blank gap, growing as the dome opens. */
  {
    static GteMatrix view = {{{4096, 0, 0}, {0, 4096, 0}, {0, 0, 4096}},
                             {0, 0, 2000}};
    static FieldActor me;
    static unsigned char ot_storage[2][64];
    g_ViewMatrixSlot.value = &view;
    g_PlayerEntity = &me;
    g_OtBufferTable[0] = ot_storage[0];
    g_OtBufferTable[1] = ot_storage[1];
    assert(pe_striker_ring_radius(&session) == (unsigned)PB_EDGE - 100);
    g_BattleAttackAnimFrame = DOME_GROW_FRAMES;
    unsigned sets = screen_offset_sets;
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    assert(ring_drawn == RING_SEGMENTS && screen_offset_sets == sets + 1 &&
           screen_offset_resets == screen_offset_sets);
    assert(ring_lines[0][0].code == 0x50 && ring_lines[0][0].r0 == 0x60 &&
           ring_lines[0][0].g1 == 0xe0 && ring_lines[0][0].tag.length == 4);
    g_BattleAttackAnimFrame = 0; /* dome not open yet: no ring */
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    assert(ring_drawn == 0);
    g_BattleAttackAnimFrame = DOME_GROW_FRAMES;
    weapon.kind = 8; /* a club: no ring */
    ring_drawn = 7;
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    assert(ring_drawn == 7);
    weapon.kind = 1;
    cue.mode = 4; /* not gun targeting */
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    assert(ring_drawn == 7);
    cue.mode = 0;
    g_ViewMatrixSlot.value = NULL; /* no camera yet: nothing drawn */
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    assert(ring_drawn == 0);
    PeStyleSession other;
    pe_style_begin(&other, PE_STYLE_QUICKSTEP);
    assert(pe_striker_ring_radius(&other) == 0);
    g_PlayerEntity = NULL;
    g_OtBufferTable[0] = g_OtBufferTable[1] = NULL;
    cue.phase = 0;
    fire(PE_HOOK_TARGET_PREVIEW, &cue);
    cue.phase = 1;
  }
  /* Beyond native range a hit still lands at falloff. */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 80);
  /* Darkness: the native 200-unit range. */
  aya.stateFlags |= AYA_DARKNESS;
  target.renderObject.target_x = 140;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 80);
  target.renderObject.target_x = 139;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  aya.stateFlags &= ~(unsigned)AYA_DARKNESS_MASK;
  /* Unknown range: no falloff. Clubs: none either. */
  weapon_action.range = 0;
  target.renderObject.target_x = 5000;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  weapon_action.range = 1000;
  weapon.kind = 8;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100);
  weapon.kind = 1;
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &gun_hit);
  assert(dealt == 100); /* snapshot cleared with the action */
  weapon_action.range = 0;
  target.renderObject.target_x = 0;
  far.renderObject.target_x = 0;
  start(&channeler);
  aya.action = &weapon_action;
  weapon.kind = 1;
  aya.maxAtk = 1000;
  aya.exp_or_acc = 0;
  target.core = &g_BattleEnemySlots[0].combatant;
  ((EnemyCombatant *)target.core)->hpAlive = 100;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(10, 1);
  weapon_hit(10, 1);
  weapon_hit(10, 1);
  assert(session.pending_damage && !session.ch.charges);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 1 && aya.exp_or_acc == 0); /* no PE refund */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 1); /* one charge per queue */
  g_BattlePeLabels[0].sprite.color.bytes.r = 19;
  g_BattlePeLabels[0].sprite.color.bytes.code = 0x65;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 0x60);
  assert(g_BattlePeLabels[0].sprite.color.bytes.code == 0x64);
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 19);
  assert(g_BattlePeLabels[0].sprite.color.bytes.code == 0x65);
  /* Passive PE regeneration is native: the hook leaves it untouched. */
  unsigned recovered = 0;
  aya.peRecoveryStep = 777;
  aya.peRecoveryDivisor = 999;
  for (int i = 0; i < 10; i++) {
    int increment = 1;
    PeHookPeRecovery q = {&aya, 0, 1000, &increment};
    fire(PE_HOOK_PE_RECOVERY, &q);
    recovered += increment;
  }
  assert(recovered == 10 && aya.peRecoveryStep == 777 &&
         aya.peRecoveryDivisor == 999);
  int near_cap = 100;
  PeHookPeRecovery rq = {&aya, 999, 1000, &near_cap};
  fire(PE_HOOK_PE_RECOVERY, &rq);
  assert(near_cap == 100);
  /* Menu cost and affordability: one charge shows half price. */
  int menu_cost = 30;
  PeHookPowerCost menu = {7, &menu_cost};
  fire(PE_HOOK_POWER_COST, &menu);
  assert(menu_cost == 15);
  menu_cost = 45;
  fire(PE_HOOK_POWER_COST, &menu);
  assert(menu_cost == 23); /* never shows less than is paid */
  weapon.kind = 8;
  weapon_action.actionCode.actionId = 8;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 2); /* clubs qualify */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_CANCEL, &aya);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  PeHookActionMode interrupted = {&player, 14};
  fire(PE_HOOK_ACTION_MODE, &interrupted);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(0, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 2); /* capped at 2 */
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 0x60); /* steady at 2 */
  /* Two charges: support powers show free; attack powers, Energy Shot and
   * Liberate keep their cost. */
  menu_cost = 60;
  menu.power = 0;
  fire(PE_HOOK_POWER_COST, &menu);
  assert(menu_cost == 0);
  int attack_cost = 60;
  PeHookPowerCost attack = {15, &attack_cost};
  fire(PE_HOOK_POWER_COST, &attack);
  attack.power = 6;
  fire(PE_HOOK_POWER_COST, &attack);
  assert(attack_cost == 60);
  /* Backlash, warning first: the first hit cracks, nothing is lost. */
  int pe_before = aya.exp_or_acc;
  assert(loss(10, 1) == 10);
  assert(session.ch.charges == 2 && session.ch.cracked);
  frame_count = 0;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 0xff &&
         g_BattlePeLabels[0].sprite.color.bytes.g == 0x90); /* crack tint */
  frame_count = CRACK_FLICKER_FRAMES;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 19); /* flicker off */
  /* More hits from the same enemy attack count as that one hit. */
  loss(10, 2);
  loss(10, 3);
  assert(session.ch.charges == 2 && session.ch.cracked);
  loss(10, 4); /* status ticks are not hits */
  for (int i = 0; i < 30; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  loss(10, 1); /* a new attack: breaks one charge */
  assert(session.ch.charges == 1 && !session.ch.cracked);
  assert(aya.exp_or_acc == pe_before); /* never any PE */
  for (int i = 0; i < 30; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  loss(10, 1);
  assert(session.ch.charges == 1 && !session.ch.cracked); /* not full */
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 19); /* one-charge look */
  /* Recharge, crack, then cast: Overchannel and the crack clears. */
  weapon.kind = 1;
  weapon_action.actionCode.actionId = 0;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  for (int i = 0; i < 30; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  loss(10, 1);
  assert(session.ch.charges == 2 && session.ch.cracked);
  /* Overchannel: Heal 1 (60 PE) is free and heals 30 + 8. The refund lands
   * before the native code subtracts the full cost. */
  spell_table[0].cost = 60;
  spell_table[7].cost = 50;
  aya.exp_or_acc = 100 << 16;
  aya.curHP = 50;
  PeHookSpell cast = {0, &player};
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 160 << 16 && aya.curHP == 58);
  assert(session.ch.charges == 0 && !session.ch.cracked);
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 160 << 16 && aya.curHP == 58); /* no charges */
  menu_cost = 60;
  fire(PE_HOOK_POWER_COST, &menu);
  assert(menu_cost == 60); /* full price again */
  fire(PE_HOOK_BATTLE_HUD, NULL);
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 19);
  /* One charge on Scan: half of its cost after other mods (Rebalance 30). */
  session.ch.charges = 1;
  aya.exp_or_acc = 0;
  cost_override = 30;
  cast.spell = 7;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == (30 << 16) * 50 / 100 && aya.curHP == 58);
  cost_override = -1;
  /* Two charges on Barrier: free (a third of max PE refunded). */
  session.ch.charges = 2;
  aya.exp_or_acc = 0;
  cast.spell = 5;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 333);
  /* Energy Shot, Liberate and attack powers keep charges untouched. */
  session.ch.charges = 2;
  aya.exp_or_acc = 0;
  cast.spell = 6;
  fire(PE_HOOK_SPELL, &cast);
  cast.spell = 15;
  fire(PE_HOOK_SPELL, &cast);
  cast.spell = 19;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 0 && session.ch.charges == 2);
  /* Another actor's spell (not Aya acting) is ignored. */
  Combatant other = {0};
  g_ActiveActor = &other;
  cast.spell = 0;
  fire(PE_HOOK_SPELL, &cast);
  assert(session.ch.charges == 2);
  g_ActiveActor = &aya;
  session.ch.charges = 0;
  g_BattleEnemySlots[1].active = 0;
  ((EnemyCombatant *)target.core)->hpAlive = 1;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(2, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.ch.charges == 0); /* final kill suppressed */
  PeStyleSession tiny;
  pe_style_begin(&tiny, PE_STYLE_CHANNELER);
  for (int i = 0; i < 20; i++) {
    pe_style_action_begin(&tiny, 1, 1);
    pe_style_weapon_damage(&tiny, 10, 9, 1);
    pe_channeler_resolve(&tiny, 1);
  }
  int over = 0;
  assert(tiny.ch.charges == 2 && pe_channeler_cast(&tiny, &over) == 100 &&
         over && !tiny.ch.charges);
  tiny.ch.charges = 1;
  assert(pe_channeler_cast(&tiny, &over) == 50 && !over);
  session.ch.charges = 2;
  fire(PE_HOOK_SAVE_READ, NULL);
  assert(!session.active && !session.ch.charges && !session.ch.cracked);
  menu_cost = 60;
  fire(PE_HOOK_POWER_COST, &menu);
  assert(menu_cost == 60); /* no discount once the encounter is over */
  battle = 0;
  requested = 0;
  settings_tick();
  assert(!pe_styles_enabled());
  puts("Costume styles: registered hooks, attribution, one refund, momentum, "
       "final kill, mode lock, guard boundaries, Retaliate counter, "
       "Spellweave charges and casts, fallback and cleanup passed.");
  return 0;
}
