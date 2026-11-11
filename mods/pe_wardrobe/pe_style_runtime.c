/* Native adapter. Definitions and simulation have no dependency on game state.
 */
#include "pe1/battle_reset_stats.h"
#include "pe1/battle_status.h"
#include "pe1/draw_buffers.h"
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_mod_menu.h"
#include "pe_pages.h"
#include "pe_style.h"
#include "pe_wardrobe.h"
#include <stdio.h>
#define FULL_AT 9000u
static PeStyleSession session;
static int requested, enabled, locked;
static int hit_hp;
static const void *hit_entity;
static int hit_ordinary;
static unsigned pulse;
static unsigned pe_pulse;
static struct {
  const BattleEntity *entity;
  const void *core;
} action_targets[PE_STYLE_TARGETS];
static unsigned action_target_count;
typedef struct {
  u8 r, g, b;
  int tinted;
} TargetTint;
static TargetTint target_tints[2][4], triangle_tints[2];
static TargetTint pe_tints[2];
static u8 pe_codes[2];
static void restore_rgb(u8 *r, u8 *g, u8 *b, TargetTint *t) {
  if (t->tinted && *r == 0x60 && *g == 0xe0 && *b == 0xff) {
    *r = t->r;
    *g = t->g;
    *b = t->b;
  }
  t->tinted = 0;
}
static void tint_rgb(u8 *r, u8 *g, u8 *b, TargetTint *t) {
  t->r = *r;
  t->g = *g;
  t->b = *b;
  t->tinted = 1;
  *r = 0x60;
  *g = 0xe0;
  *b = 0xff;
}
static RenderSpritePacket *target_packet(unsigned slot, unsigned index) {
  return index ? &g_BattleTargetHighlightSprites[slot][index - 1].sprite
               : &g_BattleStatusPointerSprites[slot].sprite;
}
static void restore_target_cue(void) {
  for (unsigned slot = 0; slot < 2; slot++) {
    for (unsigned i = 0; i < 4; i++) {
      RenderSpritePacket *p = target_packet(slot, i);
      restore_rgb(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
                  &target_tints[slot][i]);
    }
    BattleStatusPointerPrim *p = &g_BattleStatusPointerTriangles[slot];
    restore_rgb(&p->r, &p->g, &p->b, &triangle_tints[slot]);
  }
}
static void restore_pe_cue(void) {
  for (unsigned i = 0; i < 2; i++) {
    RenderSpritePacket *p = &g_BattlePeLabels[i].sprite;
    if (pe_tints[i].tinted && p->color.bytes.r == 0x60 &&
        p->color.bytes.g == 0xe0 && p->color.bytes.b == 0xff &&
        p->color.bytes.code == (u8)(pe_codes[i] & ~1u))
      p->color.bytes.code = pe_codes[i];
    restore_rgb(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
                &pe_tints[i]);
  }
}
static struct {
  u8 r, g, b;
  u8 tr, tg, tb; /* the cue color applied this frame */
  int tinted;
} marker[2];

