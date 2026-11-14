/* Native adapter. Definitions and simulation have no dependency on game state.
 */
#include "pe1/battle_ailment.h"
#include "pe1/field_actor.h"
#include "pe1/field_glow_line.h"
#include "pe1/fixed_point.h"
#include "pe1/geom_state.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_matrix_slot.h"
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
#include <string.h>
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
/* Bulwark AT bar fill, left to right like the native teal-to-mint gradient:
 * blue while guarded, red pulsing while Exposed. */
static const CueColor CUE_GUARD_LOW = {0x10, 0x30, 0x90};
static const CueColor CUE_GUARD_HIGH = {0x70, 0xb0, 0xff};
static const CueColor CUE_EXPOSED_LOW = {0x60, 0x00, 0x00};
static const CueColor CUE_EXPOSED_HIGH = {0xff, 0x30, 0x30}; /* r pulses */
enum { EXPOSED_PULSE_FRAMES = 12 }; /* dim to bright and back: 24 frames */
/* First time per play session, a one-line explanation (toast). */
enum { HINT_GUARD = 1, HINT_EXPOSED = 2, HINT_COUNTER = 4 };
static unsigned hints_shown;
static void hint(unsigned bit, const char *text) {
  if (hints_shown & bit)
    return;
  hints_shown |= bit;
  pe_toast(text);
}
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
/* Bulwark AT bar fill tint, both draw buffers; restored only if nothing else
 * changed the colors since. */
