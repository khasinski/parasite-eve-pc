/* Combat styles: the native adapter. Definitions and simulation
 * (pe_style_*.c) have no dependency on game state.
 *
 * Its own mod (pe_styles), independent of the Wardrobe: the player picks a
 * style in F1 > PE Enhancements > Combat styles (Off by default). Costumes are
 * cosmetic; linking a costume to a style (pe_style_for_costume) is kept for a
 * later integration through a service. */
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
#include "pe_services.h"
#include "pe_style.h"
#include <stdio.h>
#include <string.h>
#define FULL_AT 9000u
#define PE_PAGE_STYLES "Combat styles" /* F1 > PE Enhancements > Combat styles */
static PeStyleSession session;
/* PeStyleId values. requested: the F1 choice (saved); enabled: the style that
 * applies, taken from requested outside battle only; locked: the encounter's. */
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
enum {
  HINT_GUARD = 1,
  HINT_EXPOSED = 2,
  HINT_COUNTER = 4,
  HINT_CHARGE = 8,
  HINT_FULL = 16,
  HINT_CRACK = 32,
  HINT_REFUND = 64,
  HINT_GOLD = 128,
  HINT_POINT_BLANK = 256,
  HINT_FAR = 512
};
/* Channeler PE bar fill: blue at 1 charge (half price), bright cyan when
 * full (free), flickering orange once cracked. */
static const CueColor CUE_HALF_LOW = {0x10, 0x40, 0x90};
static const CueColor CUE_HALF_HIGH = {0x40, 0x90, 0xff};
static const CueColor CUE_FREE_LOW = {0x30, 0x90, 0xc0};
static const CueColor CUE_FREE_HIGH = {0xb0, 0xff, 0xff};
static const CueColor CUE_CRACK_LOW = {0x80, 0x38, 0x00};
static unsigned hints_shown;
static void hint(unsigned bit, const char *text) {
  if (hints_shown & bit)
    return;
  hints_shown |= bit;
  pe_toast(text);
}
/* Quickstep AT bar fill: a bright flash when a refund lands, then pulsing
 * orange while rushing (faster, and taking more damage). */
static const CueColor CUE_REFUND_LOW = {0x80, 0xff, 0xff};
static const CueColor CUE_REFUND_HIGH = {0xff, 0xff, 0xff};
static const CueColor CUE_RUSH_LOW = {0x70, 0x20, 0x00};
static const CueColor CUE_RUSH_HIGH = {0xff, 0x80, 0x20}; /* g pulses */
/* Quickstep target marker: gold while that enemy can still give its half-HP
 * refund (above half). Positive, and unlike the native dark out-of-range
 * marker. */
static const CueColor CUE_GOLD = {0xff, 0xd0, 0x30};
static const CueColor CUE_CRACK = {0xff, 0x90, 0x30}; /* Channeler cracked */
/* Striker far band (-20%): muted purple, clearly apart from the native
 * in-range red (96,14,14) and out-of-range dark red (32,0a,0a). */
static const CueColor CUE_FAR = {0x90, 0x60, 0xd0};
/* The native in-range target marker red the falloff shading starts from. */
static const CueColor CUE_NATIVE_TARGET = {0x96, 0x14, 0x14};
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
/* Bar fill tints (Bulwark: AT bar, Channeler: PE bar), both draw buffers;
 * restored only if nothing else changed the colors since. */