static void restore_marker(void) {
  unsigned i;
  for (i = 0; i < 2; i++) {
    RenderSpritePacket *p = &g_BattleAtbMarkers[i].sprite;
    if (marker[i].tinted && p->color.bytes.r == marker[i].tr &&
        p->color.bytes.g == marker[i].tg && p->color.bytes.b == marker[i].tb) {
      p->color.bytes.r = marker[i].r;
      p->color.bytes.g = marker[i].g;
      p->color.bytes.b = marker[i].b;
    }
    marker[i].tinted = 0;
  }
}
int pe_styles_enabled(void) { return enabled || requested; }
static int partial_actions(void) {
  const PeBattleActionPolicyApi *api =
      pe_service_get("battle_action_policy", 1);
  return api && api->partial_actions();
}
static void settings_tick(void) {
  if (pe_in_battle()) {
    if (requested != locked) {
      requested = locked;
      pe_menu_save_settings();
      pe_toast("Change costume abilities outside battle");
    }
  } else {
    enabled = requested != 0;
    locked = enabled;
    pe_style_end(&session);
    action_target_count = 0;
    restore_target_cue();
    hit_entity = NULL;
    restore_marker();
    pulse = 0;
    pe_pulse = 0;
    restore_pe_cue();
  }
}
int pe_styles_set_enabled(int value) {
  if (pe_in_battle())
    return 0;
  requested = value != 0;
  settings_tick();
  pe_menu_save_settings();
  return 1;
}
static void ensure_session(void) {
  const PeCostume *c;
  PeStyleId id = PE_STYLE_ORIGINAL;
  if (!pe_in_battle()) {
    pe_style_end(&session);
    return;
  }
  if (session.active) {
    /* Settings in other mods can change through F1. Fail closed for the rest
     * of this encounter if an unsupported partial-action policy appears. */
    if (session.style == PE_STYLE_BULWARK && partial_actions()) {
      pe_style_begin(&session, PE_STYLE_ORIGINAL);
      pe_toast("Half-bar policy changed: vanilla for this encounter");
    }
    return;
  }
  locked = enabled;
  c = pe_wardrobe_worn();
  if (enabled && pe_skin_appearance_ready(c))
    id = pe_style_for_costume(c ? c->key : NULL);
  if (id == PE_STYLE_BULWARK && partial_actions()) {
    id = PE_STYLE_ORIGINAL;
    pe_toast("Bulwark needs half-bar actions off; using vanilla");
  }
  pe_style_begin(&session, id);
  action_target_count = 0;
  pe_log("costume_style", "begin=%s enabled=%d costume=%s ready=%d",
         pe_style_definition(session.style)->key, enabled,
         c ? c->key : "original", pe_skin_appearance_ready(c));
}
static int controllable(void) {
  Combatant *core = pe_aya_core();
  BattleEntity *player = pe_player();
  return core && player && pe_in_battle() && g_BattleModeState == 0 &&
         g_BattleEnemyEscapeFlag && g_BattleLiveEnemyCount > 0 &&
         !(g_GameStateFlags & 0x104u) && !(core->stateFlags & 0x192000u) &&
         !g_BattleHitActionPending &&
         (player->actionMode < 4 || player->actionMode == core->actionMode12);
}
static int guarded(void) {
  Combatant *core = pe_aya_core();
  return core &&
         pe_style_guard(&session, core->hpAlive, FULL_AT, controllable());
}
static int living_enemies(void) {
  unsigned i;
  for (i = 0; i < 7; i++)
    if (g_BattleEnemySlots[i].active &&
        g_BattleEnemySlots[i].combatant.hpAlive > 0)
      return 1;
  return 0;
}
static int update(void *ctx, void *user) {
  (void)ctx;
  (void)user;
  ensure_session();
  if (pulse)
    pulse--;
  if (pe_pulse)
    pe_pulse--;
  return 0;
}
static int hud(void *ctx, void *user) {
  unsigned i;
  (void)ctx;
  (void)user;
  restore_marker();
  restore_pe_cue();
  /* Spellweave: pulse on each new charge, steady at a full Overchannel. */
  if (pe_pulse ||
      (session.style == PE_STYLE_CHANNELER &&
       session.weave_charges >=
           pe_style_definition(PE_STYLE_CHANNELER)->weave_charges))
    for (unsigned slot = 0; slot < 2; slot++) {
      RenderSpritePacket *p = &g_BattlePeLabels[slot].sprite;
      pe_codes[slot] = p->color.bytes.code;
      /* The native PE label is raw-textured: enable modulation for the cue. */
      p->color.bytes.code &= (u8)~1u;
      tint_rgb(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
               &pe_tints[slot]);
    }
  /* Cyan: guarded or a Quickstep refund. Orange: a Retaliate counter is
   * loaded while the guard is off (typically a full bar, ready to act). */
  u8 r, g, b;
  if (guarded() || pulse) {
    r = 0x60, g = 0xe0, b = 0xff;
  } else if (session.style == PE_STYLE_BULWARK && session.counter_store) {
    r = 0xff, g = 0x90, b = 0x30;
  } else {
    return 0;
  }
  for (i = 0; i < 2; i++) {
    RenderSpritePacket *p = &g_BattleAtbMarkers[i].sprite;
    marker[i].r = p->color.bytes.r;
    marker[i].g = p->color.bytes.g;
    marker[i].b = p->color.bytes.b;
    marker[i].tr = r;
    marker[i].tg = g;
    marker[i].tb = b;
    marker[i].tinted = 1;
    p->color.bytes.r = r;
    p->color.bytes.g = g;
    p->color.bytes.b = b;
  }
  return 0;
}
static int charge(void *ctx, void *user) {
  PeHookAtCharge *q = ctx;
  (void)user;
  ensure_session();
  if (q && q->actor == pe_aya_core() && pe_in_battle())
    *q->increment = pe_style_charge(&session, *q->increment);
  return 0;
}
static int hp_loss(void *ctx, void *user) {
  PeHookHpLoss *q = ctx;
  int before;
  (void)user;
  ensure_session();
  if (q && q->actor == pe_aya_core() && q->source >= 1 && q->source <= 3) {
    int guard = guarded();
    before = *q->loss;
    *q->loss = pe_style_damage(&session, *q->loss, guard);
    /* Retaliate stores the HP actually lost, never more than Aya had. */
    int taken = *q->loss;
    if (q->current_hp >= 0 && taken > q->current_hp)
      taken = q->current_hp;
    pe_style_absorb(&session, taken, guard);
    if (*q->loss != before)
      pe_log("costume_style", "bulwark loss=%d->%d atb=%u store=%u", before,
             *q->loss, pe_aya_core()->hpAlive, session.counter_store);
  }
  return 0;
}
static int hit_begin(void *ctx, void *user) {
  BattleEntity *target = ctx;
  Combatant *core = pe_aya_core();
  (void)user;
  ensure_session();
  hit_entity = NULL;
  hit_ordinary = 0;
  if (core && target && target != pe_player() && target->core &&
      g_ActiveActor == core && !(core->stateFlags & 0x180000u)) {
    hit_entity = target;
    hit_hp = ((EnemyCombatant *)target->core)->hpAlive;
  }
  return 0;
}
static int hit_end(void *ctx, void *user) {
  BattleEntity *target = ctx;
  (void)user;
  if (target && target == hit_entity && target->core) {
    pe_style_kill(&session, hit_hp, ((EnemyCombatant *)target->core)->hpAlive,
                  1);
    pe_style_weapon_damage(&session, hit_hp,
                           ((EnemyCombatant *)target->core)->hpAlive,
                           hit_ordinary);
  }
  hit_entity = NULL;
  hit_ordinary = 0;
  return 0;
}
static int normal_gun(void) {
  const ItemDataRecord *weapon = pe_equipped(0);
  Combatant *core = pe_aya_core();
  return core && core->action && !(core->stateFlags & 0x180000u) &&
         core->action->actionCode.actionId != 8 && weapon &&
         weapon->kind >= 1 && weapon->kind < 8;
}
static int normal_weapon(void) {
  const ItemDataRecord *weapon = pe_equipped(0);
  Combatant *core = pe_aya_core();
  return core && core->action && !(core->stateFlags & 0x180000u) && weapon &&
         weapon->kind >= 1 && weapon->kind <= 8;
}
static int recovery(void *ctx, void *user) {
  PeHookPeRecovery *q = ctx;
  (void)user;
  ensure_session();
  if (q && q->actor == pe_aya_core() && pe_in_battle() &&
      session.style == PE_STYLE_CHANNELER) {
    *q->increment = pe_style_pe_recovery(&session, *q->increment);
    if (q->current >= 0 && q->maximum >= q->current &&
        *q->increment >= q->maximum - q->current) {
      *q->increment = q->maximum - q->current;
      session.pe_remainder = 0;
    }
  }
  return 0;
}
static int cancel_action(void *ctx, void *user) {
  (void)user;
  if (ctx == pe_aya_core())
    pe_style_action_cancel(&session);
  return 0;
}
static int actor_mode(void *ctx, void *user) {
  PeHookActionMode *q = ctx;
  (void)user;
  if (!pe_in_battle() || !session.active)
    return 0;
  Combatant *core = pe_aya_core();
  if (q && core && q->actor == pe_player() && q->mode >= 14 &&
      q->mode != core->actionMode12)
    pe_style_action_cancel(&session);
  return 0;
}
static int candidate(const BattleEntity *e) {
  return e && e != pe_player() && e->core && !(e->entityFlags & 0x4000u) &&
         (e->entityFlags & 0x2040u) != 0x40u &&
         ((EnemyCombatant *)e->core)->hpAlive > 0;
}
static int point_blank(const BattleEntity *e) {
  BattleEntity *p = pe_player();
  if (!p || !candidate(e) || !e->moveSpeed)
    return 0;
  unsigned radius =
      (unsigned)(u16)e->renderObject.hit_cylinder.radius * e->moveSpeed / 4096u;
  return pe_style_point_blank(
      &session, e->renderObject.hit_cylinder.value0 - p->posX.parts.integer,
      e->renderObject.hit_cylinder.value2 - p->posZ.parts.integer, radius);
}
static int outgoing(void *ctx, void *user) {
  PeHookEnemyDamage *q = ctx;
  (void)user;
  ensure_session();
  if (q && q->source == pe_aya_core() && q->target == hit_entity &&
      q->ordinary && *q->damage > 0 && normal_weapon())
    hit_ordinary = 1;
  if (q && session.style == PE_STYLE_BULWARK && q->source == pe_aya_core() &&
      normal_weapon() && candidate(q->target)) {
    int before = *q->damage;
    *q->damage = pe_style_counter(&session, before, q->ordinary);
    if (*q->damage != before)
      pe_log("costume_style", "retaliate damage=%d->%d store=%u", before,
             *q->damage, session.counter_store);
  }
  if (session.style == PE_STYLE_STRIKER && q && q->source == pe_aya_core())
    pe_log("costume_style",
           "striker admission gun=%d ordinary=%d targets=%u damage=%d",
           normal_gun(), q->ordinary, action_target_count, *q->damage);
  if (!q || q->source != pe_aya_core() || !normal_gun() || !q->ordinary ||
      !candidate(q->target))
    return 0;
  for (unsigned i = 0; i < action_target_count; i++) {
    if (action_targets[i].entity == q->target &&
        action_targets[i].core == ((const BattleEntity *)q->target)->core) {
      int before = *q->damage;
      *q->damage = pe_style_gun_damage(&session, i, before, 1);
      if (*q->damage != before)
        pe_log("costume_style", "striker damage=%d->%d recipient=%u", before,
               *q->damage, i);
      break;
    }
  }
  return 0;
}
static int target_preview(void *ctx, void *user) {
  PeHookTargetPreview *q = ctx;
  (void)user;
  if (!q)
    return 0;
  if (!q->phase) {
    restore_target_cue();
    return 0;
  }
  ensure_session();
  if (!q || q->mode < 0 || q->mode > 2 || q->out_of_range || !normal_gun() ||
      session.style != PE_STYLE_STRIKER)
    return 0;
  const BattleEntity *e = (BattleEntity *)g_FieldActorListHead;
  for (unsigned visited = 0; e && visited < 256; visited++, e = e->next) {
    if (&e->renderObject == q->object && point_blank(e)) {
      unsigned slot = g_ActiveDrawSlot;
      if (slot > 1)
        return 0;
      for (unsigned i = 0; i < 4; i++) {
        RenderSpritePacket *p = target_packet(slot, i);
        tint_rgb(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
                 &target_tints[slot][i]);
      }
      BattleStatusPointerPrim *p = &g_BattleStatusPointerTriangles[slot];
      tint_rgb(&p->r, &p->g, &p->b, &triangle_tints[slot]);
      break;
    }
  }
  return 0;
}
static int action_begin(void *ctx, void *user) {
  (void)user;
  ensure_session();
  if (ctx == pe_aya_core()) {
    pe_style_action_begin(&session, normal_gun(), normal_weapon());
    action_target_count = 0;
    if (session.style == PE_STYLE_STRIKER && session.gun_action) {
      const BattleEntity *e = (BattleEntity *)g_FieldActorListHead;
      for (unsigned visited = 0;
           e && visited < 256 && action_target_count < PE_STYLE_TARGETS;
           visited++, e = e->next) {
        if (!candidate(e))
          continue;
        unsigned i = action_target_count++;
        action_targets[i].entity = e;
        action_targets[i].core = e->core;
        pe_style_target(&session, i, point_blank(e));
        pe_log("costume_style",
               "striker range recipient=%u dx=%d dz=%d radius=%u scale=%u "
               "close=%d",
               i,
               e->renderObject.hit_cylinder.value0 -
                   pe_player()->posX.parts.integer,
               e->renderObject.hit_cylinder.value2 -
                   pe_player()->posZ.parts.integer,
               (unsigned)(u16)e->renderObject.hit_cylinder.radius,
               (unsigned)e->moveSpeed, point_blank(e));
      }
      pe_log("costume_style", "striker snapshot targets=%u close=%llu",
             action_target_count, (unsigned long long)session.close_targets);
    }
  }
  return 0;
}
static int action_end(void *ctx, void *user) {
  Combatant *core = pe_aya_core();
  unsigned before;
  (void)user;
  if (!core || ctx != core || !session.active)
    return 0;
  before = core->hpAlive;
  core->hpAlive = (u16)pe_style_resolve(
      &session, before, FULL_AT,
      pe_in_battle() && (s16)core->curHP > 0 && living_enemies());
  if (core->hpAlive > before) {
    pulse = 12;
    pe_log("costume_style", "quickstep refund=%u", core->hpAlive - before);
  }
  pe_style_counter_resolve(&session);
  if (session.style == PE_STYLE_CHANNELER) {
    if (pe_style_weave_resolve(&session, pe_in_battle() &&
                                             (s16)core->curHP > 0 &&
                                             living_enemies())) {
      pe_pulse = 12;
      pe_log("costume_style", "spellweave charges=%u", session.weave_charges);
    }
  } else {
    session.action_open = 0;
    session.pending_damage = 0;
  }
  session.gun_action = 0;
  session.close_targets = 0;
  action_target_count = 0;
  return 0;
}
/* Support powers whose cost Battle_ApplySpellEffect charges after this hook.
 * Energy Shot, Liberate and the attack powers pay elsewhere and are left
 * alone: they neither use nor clear Spellweave charges. */