typedef struct {
  u8 native[4][3], cue[4][3];
  int tinted;
} GaugeTint;
static GaugeTint gauge_tints[2];
static u8 *gauge_rgb(RenderGouraudQuad *q, unsigned v) {
  return v == 0 ? &q->r0 : v == 1 ? &q->r1 : v == 2 ? &q->r2 : &q->r3;
}
static void restore_gauge(void) {
  for (unsigned slot = 0; slot < 2; slot++) {
    GaugeTint *t = &gauge_tints[slot];
    RenderGouraudQuad *q = &g_BattleAtbGauges[slot];
    int ours = t->tinted;
    for (unsigned v = 0; ours && v < 4; v++)
      ours = !memcmp(gauge_rgb(q, v), t->cue[v], 3);
    for (unsigned v = 0; ours && v < 4; v++)
      memcpy(gauge_rgb(q, v), t->native[v], 3);
    t->tinted = 0;
  }
}
/* Vertices 0 and 2 are the bar's left end, 1 and 3 its right end. */
static void tint_gauge(CueColor left, CueColor right) {
  for (unsigned slot = 0; slot < 2; slot++) {
    GaugeTint *t = &gauge_tints[slot];
    RenderGouraudQuad *q = &g_BattleAtbGauges[slot];
    for (unsigned v = 0; v < 4; v++) {
      CueColor c = v % 2 ? right : left;
      u8 *rgb = gauge_rgb(q, v);
      if (!t->tinted)
        memcpy(t->native[v], rgb, 3);
      rgb[0] = t->cue[v][0] = c.r;
      rgb[1] = t->cue[v][1] = c.g;
      rgb[2] = t->cue[v][2] = c.b;
    }
    t->tinted = 1;
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
  restore_gauge();
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
/* Bulwark phase right now: guarded below full AT, exposed on a full bar, off
 * whenever Aya is not free to move. */
static int bulwark_phase(void) {
  Combatant *core = pe_aya_core();
  return core ? pe_bulwark_phase(&session, core->hpAlive, FULL_AT,
                                 controllable())
              : PE_BW_OFF;
}
static int guarded(void) { return bulwark_phase() == PE_BW_GUARDED; }
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
  Combatant *core = pe_aya_core();
  if (core && session.style == PE_STYLE_BULWARK &&
      pe_bulwark_observe_hp(&session, (s16)core->curHP))
    pe_log("costume_style", "rally pool emptied by a heal hp=%d",
           (s16)core->curHP);
  frame_count++;
  if (pulse)
    pulse--;
  if (pe_pulse)
    pe_pulse--;
  return 0;
}
/* Rally "+N": orange, just outside the right edge of the native status panel
 * (84 px wide, full) on the HP row. Glyphs are the native HUD digit strip;
 * the "+" is two flat tiles. Position and color live here. */
enum {
  RALLY_X = 0x56, /* from g_HudOrigin.x; the panel ends at 0x54 */
  RALLY_Y = 11,   /* the HP digits' row */
  RALLY_DIGITS = 4
};
static const CueColor CUE_RALLY = {0xff, 0x90, 0x30};
void Draw_InitDrawModeSprtPacket(void *packet, int tpage);
static RenderPagedSpritePacket rally_digits[2][RALLY_DIGITS];
static RenderColorTilePacket rally_plus[2][2];
static int rally_ready;
static void rally_init(void) {
  int page = GetTPage(0, 1, 0x100, 0x1E0); /* as Battle_DrawHPBar */
  u16 clut = GetClut(0x130, 0x1F8);
  for (unsigned slot = 0; slot < 2; slot++) {
    for (unsigned i = 0; i < RALLY_DIGITS; i++) {
      RenderSpritePacket *s = &rally_digits[slot][i].sprite;
      Draw_InitDrawModeSprtPacket(&rally_digits[slot][i], page);
      s->clut = clut;
      s->width = 6;
      s->height = 0xA;
      s->v = 0xE8;
      BATTLE_SET_SPRITE_COLOR(*s, CUE_RALLY.r, CUE_RALLY.g, CUE_RALLY.b);
    }
    for (unsigned i = 0; i < 2; i++) {
      RenderColorTilePacket *t = &rally_plus[slot][i];
      SetTile(t);
      t->r0 = CUE_RALLY.r;
      t->g0 = CUE_RALLY.g;
      t->b0 = CUE_RALLY.b;
    }
  }
  rally_ready = 1;
}
static void draw_rally(void) {
  unsigned left = pe_bulwark_rally_left(&session), digits[RALLY_DIGITS], n = 0;
  unsigned slot = (unsigned)g_ActiveDrawSlot;
  if (!left || slot > 1)
    return;
  if (!rally_ready)
    rally_init();
  if (left > 9999)
    left = 9999;
  do {
    digits[n++] = left % 10;
    left /= 10;
  } while (left && n < RALLY_DIGITS);
  int x = g_HudOrigin.x + RALLY_X, y = g_HudOrigin.y + RALLY_Y;
  RenderColorTilePacket *bar = &rally_plus[slot][0], *post = &rally_plus[slot][1];
  bar->x0 = (u16)x;
  bar->y0 = (u16)(y + 4);
  bar->w = 5;
  bar->h = 1;
  post->x0 = (u16)(x + 2);
  post->y0 = (u16)(y + 2);
  post->w = 1;
  post->h = 5;
  AddPrim(D_800B0E38.ordering[slot] + 4, bar);
  AddPrim(D_800B0E38.ordering[slot] + 4, post);
  x += 7;
  for (unsigned i = n; i-- > 0; x += 6) {
    RenderSpritePacket *s = &rally_digits[slot][i].sprite;
    s->u = (u8)(digits[i] * 8);
    s->x = (s16)x;
    s->y = (s16)y;
    AddPrim(D_800B0E38.ordering[slot] + 4, &rally_digits[slot][i]);
  }
}
static int hud(void *ctx, void *user) {
  unsigned i;
  (void)ctx;
  (void)user;
  restore_marker();
  restore_gauge();
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
  draw_rally();
  /* Bulwark, one meaning per element: the AT bar FILL is the guard state
   * (blue guarded, pulsing red Exposed), the AT MARKER is orange while a
   * counter is loaded, and "+N" beside HP is the HP left to win back. */
  int phase = bulwark_phase();
  if (phase == PE_BW_GUARDED) {
    tint_gauge(CUE_GUARD_LOW, CUE_GUARD_HIGH);
  } else if (phase == PE_BW_EXPOSED) {
    unsigned t = frame_count % (2 * EXPOSED_PULSE_FRAMES);
    unsigned up = t < EXPOSED_PULSE_FRAMES ? t : 2 * EXPOSED_PULSE_FRAMES - t;
    CueColor high = CUE_EXPOSED_HIGH;
    high.r = (u8)(0xa0 + up * (0xff - 0xa0) / EXPOSED_PULSE_FRAMES);
    tint_gauge(CUE_EXPOSED_LOW, high);
    hint(HINT_EXPOSED, "Bulwark: red AT bar = exposed, you take 15% more "
                       "damage. Act now!");
  }
  /* AT marker. Cyan: a Quickstep refund landing. Orange: a Bulwark counter is
   * loaded. Red: Quickstep burst, faster and taking more damage. */
  CueColor c;
  if (pulse) {
    c = CUE_CYAN;
  } else if (session.style == PE_STYLE_BULWARK && session.bw.store) {
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
    int phase = bulwark_phase();
    before = *q->loss;
    /* Bulwark guard / Exposed or Quickstep Overextended, judged as the hit
     * resolves. */
    *q->loss = pe_style_damage(&session, *q->loss, phase);
    if (*q->loss != before && session.style == PE_STYLE_QUICKSTEP)
      pe_log("costume_style", "overextended loss=%d->%d burst=%u", before,
             *q->loss, session.qs.burst);
    /* Counter store and Rally pool take the HP actually lost from a guarded
     * hit, never more than Aya had. Exposed hits feed nothing. */
    int taken = *q->loss;
    if (q->current_hp >= 0 && taken > q->current_hp)
      taken = q->current_hp;
    pe_bulwark_absorb(&session, taken, phase);
    if (session.style == PE_STYLE_BULWARK && phase == PE_BW_GUARDED &&
        taken > 0)
      hint(HINT_GUARD, "Bulwark: blue AT bar = guarded, 25% less damage. "
                       "Orange marker = counter ready.");
    if (*q->loss != before && session.style == PE_STYLE_BULWARK)
      pe_log("costume_style", "bulwark %s loss=%d->%d store=%u pool=%u",
             phase == PE_BW_EXPOSED ? "exposed" : "guarded", before, *q->loss,
             session.bw.store, session.bw.pool);
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
    unsigned bonus;
    *q->damage = pe_bulwark_counter(&session, before, q->ordinary, &bonus);
    Combatant *aya = pe_aya_core();
    if (bonus && aya) {
      /* Rally: the boosted hit wins back HP equal to its bonus, up to the
       * pool's share and never above max HP. */
      unsigned heal = pe_bulwark_rally(&session, bonus, (s16)aya->curHP,
                                       (s16)aya->maxHP);
      aya->curHP = (u16)(aya->curHP + heal);
      hint(HINT_COUNTER, "Counter! Extra damage, and HP won back (orange +N "
                         "by your HP).");
      pe_log("costume_style",
             "counter damage=%d->%d store=%u rally=%u left=%u hp=%d", before,
             *q->damage, session.bw.store, heal,
             pe_bulwark_rally_left(&session), (s16)aya->curHP);
    }
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
/* Striker point-blank ring: a flat circle on the floor around Aya while she
 * aims a gun, drawn like the native targeting dome (same view matrix, screen
 * offset, ordering table and LINE_G2 packets) and growing with it. An enemy
 * whose body reaches the ring is point-blank. Radius:
 * pe_striker_ring_radius; color and smoothness here. */
enum { RING_SEGMENTS = 48, DOME_GROW_FRAMES = 10, OT_DEPTH_LIMIT = 0x1000 };
static const CueColor CUE_RING = {0x60, 0xe0, 0xff}; /* point-blank cyan */
static FieldLineG2Packet ring_lines[2][RING_SEGMENTS];
static unsigned ring_drawn; /* segments submitted last time (tests, logs) */
static void draw_point_blank_ring(void) {
  unsigned slot = (unsigned)g_ActiveDrawSlot;
  unsigned radius = pe_striker_ring_radius(&session);
  unsigned frame = (unsigned)g_BattleAttackAnimFrame;
  const GteMatrix *view = g_ViewMatrixSlot.value;
  int sxy[RING_SEGMENTS];
  long depth[RING_SEGMENTS];
  ring_drawn = 0;
  if (slot > 1 || !radius || !view || !g_PlayerEntity ||
      !g_OtBufferTable[slot])
    return;
  if (frame < DOME_GROW_FRAMES) /* the dome opens over 10 frames */
    radius = radius * frame / DOME_GROW_FRAMES;
  if (!radius)
    return;
  int cx = Pe1Fixed_Integer(&g_PlayerEntity->pos_x);
  int cy = Pe1Fixed_Integer(&g_PlayerEntity->pos_y); /* floor, as the dome */
  int cz = Pe1Fixed_Integer(&g_PlayerEntity->pos_z);
  SetRotMatrix(view);
  SetTransMatrix(view);
  Render_SetGteScreenOffset();
  for (unsigned i = 0; i < RING_SEGMENTS; i++) {
    int angle = (int)(i * 4096u / RING_SEGMENTS), p, flag;
    GteShortVector v = {(s16)(cx + rcos(angle) * (int)radius / 4096), (s16)cy,
                        (s16)(cz + rsin(angle) * (int)radius / 4096), 0};
    depth[i] = RotTransPers(&v, &sxy[i], &p, &flag);
  }
  Render_ResetGteScreenOffset();
  RenderNativeTag *ot = (RenderNativeTag *)g_OtBufferTable[slot] + 2;
  for (unsigned i = 0; i < RING_SEGMENTS; i++) {
    unsigned j = (i + 1) % RING_SEGMENTS;
    long z = (depth[i] + depth[j]) / 2;
    if (z <= 0 || z >= OT_DEPTH_LIMIT)
      continue; /* behind the camera or past the table, as the dome skips */
    FieldLineG2Packet *line = &ring_lines[slot][i];
    line->tag.length = 4;
    line->code = 0x50; /* LINE_G2 */
    line->r0 = line->r1 = CUE_RING.r;
    line->g0 = line->g1 = CUE_RING.g;
    line->b0 = line->b1 = CUE_RING.b;
    line->xy0 = (u32)sxy[i];
    line->xy1 = (u32)sxy[j];
    AddPrim(ot + z, line);
    ring_drawn++;
  }
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
  /* Aiming a gun as Striker: the point-blank ring, with the native dome. */
  if (session.style == PE_STYLE_STRIKER && normal_gun())
    draw_point_blank_ring();
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
  pe_bulwark_resolve(&session);
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
  restore_gauge();
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
  int phase = bulwark_phase();
  snprintf(out, size, "%s, counter %u, rally +%u",
           phase == PE_BW_GUARDED   ? "Guarded"
           : phase == PE_BW_EXPOSED ? "Exposed"
                                    : "Neither",
           session.bw.store, pe_bulwark_rally_left(&session));
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
                        "25% guard below full AT; guarded hits load a counter "
                        "that wins back up to half that HP; +15% damage taken "
                        "on a full bar. Striker: point-"
                        "blank guns +25%, outer 30% of gun range -20%. Channeler: weapon hits charge the next "
                        "support PE (1 = half, 2 = free); at 2, a hit cracks "
                        "it and a second breaks one.",
                        style_info);
  pe_menu_register_info(
      PE_PAGE_WARDROBE, "Style state",
      "Bulwark: guarded or exposed, stored counter damage and HP left to "
      "win back. Channeler: Spellweave charges. Half-bar features must be off "
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
