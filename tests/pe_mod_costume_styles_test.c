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
  assert(aya.hpAlive == 2250);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 2250);
  /* Momentum: battle movement is 25% faster until the next action. */
  int factor = 0x10000;
  PeHookPlayerMove move = {&player, &factor, 0, 1};
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x14000);
  factor = 0x10000;
  move.combat = 0; /* field movement is never touched */
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x10000);
  move.combat = 1;
  aya.hpAlive = 8500;
  pe_style_kill(&session, 10, 0, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 9000);
  aya.hpAlive = 0;
  pe_style_kill(&session, 10, 0, 1);
  g_BattleEnemySlots[1].combatant.hpAlive = 0;
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(aya.hpAlive == 0 && !session.pending_kill);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  factor = 0x10000;
  fire(PE_HOOK_PLAYER_MOVE, &move);
  assert(factor == 0x10000); /* burst ends when the next action begins */
  aya.stateFlags = 0x80000;
  ((EnemyCombatant *)target.core)->hpAlive = 10;
  fire(PE_HOOK_ENEMY_HIT, &target);
  ((EnemyCombatant *)target.core)->hpAlive = 0;
  fire(PE_HOOK_ENEMY_HIT_END, &target);
  assert(!session.pending_kill);
  /* Session and mode cannot change mid-encounter. */
  worn = &guard;
  requested = 0;
  settings_tick();
  assert(requested == 1 && saves == 1 && session.style == PE_STYLE_QUICKSTEP);
  fire(PE_HOOK_SCENE_START, NULL);
  assert(!session.active && !session.pending_kill);
  start(&guard);
  assert(guarded());
  assert(loss(100, 1) == 75 && loss(1, 1) == 1);
  g_BattleAtbMarkers[0].sprite.color.bytes.r = 17;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0x60);
  assert(loss(100, 4) == 100);
  aya.hpAlive = 9000;
  assert(!guarded() && loss(100, 1) == 100);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  /* Guard off at a full bar, but the guarded hits above loaded a counter. */
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0xff);
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
  /* Retaliate: guarded losses (75 + 1) were stored; periodic source 4 and the
   * unguarded full-bar hit were not. */
  assert(session.counter_store == 76);
  PeHookHpLoss lethal_q = {&aya, 10, &(int){40}, 1};
  fire(PE_HOOK_HP_LOSS, &lethal_q);
  assert(session.counter_store == 86); /* never more HP than Aya had */
  session.counter_store = 76;
  aya.hpAlive = 9000; /* guard off, counter loaded: orange marker */
  g_BattleAtbMarkers[0].sprite.color.bytes.r = 17;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0xff);
  aya.hpAlive = 0;
  BattleEntity foe = {0};
  foe.core = &g_BattleEnemySlots[0].combatant;
  ((EnemyCombatant *)foe.core)->hpAlive = 100;
  aya.action = &weapon_action;
  weapon.kind = 1;
  fire(PE_HOOK_PLAYER_ACTION, &aya); /* a weapon action that misses */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.counter_store == 76); /* kept for a later hit */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  int counter_dealt = 100;
  PeHookEnemyDamage counter_hit = {&aya, &foe, &counter_dealt, 1};
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 150 && session.counter_store == 26); /* +50% cap */
  counter_dealt = 9;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 14 && session.counter_store == 21); /* ceil 4.5 */
  counter_hit.ordinary = 0;
  counter_dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  assert(counter_dealt == 100); /* PE / instant effects never counter */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.counter_store == 0); /* leftovers lost after a counter */
  session.counter_store = 30;
  aya.action = NULL; /* non-weapon actions neither spend nor clear it */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  counter_hit.ordinary = 1;
  counter_dealt = 100;
  fire(PE_HOOK_ENEMY_DAMAGE, &counter_hit);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(counter_dealt == 100 && session.counter_store == 30);
  session.counter_store = 0;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 0x60); /* guarded */
  aya.hpAlive = 9000;
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattleAtbMarkers[0].sprite.color.bytes.r == 17); /* restored */
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
  assert(pe_style_point_blank(&session, 259, 0, 100));
  assert(!pe_style_point_blank(&session, 260, 0, 100));
  assert(pe_style_point_blank(&session, 459, 0,
                              300)); /* equal surface gap, larger enemy */
  assert(!pe_style_point_blank(&session, 0, 0, 0));
  assert(!pe_style_point_blank(&session, INT_MIN, INT_MIN, 100));
  PeStyleSession capacity;
  pe_style_begin(&capacity, PE_STYLE_STRIKER);
  pe_style_action_begin(&capacity, 1, 1);
  pe_style_target(&capacity, 63, 1);
  pe_style_target(&capacity, 64, 1);
  assert(pe_style_gun_damage(&capacity, 63, 100, 1) == 125);
  assert(pe_style_gun_damage(&capacity, 64, 100, 1) == 100);
  assert(pe_style_gun_damage(&capacity, 0, 100, 1) == 100);
  BattleEntity far = {0}, unknown = {0};
  target = (BattleEntity){0};
  target.core = &g_BattleEnemySlots[0].combatant;
  ((EnemyCombatant *)target.core)->hpAlive = 100;
  target.moveSpeed = 4096;
  target.renderObject.hit_cylinder.radius = 100;
  target.renderObject.hit_cylinder.value0 = 259;
  far = target;
  far.core = &g_BattleEnemySlots[1].combatant;
  far.renderObject.hit_cylinder.value0 = 260;
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
  target.renderObject.hit_cylinder.value0 = 259;
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
  target.renderObject.hit_cylinder.value0 = 260;
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
  assert(session.pending_damage && !session.weave_charges);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 1 && aya.exp_or_acc == 0); /* no PE refund */
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 1); /* one charge per queue */
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
  unsigned recovered = 0;
  aya.peRecoveryStep = 777;
  aya.peRecoveryDivisor = 999;
  for (int i = 0; i < 10; i++) {
    int increment = 1;
    PeHookPeRecovery q = {&aya, 0, 1000, &increment};
    fire(PE_HOOK_PE_RECOVERY, &q);
    recovered += increment;
  }
  assert(recovered == 5 && aya.peRecoveryStep == 777 &&
         aya.peRecoveryDivisor == 999);
  int near_cap = 100;
  PeHookPeRecovery rq = {&aya, 999, 1000, &near_cap};
  fire(PE_HOOK_PE_RECOVERY, &rq);
  assert(near_cap == 1 && session.pe_remainder == 0);
  weapon.kind = 8;
  weapon_action.actionCode.actionId = 8;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 2); /* clubs qualify */
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_CANCEL, &aya);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  PeHookActionMode interrupted = {&player, 14};
  fire(PE_HOOK_ACTION_MODE, &interrupted);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(0, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 2);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 3);
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(1, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 3); /* capped */
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 0x60); /* steady at 3 */
  /* Overchannel: Heal 1 (60 PE) is free and heals 30 + 8. The refund lands
   * before the native code subtracts the full cost. */
  spell_table[0].cost = 60;
  spell_table[7].cost = 50;
  aya.exp_or_acc = 100 << 16;
  aya.curHP = 50;
  PeHookSpell cast = {0, &player};
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 160 << 16 && aya.curHP == 58);
  assert(session.weave_charges == 0);
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 160 << 16 && aya.curHP == 58); /* no charges */
  fire(PE_HOOK_BATTLE_HUD, NULL);
  for (int i = 0; i < 13; i++)
    fire(PE_HOOK_BATTLE_UPDATE, NULL);
  fire(PE_HOOK_BATTLE_HUD, NULL);
  assert(g_BattlePeLabels[0].sprite.color.bytes.r == 19);
  /* One charge on Scan: 34% of its cost after other mods (Rebalance 30). */
  session.weave_charges = 1;
  aya.exp_or_acc = 0;
  cost_override = 30;
  cast.spell = 7;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == (30 << 16) * 34 / 100 && aya.curHP == 58);
  cost_override = -1;
  /* Two charges on Barrier: 67% of a third of max PE. */
  session.weave_charges = 2;
  aya.exp_or_acc = 0;
  cast.spell = 5;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 333 * 67 / 100);
  /* Energy Shot, Liberate and attack powers keep charges untouched. */
  session.weave_charges = 3;
  aya.exp_or_acc = 0;
  cast.spell = 6;
  fire(PE_HOOK_SPELL, &cast);
  cast.spell = 15;
  fire(PE_HOOK_SPELL, &cast);
  cast.spell = 19;
  fire(PE_HOOK_SPELL, &cast);
  assert(aya.exp_or_acc == 0 && session.weave_charges == 3);
  /* Another actor's spell (not Aya acting) is ignored. */
  Combatant other = {0};
  g_ActiveActor = &other;
  cast.spell = 0;
  fire(PE_HOOK_SPELL, &cast);
  assert(session.weave_charges == 3);
  g_ActiveActor = &aya;
  weapon.kind = 1;
  weapon_action.actionCode.actionId = 0;
  session.weave_charges = 0;
  g_BattleEnemySlots[1].active = 0;
  ((EnemyCombatant *)target.core)->hpAlive = 1;
  fire(PE_HOOK_PLAYER_ACTION, &aya);
  weapon_hit(2, 1);
  fire(PE_HOOK_PLAYER_ACTION_END, &aya);
  assert(session.weave_charges == 0); /* final kill suppressed */
  PeStyleSession tiny;
  pe_style_begin(&tiny, PE_STYLE_CHANNELER);
  for (int i = 0; i < 20; i++) {
    pe_style_action_begin(&tiny, 1, 1);
    pe_style_weapon_damage(&tiny, 10, 9, 1);
    pe_style_weave_resolve(&tiny, 1);
  }
  int over = 0;
  assert(tiny.weave_charges == 3 && pe_style_weave_cast(&tiny, &over) == 100 &&
         over && !tiny.weave_charges);
  tiny.weave_charges = 1;
  assert(pe_style_weave_cast(&tiny, &over) == 34 && !over);
  tiny.weave_charges = 2;
  assert(pe_style_weave_cast(&tiny, &over) == 67 && !over);
  fire(PE_HOOK_SAVE_READ, NULL);
  assert(!session.active);
  battle = 0;
  requested = 0;
  settings_tick();
  assert(!pe_styles_enabled());
  puts("Costume styles: registered hooks, attribution, one refund, momentum, "
       "final kill, mode lock, guard boundaries, Retaliate counter, "
       "Spellweave charges and casts, fallback and cleanup passed.");
  return 0;
}