static int weave_power(unsigned spell) {
  return spell <= 12u && spell != 6u ? 1 : spell == 18u;
}
/* The raw 16.16 PE the native code is about to subtract, after other mods'
 * cost changes and the PE-saving armor, exactly as Battle_ApplySpellEffect
 * and the Rebalance refund compute it. */
static unsigned weave_raw_cost(const Combatant *core, unsigned spell) {
  if (spell == 5u) /* Barrier: a third of max PE, armor does not apply */
    return core->maxAtk > 0 ? (unsigned)core->maxAtk / 3u : 0u;
  const ParasiteSpellEntry *table = Aya_GetParasiteSpellUnlockTable();
  int cost = table ? table[spell].cost : 0;
  PeHookPowerCost query = {(int)spell, &cost};
  pe_hook_dispatch(PE_HOOK_POWER_COST, &query);
  if (cost <= 0)
    return 0;
  uint64_t raw = (uint64_t)(unsigned)cost * 0x10000u;
  if (core->attributes && (core->attributes->effectFlags & 0x200u))
    raw = raw * 2u / 3u;
  return raw > 0x7fffffffu ? 0x7fffffffu : (unsigned)raw;
}
static int spell(void *ctx, void *user) {
  static const int heal[3] = {30, 60, 280};
  const PeHookSpell *q = ctx;
  Combatant *core = pe_aya_core();
  int overchannel;
  (void)user;
  if (!q || !core || !pe_in_battle() || g_ActiveActor != core)
    return 0;
  ensure_session();
  if (session.style != PE_STYLE_CHANNELER || !session.weave_charges ||
      !weave_power(q->spell))
    return 0;
  unsigned charges = session.weave_charges;
  unsigned percent = pe_style_weave_cast(&session, &overchannel);
  unsigned raw = weave_raw_cost(core, q->spell);
  /* Refund first; the native code then subtracts its full cost. */
  unsigned refund = (unsigned)((uint64_t)raw * percent / 100u);
  core->exp_or_acc = (s32)((u32)core->exp_or_acc + refund);
  if (overchannel && q->spell <= 2u) /* native clamps HP to max afterwards */
    core->curHP = (u16)(core->curHP +
                        (heal[q->spell] *
                             (int)pe_style_definition(PE_STYLE_CHANNELER)
                                 ->overchannel_percent +
                         99) /
                            100);
  pe_pulse = 12;
  pe_log("costume_style",
         "spellweave cast spell=%u charges=%u refund=%u/%u overchannel=%d",
         q->spell, charges, refund, raw, overchannel);
  return 0;
}
static int player_move(void *ctx, void *user) {
  PeHookPlayerMove *q = ctx;
  (void)user;
  if (!q || !q->factor || !q->combat || !pe_in_battle() ||
      q->actor != pe_player() || session.style != PE_STYLE_QUICKSTEP)
    return 0;
  *q->factor = pe_style_move(&session, *q->factor);
  return 0;
}
static int reset(void *ctx, void *user) {
  (void)ctx;
  (void)user;
  restore_marker();
  pulse = 0;
  pe_pulse = 0;
  restore_pe_cue();
  pe_style_end(&session);
  action_target_count = 0;
  restore_target_cue();
  hit_entity = NULL;
  return 0;
}
void pe_styles_describe(const char *key, char *out, unsigned size) {
  const PeStyleDef *d = pe_style_definition(pe_style_for_costume(key));
  const char *state = d->id == PE_STYLE_ORIGINAL ? "native"
                      : !d->implemented          ? "later"
                      : !enabled                 ? "off"
                      : d->id == PE_STYLE_BULWARK && partial_actions()
                          ? "blocked"
                          : "on";
  snprintf(out, size, "%s (%s)", d->name, state);
}
static void style_info(char *out, unsigned size) {
  const PeCostume *c = pe_wardrobe_worn();
  if (pe_in_battle() && session.active) {
    snprintf(out, size, "%s (active)",
             pe_style_definition(session.style)->name);
    return;
  }
  if (enabled && pe_style_for_costume(c ? c->key : NULL) == PE_STYLE_BULWARK &&
      partial_actions()) {
    snprintf(out, size, "Original (disable half-bar actions for Bulwark)");
    return;
  }
  pe_styles_describe(c ? c->key : NULL, out, size);
}
static void guard_info(char *out, unsigned size) {
  if (session.style == PE_STYLE_CHANNELER) {
    snprintf(out, size, "Spellweave %u/%u", session.weave_charges,
             pe_style_definition(PE_STYLE_CHANNELER)->weave_charges);
    return;
  }
  snprintf(out, size, "%s, counter %u",
           guarded() ? "Protected" : "Not protected", session.counter_store);
}
static const PeStyleDef *browsed_style(void) {
  const PeCostume *c = pe_wardrobe_browsed();
  return pe_style_definition(pe_style_for_costume(c ? c->key : NULL));
}
static void preview_info(char *out, unsigned size) {
  const PeCostume *c = pe_wardrobe_browsed();
  pe_styles_describe(c ? c->key : NULL, out, size);
}
static void preview_benefit(char *out, unsigned size) {
  const PeStyleDef *d = browsed_style();
  snprintf(out, size, "%s",
           d->implemented ? d->benefit
                          : "Native combat; planned style not available");
}
static void preview_tradeoff(char *out, unsigned size) {
  const PeStyleDef *d = browsed_style();
  snprintf(out, size, "%s",
           d->implemented ? d->limitation
                          : "No costume ability in this prototype");
}
void pe_styles_register(const char *mod_id) {
  pe_menu_register_toggle(
      PE_PAGE_WARDROBE, "Costume abilities (prototype)",
      "Off: vanilla combat. On: costume style. Change outside battle only.",
      &requested, settings_tick);
  pe_menu_register_info(PE_PAGE_WARDROBE, "Selected style",
                        "Preview for the highlighted F1 costume; browsing "
                        "changes no combat state.",
                        preview_info);
  pe_menu_register_info(
      PE_PAGE_WARDROBE, "Style benefit",
      "The highlighted costume's ability when costume abilities are enabled.",
      preview_benefit);
  pe_menu_register_info(PE_PAGE_WARDROBE, "Style tradeoff",
                        "The highlighted costume's cost or limitation. "
                        "Ownership rules are unchanged.",
                        preview_tradeoff);
  pe_menu_register_info(PE_PAGE_WARDROBE, "Costume style",
                        "Quickstep: kills +25% AT and a speed burst. Bulwark: "
                        "25% guard; hits taken load a counter. Striker: close "
                        "guns +25%. Channeler: weapon hits charge the next PE "
                        "power (3 = free); half passive PE.",
                        style_info);
  pe_menu_register_info(
      PE_PAGE_WARDROBE, "Style state",
      "Bulwark: guard below full AT while free to move, and stored counter "
      "damage. Channeler: Spellweave charges. Half-bar features must be off "
      "for Bulwark.",
      guard_info);
  pe_hook_add(PE_HOOK_BATTLE_UPDATE, update, NULL, PE_PRIORITY_EARLY, mod_id);
  pe_hook_add(PE_HOOK_BATTLE_HUD, hud, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_AT_CHARGE, charge, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_PE_RECOVERY, recovery, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_ACTION_MODE, actor_mode, NULL, PE_PRIORITY_NORMAL,
              mod_id);
  pe_hook_add(PE_HOOK_PLAYER_ACTION_CANCEL, cancel_action, NULL,
              PE_PRIORITY_NORMAL, mod_id);
  pe_hook_add(PE_HOOK_HP_LOSS, hp_loss, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_ENEMY_HIT, hit_begin, NULL, PE_PRIORITY_EARLY, mod_id);
  pe_hook_add(PE_HOOK_ENEMY_HIT_END, hit_end, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_ENEMY_DAMAGE, outgoing, NULL, PE_PRIORITY_LATE, mod_id);
  pe_hook_add(PE_HOOK_TARGET_PREVIEW, target_preview, NULL, PE_PRIORITY_LATE,
              mod_id);
  pe_hook_add(PE_HOOK_PLAYER_ACTION, action_begin, NULL, PE_PRIORITY_NORMAL,
              mod_id);
  pe_hook_add(PE_HOOK_PLAYER_ACTION_END, action_end, NULL, PE_PRIORITY_LATE,
              mod_id);
  pe_hook_add(PE_HOOK_SPELL, spell, NULL, PE_PRIORITY_LATE, mod_id);
  /* LATE: scales whatever battle speed the movement mod chose. */
  pe_hook_add(PE_HOOK_PLAYER_MOVE, player_move, NULL, PE_PRIORITY_LATE,
              mod_id);
  pe_hook_add(PE_HOOK_SCENE_START, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
  pe_hook_add(PE_HOOK_SAVE_READ, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
  pe_hook_add(PE_HOOK_NEW_GAME, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}
