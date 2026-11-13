/* Native adapter. Definitions and simulation have no dependency on game state.
 */
#include "pe1/battle_ailment.h"
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
  u8 r, g, b;    /* native color */
  u8 tr, tg, tb; /* cue color applied */
  int tinted;
} TargetTint;
typedef struct {
  u8 r, g, b;
} CueColor;
/* Battle cue colors, one place. */
static const CueColor CUE_CYAN = {0x60, 0xe0, 0xff}; /* guard, refund, Striker */
static const CueColor CUE_COUNTER = {0xff, 0x90, 0x30}; /* Bulwark loaded */
static const CueColor CUE_RUSH = {0xff, 0x50, 0x40}; /* Quickstep burst */
static const CueColor CUE_SPENT = {0x30, 0x30, 0x30}; /* Quickstep HP pip */
static const CueColor CUE_CRACK = {0xff, 0x90, 0x30}; /* Channeler cracked */
/* Striker falloff: between the native in-range red (96,14,14) and the native
 * out-of-range dark red (32,0a,0a). */
static const CueColor CUE_FAR = {0x5a, 0x1e, 0x1e};
enum { CRACK_FLICKER_FRAMES = 4 };
static unsigned frame_count;
static TargetTint target_tints[2][4], triangle_tints[2];
static TargetTint pe_tints[2];
static u8 pe_codes[2];
static void restore_rgb(u8 *r, u8 *g, u8 *b, TargetTint *t) {
  if (t->tinted && *r == t->tr && *g == t->tg && *b == t->tb) {
    *r = t->r;
    *g = t->g;
    *b = t->b;
  }
  t->tinted = 0;
}
static void tint_color(u8 *r, u8 *g, u8 *b, TargetTint *t, CueColor c) {
  if (t->tinted && *r == t->tr && *g == t->tg && *b == t->tb)
    return; /* already ours this frame: keep the native color saved */
  t->r = *r;
  t->g = *g;
  t->b = *b;
  t->tr = c.r;
  t->tg = c.g;
  t->tb = c.b;
  t->tinted = 1;
  *r = c.r;
  *g = c.g;
  *b = c.b;
}
static void tint_rgb(u8 *r, u8 *g, u8 *b, TargetTint *t) {
  tint_color(r, g, b, t, CUE_CYAN);
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
    if (pe_tints[i].tinted && p->color.bytes.r == pe_tints[i].tr &&
        p->color.bytes.g == pe_tints[i].tg &&
        p->color.bytes.b == pe_tints[i].tb &&
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
  pe_quickstep_tick(&session,
                    session.style == PE_STYLE_QUICKSTEP && controllable());
  pe_channeler_tick(&session);
  frame_count++;
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
  /* Channeler PE label: cyan pulse on each charge or cast, steady cyan at full
   * charge, orange flicker once cracked. A break drops it back to the
   * one-charge look (no glow). */
  int pe_cue = pe_pulse != 0;
  CueColor pe_color = CUE_CYAN;
  if (pe_channeler_full(&session)) {
    pe_cue = 1;
    if (session.ch.cracked) {
      pe_color = CUE_CRACK;
      pe_cue = (frame_count / CRACK_FLICKER_FRAMES) % 2 == 0;
    }
  }
  if (pe_cue)
    for (unsigned slot = 0; slot < 2; slot++) {
      RenderSpritePacket *p = &g_BattlePeLabels[slot].sprite;
      pe_codes[slot] = p->color.bytes.code;
      /* The native PE label is raw-textured: enable modulation for the cue. */
      p->color.bytes.code &= (u8)~1u;
      tint_color(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
                 &pe_tints[slot], pe_color);
    }
  /* Cyan: guarded or a Quickstep refund landing. Orange: a Retaliate counter
   * is loaded while the guard is off (typically a full bar, ready to act).
   * Red: Quickstep burst, faster and taking more damage. */
  CueColor c;
  if (guarded() || pulse) {
    c = CUE_CYAN;
  } else if (session.style == PE_STYLE_BULWARK && session.counter_store) {
    c = CUE_COUNTER;
  } else if (pe_quickstep_burst(&session)) {
    c = CUE_RUSH;
  } else {
    return 0;
  }
  u8 r = c.r, g = c.g, b = c.b;
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
    /* Bulwark guard or Quickstep Overextended, judged as the hit resolves. */
    *q->loss = pe_style_damage(&session, *q->loss, guard);
    if (*q->loss != before && session.style == PE_STYLE_QUICKSTEP)
      pe_log("costume_style", "overextended loss=%d->%d burst=%u", before,
             *q->loss, session.qs.burst);
    /* Retaliate stores the HP actually lost, never more than Aya had. */
    int taken = *q->loss;
    if (q->current_hp >= 0 && taken > q->current_hp)
      taken = q->current_hp;
    pe_style_absorb(&session, taken, guard);
    if (*q->loss != before && session.style == PE_STYLE_BULWARK)
      pe_log("costume_style", "bulwark loss=%d->%d atb=%u store=%u", before,
             *q->loss, pe_aya_core()->hpAlive, session.counter_store);
    /* Channeler Backlash: a damaging hit at full charge cracks, then breaks. */
    if (*q->loss > 0) {
      int backlash = pe_channeler_hit(&session);
      if (backlash != PE_CH_NONE)
        pe_log("costume_style", "spellweave %s charges=%u",
               backlash == PE_CH_CRACK ? "crack" : "break",
               session.ch.charges);
    }
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
    const EnemyCombatant *foe = target->core;
    /* Judged against max HP at the time of the hit, per enemy or part. */
    pe_quickstep_hit(&session, foe, hit_hp, foe->hpAlive, foe->maxHp);
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
      session.style == PE_STYLE_CHANNELER &&
      pe_style_definition(PE_STYLE_CHANNELER)->channeler.pe_recovery_percent !=
          100) { /* 100 = native: leave the game's value untouched */
    *q->increment = pe_channeler_recovery(&session, *q->increment);
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
  return pe_striker_point_blank(
      &session, e->renderObject.hit_cylinder.value0 - p->posX.parts.integer,
      e->renderObject.hit_cylinder.value2 - p->posZ.parts.integer, radius);
}
/* The gun's native range: what the targeting dome, the out-of-range marker and
 * native accuracy use, mod-changed weapon stats included. Darkness shrinks it
 * natively. 0 when unknown, which turns falloff off. */
static unsigned native_range(void) {
  Combatant *core = pe_aya_core();
  if (!core || !core->action)
    return 0;
  if ((core->stateFlags & AYA_DARKNESS_MASK) == AYA_DARKNESS)
    return AYA_DARKNESS_RANGE;
  return core->action->range > 0 ? (unsigned)core->action->range : 0u;
}
/* Striker zone for one recipient, the same call for preview and snapshot. */
static int striker_zone(const BattleEntity *e) {
  BattleEntity *p = pe_player();
  if (!p || !candidate(e))
    return PE_ST_NATIVE;
  return pe_striker_zone(&session, point_blank(e),
                         e->renderObject.target_x - p->posX.parts.integer,
                         e->renderObject.target_z - p->posZ.parts.integer,
                         native_range());
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
      *q->damage = pe_striker_damage(&session, i, before, 1);
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
  if (q->mode < 0 || q->mode > 2 ||
      (session.style != PE_STYLE_STRIKER && session.style != PE_STYLE_QUICKSTEP))
    return 0;
  const BattleEntity *e = (BattleEntity *)g_FieldActorListHead;
  for (unsigned visited = 0; e && visited < 256; visited++, e = e->next) {
    if (&e->renderObject != q->object)
      continue;
    CueColor c;
    if (session.style == PE_STYLE_STRIKER) {
      /* Cyan point-blank, native red in the middle, dim red in the falloff
       * band. Beyond range the native marker is already dark: left alone. */
      if (q->out_of_range || !normal_gun())
        return 0;
      int zone = striker_zone(e);
      if (zone == PE_ST_POINT_BLANK)
        c = CUE_CYAN;
      else if (zone == PE_ST_FALLOFF)
        c = CUE_FAR;
      else
        return 0;
    } else {
      /* Quickstep HP pip: the marker darkens once this enemy has dropped to
       * half, so its breakpoint refund is used up. */
      const EnemyCombatant *foe = e->core;
      if (!candidate(e) ||
          !pe_quickstep_spent(&session, foe, foe->hpAlive, foe->maxHp))
        return 0;
      c = CUE_SPENT;
    }
    unsigned slot = g_ActiveDrawSlot;
    if (slot > 1)
      return 0;
    for (unsigned i = 0; i < 4; i++) {
      RenderSpritePacket *p = target_packet(slot, i);
      tint_color(&p->color.bytes.r, &p->color.bytes.g, &p->color.bytes.b,
                 &target_tints[slot][i], c);
    }
    BattleStatusPointerPrim *p = &g_BattleStatusPointerTriangles[slot];
    tint_color(&p->r, &p->g, &p->b, &triangle_tints[slot], c);
    break;
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
        pe_striker_target(&session, i, striker_zone(e));
        pe_log("costume_style",
               "striker range recipient=%u dx=%d dz=%d radius=%u scale=%u "
               "tx=%d tz=%d range=%u zone=%d",
               i,
               e->renderObject.hit_cylinder.value0 -
                   pe_player()->posX.parts.integer,
               e->renderObject.hit_cylinder.value2 -
                   pe_player()->posZ.parts.integer,
               (unsigned)(u16)e->renderObject.hit_cylinder.radius,
               (unsigned)e->moveSpeed,
               e->renderObject.target_x - pe_player()->posX.parts.integer,
               e->renderObject.target_z - pe_player()->posZ.parts.integer,
               native_range(), pe_striker_target_zone(&session, i));
      }
      pe_log("costume_style", "striker snapshot targets=%u range=%u",
             action_target_count, native_range());
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
  core->hpAlive = (u16)pe_quickstep_resolve(
      &session, before, FULL_AT,
      pe_in_battle() && (s16)core->curHP > 0 && living_enemies());
  if (core->hpAlive > before) {
    pulse = 12;
    pe_log("costume_style", "quickstep refund=%u burst=%u",
           core->hpAlive - before, session.qs.burst);
  }
  pe_style_counter_resolve(&session);
  if (session.style == PE_STYLE_CHANNELER) {
    if (pe_channeler_resolve(&session, pe_in_battle() &&
                                           (s16)core->curHP > 0 &&
                                           living_enemies())) {
      pe_pulse = 12;
      pe_log("costume_style", "spellweave charges=%u", session.ch.charges);
    }
  } else {
    session.action_open = 0;
    session.pending_damage = 0;
  }
  session.gun_action = 0;
  pe_striker_action_begin(&session); /* snapshot ends with the action */
  action_target_count = 0;
  return 0;
}
/* Support powers whose cost Battle_ApplySpellEffect charges after this hook.
 * Energy Shot, Liberate and the attack powers pay elsewhere and are left
 * alone: they neither use nor clear Spellweave charges. */
static int weave_power(unsigned spell) {
  return spell <= 12u && spell != 6u ? 1 : spell == 18u;
}
static int raw_cost_query;
/* The PE menu's cost, its can-afford check and the queued reservation all
 * read Inv_GetSlotHighlightState, which asks PE_HOOK_POWER_COST. Showing the
 * discounted cost there makes "affordability uses the discounted cost" true
 * everywhere at once; the cast itself still pays through the refund in
 * spell(). Barrier (5) is not a table cost, so its menu stays native. */
static int power_cost(void *ctx, void *user) {
  PeHookPowerCost *q = ctx;
  (void)user;
  if (!q || !q->cost || *q->cost <= 0 || raw_cost_query || !pe_in_battle() ||
      session.style != PE_STYLE_CHANNELER || q->power < 0 ||
      !weave_power((unsigned)q->power))
    return 0;
  unsigned percent = pe_channeler_discount(&session);
  *q->cost -= (int)((int64_t)*q->cost * percent / 100);
  return 0;
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
  raw_cost_query = 1; /* other mods' cost, without our own discount */
  pe_hook_dispatch(PE_HOOK_POWER_COST, &query);
  raw_cost_query = 0;
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
  if (session.style != PE_STYLE_CHANNELER || !session.ch.charges ||
      !weave_power(q->spell))
    return 0;
  unsigned charges = session.ch.charges;
  unsigned raw = weave_raw_cost(core, q->spell);
  unsigned percent = pe_channeler_cast(&session, &overchannel);
  /* Refund first; the native code then subtracts its full cost. */
  unsigned refund = (unsigned)((uint64_t)raw * percent / 100u);
  core->exp_or_acc = (s32)((u32)core->exp_or_acc + refund);
  if (overchannel && q->spell <= 2u) /* native clamps HP to max afterwards */
    core->curHP = (u16)(core->curHP +
                        (heal[q->spell] *
                             (int)pe_style_definition(PE_STYLE_CHANNELER)
                                 ->channeler.overchannel_percent +
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
  *q->factor = pe_quickstep_move(&session, *q->factor);
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
    snprintf(out, size, "Spellweave %u/%u%s", session.ch.charges,
             pe_style_definition(PE_STYLE_CHANNELER)->channeler.max_charges,
             session.ch.cracked ? ", cracked" : "");
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
                        "Quickstep: half-HP breaks and kills +20% AT, 3 s "
                        "speed, +20% damage taken meanwhile. Bulwark: "
                        "25% guard; hits taken load a counter. Striker: point-"
                        "blank guns +25%, outer 30% of gun range -20%. Channeler: weapon hits charge the next "
                        "support PE (1 = half, 2 = free); at 2, a hit cracks "
                        "it and a second breaks one.",
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
  /* LATE: discounts whatever cost Rebalance or other mods chose. */
  pe_hook_add(PE_HOOK_POWER_COST, power_cost, NULL, PE_PRIORITY_LATE, mod_id);
  /* LATE: scales whatever battle speed the movement mod chose. */
  pe_hook_add(PE_HOOK_PLAYER_MOVE, player_move, NULL, PE_PRIORITY_LATE,
              mod_id);
  pe_hook_add(PE_HOOK_SCENE_START, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
  pe_hook_add(PE_HOOK_SAVE_READ, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
  pe_hook_add(PE_HOOK_NEW_GAME, reset, NULL, PE_PRIORITY_NORMAL, mod_id);
}