typedef struct {
  u8 native[4][3], cue[4][3];
  int tinted;
} GaugeTint;
enum { BAR_AT, BAR_PE, BAR_COUNT };
static GaugeTint gauge_tints[BAR_COUNT][2];
static RenderGouraudQuad *bar_quad(unsigned bar, unsigned slot) {
  return bar == BAR_AT ? &g_BattleAtbGauges[slot] : &g_BattlePeGauges[slot][0];
}
static u8 *gauge_rgb(RenderGouraudQuad *q, unsigned v) {
  return v == 0 ? &q->r0 : v == 1 ? &q->r1 : v == 2 ? &q->r2 : &q->r3;
}
static void restore_gauge(void) {
  for (unsigned bar = 0; bar < BAR_COUNT; bar++)
    for (unsigned slot = 0; slot < 2; slot++) {
      GaugeTint *t = &gauge_tints[bar][slot];
      RenderGouraudQuad *q = bar_quad(bar, slot);
      int ours = t->tinted;
      for (unsigned v = 0; ours && v < 4; v++)
        ours = !memcmp(gauge_rgb(q, v), t->cue[v], 3);
      for (unsigned v = 0; ours && v < 4; v++)
        memcpy(gauge_rgb(q, v), t->native[v], 3);
      t->tinted = 0;
    }
}
/* Vertices 0 and 2 are the bar's left end, 1 and 3 its right end. */
static void tint_bar(unsigned bar, CueColor left, CueColor right) {
  for (unsigned slot = 0; slot < 2; slot++) {
    GaugeTint *t = &gauge_tints[bar][slot];
    RenderGouraudQuad *q = bar_quad(bar, slot);
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
/* A saved choice that is out of range or not built yet means Off. */
static int valid_style(int id) {
  return id > PE_STYLE_ORIGINAL && id < PE_STYLE_COUNT &&
                 pe_style_definition((PeStyleId)id)->implemented
             ? id
             : PE_STYLE_ORIGINAL;
}
int pe_styles_enabled(void) {
  return enabled != PE_STYLE_ORIGINAL || valid_style(requested);
}
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
      pe_toast("Change the combat style outside battle");
    }
  } else {
    enabled = valid_style(requested);
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
static void ensure_session(void) {
  PeStyleId id;
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
  id = (PeStyleId)enabled;
  if (id == PE_STYLE_BULWARK && partial_actions()) {
    id = PE_STYLE_ORIGINAL;
    pe_toast("Bulwark needs half-bar actions off; using vanilla");
  }
  pe_style_begin(&session, id);
  action_target_count = 0;
  pe_log("costume_style", "begin=%s enabled=%d chosen=%s",
         pe_style_definition(session.style)->key, enabled,
         pe_style_definition((PeStyleId)valid_style(requested))->key);
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
/* Channeler charge count "1/2" / "2/2": cyan (orange flicker when cracked),
 * same column as Rally "+N", on the PE bar's row. */
enum { CHARGE_Y = 0x15, CHARGE_GLYPHS = 3, GLYPH_SLASH = 10 };
static RenderPagedSpritePacket rally_digits[2][RALLY_DIGITS];
static RenderPagedSpritePacket charge_glyphs[2][CHARGE_GLYPHS];
static RenderColorTilePacket rally_plus[2][2];
static int rally_ready;
/* One native HUD digit-strip glyph (0-9, 10 = '/'), as the HP digits. */
static void init_glyph(RenderPagedSpritePacket *p, int page, u16 clut,
                       CueColor c) {
  Draw_InitDrawModeSprtPacket(p, page);
  p->sprite.clut = clut;
  p->sprite.width = 6;
  p->sprite.height = 0xA;
  p->sprite.v = 0xE8;
  BATTLE_SET_SPRITE_COLOR(p->sprite, c.r, c.g, c.b);
}
static void rally_init(void) {
  int page = GetTPage(0, 1, 0x100, 0x1E0); /* as Battle_DrawHPBar */
  u16 clut = GetClut(0x130, 0x1F8);
  for (unsigned slot = 0; slot < 2; slot++) {
    for (unsigned i = 0; i < RALLY_DIGITS; i++)
      init_glyph(&rally_digits[slot][i], page, clut, CUE_RALLY);
    for (unsigned i = 0; i < CHARGE_GLYPHS; i++)
      init_glyph(&charge_glyphs[slot][i], page, clut, CUE_CYAN);
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
static void draw_charges(void) {
  unsigned slot = (unsigned)g_ActiveDrawSlot, have = session.ch.charges;
  unsigned max = pe_style_definition(PE_STYLE_CHANNELER)->channeler.max_charges;
  if (session.style != PE_STYLE_CHANNELER || !have || slot > 1 || max > 9)
    return;
  if (!rally_ready)
    rally_init();
  CueColor c = session.ch.cracked &&
                       (frame_count / CRACK_FLICKER_FRAMES) % 2 == 0
                   ? CUE_CRACK
                   : CUE_CYAN;
  unsigned glyph[CHARGE_GLYPHS] = {have, GLYPH_SLASH, max};
  int x = g_HudOrigin.x + RALLY_X, y = g_HudOrigin.y + CHARGE_Y;
  for (unsigned i = 0; i < CHARGE_GLYPHS; i++) {
    RenderSpritePacket *s = &charge_glyphs[slot][i].sprite;
    s->u = (u8)(glyph[i] * 8);
    s->x = (s16)(x + 6 * (int)i);
    s->y = (s16)y;
    BATTLE_SET_SPRITE_COLOR(*s, c.r, c.g, c.b);
    AddPrim(D_800B0E38.ordering[slot] + 4, &charge_glyphs[slot][i]);
  }
}
static int hud(void *ctx, void *user) {
  unsigned i;
  (void)ctx;
  (void)user;
  restore_marker();
  restore_gauge();
  restore_pe_cue();
  /* Channeler, one meaning per element: the PE bar FILL is what your next
   * support power costs (blue = half price, bright cyan = free, orange
   * flicker = cracked, the next hit loses a charge), and "1/2" or "2/2"
   * beside the PE bar is the charge count. */
  if (session.style == PE_STYLE_CHANNELER && session.ch.charges) {
    if (session.ch.cracked && (frame_count / CRACK_FLICKER_FRAMES) % 2 == 0)
      tint_bar(BAR_PE, CUE_CRACK_LOW, CUE_CRACK);
    else if (pe_channeler_full(&session))
      tint_bar(BAR_PE, CUE_FREE_LOW, CUE_FREE_HIGH);
    else
      tint_bar(BAR_PE, CUE_HALF_LOW, CUE_HALF_HIGH);
  }
  draw_charges();
  draw_rally();
  /* Bulwark, one meaning per element: the AT bar FILL is the guard state
   * (blue guarded, pulsing red Exposed), the AT MARKER is orange while a
   * counter is loaded, and "+N" beside HP is the HP left to win back. */
  int phase = bulwark_phase();
  if (phase == PE_BW_GUARDED) {
    tint_bar(BAR_AT, CUE_GUARD_LOW, CUE_GUARD_HIGH);
  } else if (phase == PE_BW_EXPOSED) {
    unsigned t = frame_count % (2 * EXPOSED_PULSE_FRAMES);
    unsigned up = t < EXPOSED_PULSE_FRAMES ? t : 2 * EXPOSED_PULSE_FRAMES - t;
    CueColor high = CUE_EXPOSED_HIGH;
    high.r = (u8)(0xa0 + up * (0xff - 0xa0) / EXPOSED_PULSE_FRAMES);
    tint_bar(BAR_AT, CUE_EXPOSED_LOW, high);
    hint(HINT_EXPOSED, "Bulwark: red AT bar = exposed, you take 15% more "
                       "damage. Act now!");
  }
  /* Quickstep: the AT bar FILL flashes bright as a refund lands, then pulses
   * orange while rushing (faster, and taking 20% more damage). */
  if (session.style == PE_STYLE_QUICKSTEP) {
    if (pulse) {
      tint_bar(BAR_AT, CUE_REFUND_LOW, CUE_REFUND_HIGH);
    } else if (pe_quickstep_burst(&session)) {
      unsigned t = frame_count % (2 * EXPOSED_PULSE_FRAMES);
      unsigned up = t < EXPOSED_PULSE_FRAMES ? t : 2 * EXPOSED_PULSE_FRAMES - t;
      CueColor high = CUE_RUSH_HIGH;
      high.g = (u8)(0x40 + up * (0xa0 - 0x40) / EXPOSED_PULSE_FRAMES);
      tint_bar(BAR_AT, CUE_RUSH_LOW, high);
    }
  }
  /* AT marker: orange only while a Bulwark counter is loaded. */
  CueColor c;
  if (session.style == PE_STYLE_BULWARK && session.bw.store) {
    c = CUE_COUNTER;
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
      if (backlash == PE_CH_CRACK)
        hint(HINT_CRACK, "Cracked! One more hit loses a charge. Cast a "
                         "support PE to keep it.");
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
/* Striker damage percent for one recipient, the same call for preview and
 * snapshot. 100 = native. */
static unsigned striker_percent(const BattleEntity *e) {
  BattleEntity *p = pe_player();
  if (!p || !candidate(e))
    return 100;
  unsigned radius =
      e->moveSpeed
          ? (unsigned)(u16)e->renderObject.hit_cylinder.radius * e->moveSpeed /
                4096u
          : 0u;
  return pe_striker_shot_percent(
      &session, point_blank(e), radius,
      e->renderObject.target_x - p->posX.parts.integer,
      e->renderObject.target_z - p->posZ.parts.integer, native_range());
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
        pe_log("costume_style", "striker damage=%d->%d recipient=%u pct=%u",
               before, *q->damage, i, pe_striker_target_percent(&session, i));
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
      /* Cyan at close range. Past the ring the marker shades from the native
       * red toward purple as the damage penalty grows. Beyond range the
       * native dark marker is left alone (it also says "may miss"). */
      if (q->out_of_range || !normal_gun())
        return 0;
      unsigned percent = striker_percent(e);
      unsigned max_penalty =
          pe_style_definition(PE_STYLE_STRIKER)->striker.falloff_max_penalty;
      if (percent > 100) {
        c = CUE_CYAN;
        hint(HINT_POINT_BLANK, "Striker: cyan target = close range, +25% gun "
                               "damage. Reach the cyan ring.");
      } else if (percent < 100 && max_penalty) {
        unsigned t = 100 - percent; /* 1..max_penalty */
        c.r = (u8)(CUE_NATIVE_TARGET.r +
                   ((int)CUE_FAR.r - CUE_NATIVE_TARGET.r) * (int)t /
                       (int)max_penalty);
        c.g = (u8)(CUE_NATIVE_TARGET.g +
                   ((int)CUE_FAR.g - CUE_NATIVE_TARGET.g) * (int)t /
                       (int)max_penalty);
        c.b = (u8)(CUE_NATIVE_TARGET.b +
                   ((int)CUE_FAR.b - CUE_NATIVE_TARGET.b) * (int)t /
                       (int)max_penalty);
        hint(HINT_FAR, "Striker: the further the target, the less damage, "
                       "down to -25%. Purple = far.");
      } else {
        return 0;
      }
    } else {
      /* Quickstep: gold while this enemy can still give its half-HP refund;
       * native once spent. Out of range the native dark marker stays. */
      const EnemyCombatant *foe = e->core;
      if (q->out_of_range || !candidate(e) ||
          !pe_quickstep_available(&session, foe, foe->hpAlive, foe->maxHp))
        return 0;
      c = CUE_GOLD;
      hint(HINT_GOLD, "Quickstep: gold target = above half HP. Knock it below "
                      "half for +AT and speed.");
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
        pe_striker_target(&session, i, striker_percent(e));
        pe_log("costume_style",
               "striker range recipient=%u dx=%d dz=%d radius=%u scale=%u "
               "tx=%d tz=%d range=%u pct=%u",
               i,
               e->renderObject.hit_cylinder.value0 -
                   pe_player()->posX.parts.integer,
               e->renderObject.hit_cylinder.value2 -
                   pe_player()->posZ.parts.integer,
               (unsigned)(u16)e->renderObject.hit_cylinder.radius,
               (unsigned)e->moveSpeed,
               e->renderObject.target_x - pe_player()->posX.parts.integer,
               e->renderObject.target_z - pe_player()->posZ.parts.integer,
               native_range(), pe_striker_target_percent(&session, i));
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
    hint(HINT_REFUND, "Quickstep: +AT and 3 s of speed! Orange AT bar = "
                      "rushing: you take 20% more damage.");
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
      if (pe_channeler_full(&session))
        hint(HINT_FULL, "Fully charged: your next support PE is free. A hit "
                        "now cracks the charge (orange).");
      else
        hint(HINT_CHARGE, "Channeler: hits charge your next support PE. "
                          "Blue PE bar = half price, bright = free.");
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
/* What the F1 choice means right now: in battle the encounter's style, else
 * the chosen one (or why it falls back). */
static void style_info(char *out, unsigned size) {
  int chosen = valid_style(requested);
  if (pe_in_battle() && session.active) {
    snprintf(out, size, "%s (this battle)",
             session.style == PE_STYLE_ORIGINAL
                 ? "Off"
                 : pe_style_definition(session.style)->name);
    return;
  }
  if (chosen == PE_STYLE_ORIGINAL) {
    snprintf(out, size, "Off: vanilla combat");
    return;
  }
  if (chosen == PE_STYLE_BULWARK && partial_actions()) {
    snprintf(out, size, "Off (turn half-bar actions off for Bulwark)");
    return;
  }
  snprintf(out, size, "%s, from the next battle",
           pe_style_definition((PeStyleId)chosen)->name);
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
static const PeStyleDef *chosen_style(void) {
  return pe_style_definition((PeStyleId)valid_style(requested));
}
static void chosen_benefit(char *out, unsigned size) {
  const PeStyleDef *d = chosen_style();
  snprintf(out, size, "%s",
           d->id == PE_STYLE_ORIGINAL ? "Vanilla combat" : d->benefit);
}
static void chosen_tradeoff(char *out, unsigned size) {
  const PeStyleDef *d = chosen_style();
  snprintf(out, size, "%s", d->id == PE_STYLE_ORIGINAL ? "-" : d->limitation);
}
static void all_styles(char *out, unsigned size) {
  snprintf(out, size, "Quickstep, Bulwark, Striker, Channeler");
}
/* Choice names in PeStyleId order, so the saved index is the style. */
static const char *const style_names[PE_STYLE_COUNT] = {
    "Off", "Quickstep", "Bulwark", "Striker", "Channeler"};
void pe_styles_register(const char *mod_id) {
  pe_menu_register_choice(
      PE_PAGE_STYLES, "Style",
      "Off (default): vanilla combat. Pick a prototype combat style; it "
      "applies from the next battle. Change outside battle only.",
      &requested, style_names, PE_STYLE_COUNT);
  pe_menu_register_tick(settings_tick);
  pe_menu_register_info(PE_PAGE_STYLES, "Now", "Which style applies.",
                        style_info);
  pe_menu_register_info(PE_PAGE_STYLES, "Benefit",
                        "What the chosen style gives you.", chosen_benefit);
  pe_menu_register_info(PE_PAGE_STYLES, "Tradeoff",
                        "The chosen style's challenge.", chosen_tradeoff);
  pe_menu_register_info(PE_PAGE_STYLES, "All styles",
                        "Quickstep: half-HP breaks and kills +20% AT, 3 s "
                        "speed, +20% damage taken meanwhile. Bulwark: "
                        "25% guard below full AT; guarded hits load a counter "
                        "that wins back up to half that HP; +15% damage taken "
                        "on a full bar. Striker: close-range guns +25%; past "
                        "the ring damage drops, -25% at max range. Channeler: "
                        "weapon hits charge the next support PE (1 = half, "
                        "2 = free); at 2, a hit cracks it and a second breaks "
                        "one.",
                        all_styles);
  pe_menu_register_info(
      PE_PAGE_STYLES, "Battle state",
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
