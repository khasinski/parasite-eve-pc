/* Half-bar ATB and Quick powers, ported from the recomp's pe_atb_items.c.
 * Uses native Combatant, action queue, input flags and HUD quads. PE balance
 * owns the settings rows. Battle-only bookkeeping is reset across loads and
 * scenes; no transient state is added to the memory-card save format.
 * Cancels refund the borrowed gauge; full-cost powers/attacks incur debt.
 */
#include <stdint.h>
#include <string.h>
#include "pe_game.h"
#include "pe_mod_host.h"
#include "pe_atb_items.h"
#include "pe1/menu_state.h"
#include "pe1/battle_status.h"
#define PE_AYA_SLOT_COUNT 50
#define PE_GAUGE_FULL 9000u
#define PE_GAUGE_HALF 4500u
#define PRESS_ITEM 0x0080u
#define PRESS_ATTACK 0x0200u
#define PRESS_CANCEL 0x2000u
/* Battle-menu actions, in tab-bit order. */
enum { ACT_ITEM, ACT_PE, ACT_WEAPON, ACT_ARMOR, ACT_ESCAPE, ACT_COUNT,
       ACT_NONE = -1, ACT_ATTACK = -2, ACT_OTHER = -3 };

/* Settings (F1 > Items & Parasite Energy, pe_pe_balance.c). */
int g_pe_half_enabled = 1;                      /* master switch */
int g_pe_half_allow[ACT_COUNT] = { 1, 1, 1, 0, 0 };
uint32_t g_pe_quick_mask = 0x59Bu;              /* Heal 1/2, Detox, Medic, Scan, Slow, Confuse */
#define s_enabled g_pe_half_enabled
#define s_allow g_pe_half_allow

/* Transient native battle bookkeeping; never written to a card save. */
static struct {
    int borrowed, borrow_pending, turn_active, track_full;
    unsigned saved_gauge;
    unsigned saved_rate, last_rate;              /* fill rate at the borrow / last nonzero */
    int action;                                  /* most significant action this turn */
    int tab_saved;
    uint32_t saved_tabs;
    uint16_t bag[PE_AYA_SLOT_COUNT];
    uint8_t weapon_slot, armor_slot;
    uint8_t normal[2][4][3];                     /* the game's bar colours */
    int have_normal, tinted;
    int pe_index;                                /* the PE power queued this turn, or -1 */
    unsigned debt, last_gauge;                   /* half bar owed by a full power cast early */
} s_st;


/* ---- half-bar indicator ----------------------------------------------------
 * Native ATB Gouraud quads, one for each draw buffer. The game only rewrites
 * them while the bar is full (its flash), so between half and full the mod
 * tints the bar gold, and puts the game's own colours back below half. */
static const uint8_t s_gold[4][3] = {
    {0x60, 0x40, 0}, {0xFF, 0xD8, 0x40},
    {0x60, 0x40, 0}, {0xFF, 0xD8, 0x40},
};
static void pe_bar_colors(int save, const uint8_t (*src)[3]) {
    for (int slot = 0; slot < 2; ++slot) {
        RenderGouraudQuad *q = &g_BattleAtbGauges[slot];
        uint8_t *colors[4][3] = {
            {&q->r0, &q->g0, &q->b0}, {&q->r1, &q->g1, &q->b1},
            {&q->r2, &q->g2, &q->b2}, {&q->r3, &q->g3, &q->b3},
        };
        for (int v = 0; v < 4; ++v)
            for (int c = 0; c < 3; ++c)
                if (save) s_st.normal[slot][v][c] = *colors[v][c];
                else *colors[v][c] = src ? src[v][c] : s_st.normal[slot][v][c];
    }
}
static int pe_any_allowed(void) {
    for (int a = 0; a < ACT_COUNT; ++a) if (s_allow[a]) return 1;
    return 0;
}

static void pe_update_indicator(unsigned gauge) {
    const int want = s_enabled && pe_any_allowed() &&
                     gauge >= PE_GAUGE_HALF && gauge < PE_GAUGE_FULL;
    if (want) {
        if (!s_st.tinted && !s_st.have_normal) { pe_bar_colors(1, 0); s_st.have_normal = 1; }
        pe_bar_colors(0, s_gold);
        s_st.tinted = 1;
    } else if (s_st.tinted) {
        if (gauge < PE_GAUGE_FULL && s_st.have_normal) pe_bar_colors(0, 0); /* full: game flashes */
        s_st.tinted = 0;
    } else if (gauge < PE_GAUGE_HALF) {
        pe_bar_colors(1, 0);                     /* remember the game's colours */
        s_st.have_normal = 1;
    }
}

/* ---- turn bookkeeping ------------------------------------------------------ */
/* Item counts, ignoring order (Move doesn't count as using an item). */
static void pe_bag_read(uint16_t *out) {
    for (int i = 0; i < PE_AYA_SLOT_COUNT; ++i)
        out[i] = g_AyaInventoryItems[i];
}

static int pe_bag_contents_changed(void) {
    uint16_t now[PE_AYA_SLOT_COUNT];
    pe_bag_read(now);
    for (int i = 0; i < PE_AYA_SLOT_COUNT; ++i) {
        int before = 0, after = 0;
        for (int j = 0; j < PE_AYA_SLOT_COUNT; ++j) {
            before += s_st.bag[j] == s_st.bag[i];
            after += now[j] == s_st.bag[i];
        }
        if (before != after) return 1;
    }
    return 0;
}

static void pe_snapshot(void) {
    pe_bag_read(s_st.bag);
    s_st.weapon_slot = g_AyaSaveState.equipped_weapon_slot;
    s_st.armor_slot = g_AyaSaveState.equipped_armor_slot;
    s_st.action = ACT_NONE;
    s_st.pe_index = -1;
}

static int pe_classify(unsigned command) {
    if (command >= 1 && command <= 2) return ACT_ATTACK;
    if (command >= 3 && command < 0x183) return ACT_ITEM;
    if (command >= 0x183 && command < 0x197) return ACT_PE;
    if (command >= 0x197 && command < 0x199)
        return g_AyaSaveState.equipped_armor_slot != s_st.armor_slot ? ACT_ARMOR : ACT_WEAPON;
    if (command >= 0x199) return ACT_ESCAPE;
    return ACT_OTHER;
}

/* Fold in the queue and the equipment slots. Attacks win over everything. */
static void pe_note_actions(void) {
    const unsigned count = g_BattleActionQueueCount;
    for (unsigned i = 0; i < count && i < 45; ++i) {
        const unsigned command = (u16)g_BattleActionQueue[i].command;
        const int act = pe_classify(command);
        if (act == ACT_PE) s_st.pe_index = (int)(command - 0x183u);
        if (act == ACT_ATTACK || s_st.action == ACT_NONE) s_st.action = act;
    }
    if (s_st.action == ACT_NONE) {
        if (g_AyaSaveState.equipped_weapon_slot != s_st.weapon_slot) s_st.action = ACT_WEAPON;
        else if (g_AyaSaveState.equipped_armor_slot != s_st.armor_slot) s_st.action = ACT_ARMOR;
        else if (pe_bag_contents_changed()) s_st.action = ACT_ITEM;
    }
}

static int pe_action_allowed(int act) {
    if (act == ACT_PE)                           /* only the quick powers */
        return s_allow[ACT_PE] && s_st.pe_index >= 0 && s_st.pe_index < 20 &&
               ((g_pe_quick_mask >> s_st.pe_index) & 1u);
    return act >= 0 && act < ACT_COUNT && s_allow[act];
}

static void pe_log_pe_turn(const char *cost, unsigned from, unsigned to) {
    if (s_st.pe_index < 0 || s_st.pe_index >= 20) return;   /* a PE power was queued (Energy Shot shoots) */
    pe_log("pe_turn", "power=%s cost=%s gauge=%u->%u", pe_power_tag(s_st.pe_index), cost, from, to);
}

static void pe_restore_tabs(void) {
    if (s_st.tab_saved) g_MenuItemContextFlag = s_st.saved_tabs;
    s_st.tab_saved = 0;
}

static void pe_reset(void) {
    pe_restore_tabs();
    s_st.borrowed = s_st.borrow_pending = s_st.turn_active = s_st.track_full = 0;
}

/* A gauge below full must be filling: put back a zero rate (see 4. above). */
static void pe_fix_rate(Combatant *core, unsigned preferred) {

    const unsigned rate = core->scaledOffense;
    if (rate) { s_st.last_rate = rate; return; }
    if (core->hpAlive >= PE_GAUGE_FULL) return;
    if (core->stateFlags & 0x10000u) return;
    unsigned fix = preferred ? preferred : s_st.last_rate;
    if (!fix) fix = core->stat22 * 4u / 10u;   /* a gun's scale */
    if (!fix) fix = 1;
    core->scaledOffense = (u16)fix;
}

/* ---- the hook --------------------------------------------------------------- */
static void pe_atb_tick(void) {

    if (!pe_in_battle()) {
        pe_reset();
        s_st.tinted = s_st.have_normal = 0;     /* HUD packets are rebuilt per battle */
        s_st.debt = 0;
        return;
    }
    Combatant *core = pe_aya_core();
    if (!core) return;
    const uint32_t phase = g_BattleModeState;
    pe_update_indicator(s_st.borrowed ? s_st.saved_gauge : core->hpAlive);
    /* Vanilla mode must not repair or otherwise change the stock gauge. */
    if (!s_enabled && !s_st.borrowed && !s_st.borrow_pending &&
        !s_st.turn_active && !s_st.debt) return;

    if (phase == 1) {
        if (!s_st.turn_active) {
            s_st.turn_active = 1;
            s_st.track_full = !s_st.borrowed && s_enabled;   /* a normal full-bar turn */
            if (s_st.track_full) pe_snapshot();
        }
        s_st.borrow_pending = 0;
        if (s_st.borrowed || s_st.track_full) pe_note_actions();
        /* Menu closed without doing anything: end the borrowed turn instead
         * of letting the borrowed bar be spent on shots. */
        if (s_st.borrowed && s_st.action == ACT_NONE &&
            g_BattleItemMenuState == 0) {
            const uint32_t pressed = g_FieldPadPressed;
            g_FieldPadPressed = (pressed | PRESS_CANCEL) & ~PRESS_ATTACK;
        }
        return;
    }
    if (phase != 0) return;

    if (s_st.turn_active) {                             /* the turn just ended */
        if (s_st.borrowed || s_st.track_full) pe_note_actions();
        if (s_st.borrowed) {
            unsigned gauge = 0;
            if (s_st.action == ACT_NONE) gauge = s_st.saved_gauge;
            else if (pe_action_allowed(s_st.action))
                gauge = s_st.saved_gauge > PE_GAUGE_HALF ? s_st.saved_gauge - PE_GAUGE_HALF : 0;
            else if (s_st.action == ACT_PE || s_st.action == ACT_ATTACK) {
                /* A full power from a half bar, or anything that shoots (Energy Shot and
                 * Liberate are queued as PE but fire as attacks): the rest of the bar is owed. */
                const unsigned paid = s_st.saved_gauge;
                s_st.debt = paid < PE_GAUGE_FULL ? PE_GAUGE_FULL - paid : 0;
            }
            core->hpAlive = (u16)gauge;
            pe_log_pe_turn(pe_action_allowed(s_st.action) ? "half" : "full_debt", s_st.saved_gauge, gauge);
        } else if (s_st.track_full && pe_action_allowed(s_st.action)) {
            core->hpAlive = (u16)PE_GAUGE_HALF;
            pe_log_pe_turn("half", PE_GAUGE_FULL, PE_GAUGE_HALF);
        } else if (s_st.track_full) {
            pe_log_pe_turn("full", PE_GAUGE_FULL, 0);
        }
        s_st.last_gauge = core->hpAlive;
        pe_fix_rate(core, s_st.borrowed && s_st.action == ACT_NONE ? s_st.saved_rate : 0);
        pe_reset();
        return;
    }
    if (s_st.borrow_pending) {                          /* the game didn't take it */
        core->hpAlive = (u16)s_st.saved_gauge;
        pe_fix_rate(core, s_st.saved_rate);
        pe_reset();
        return;
    }
    pe_fix_rate(core, 0);                               /* every free moment */
    {                                                   /* a debt is paid from the refill first */
        unsigned g = core->hpAlive;
        if (s_st.debt && g > s_st.last_gauge) {
            unsigned take = g - s_st.last_gauge;
            if (take > s_st.debt) take = s_st.debt;
            g -= take;
            s_st.debt -= take;
            core->hpAlive = (u16)g;
        }
        s_st.last_gauge = g;
    }
    if (!s_enabled || !pe_any_allowed()) return;

    const unsigned gauge = core->hpAlive;
    const uint32_t pressed = g_FieldPadPressed;
    if ((pressed & PRESS_ITEM) && !(pressed & PRESS_ATTACK) &&
        gauge >= PE_GAUGE_HALF && gauge < PE_GAUGE_FULL) {
        s_st.saved_gauge = gauge;
        s_st.saved_rate = core->scaledOffense;
        pe_snapshot();
        s_st.saved_tabs = g_MenuItemContextFlag;
        s_st.tab_saved = 1;
        uint32_t tabs = s_st.saved_tabs;
        for (int a = 0; a < ACT_COUNT; ++a)
            if (!s_allow[a]) tabs &= ~(1u << a);
        if (!(tabs & 0x1Fu)) { s_st.tab_saved = 0; return; }
        g_MenuItemContextFlag = tabs;
        core->hpAlive = (u16)PE_GAUGE_FULL;
        g_BattleGaugeCapLatch = 1;
        s_st.borrowed = s_st.borrow_pending = 1;
    }
}

/* On a borrowed (half) bar only the quick powers can be picked; the rest need a full bar and
 * show dimmed, as a power Aya can't afford (the game refuses it with its error sound). */
static int pe_power_available(void *ctx, void *user) {
    PeHookPowerAvailable *p = ctx;
    (void)user;
    if (s_st.borrowed && pe_in_battle() && *p->enabled && p->power >= 0 && p->power < 20 &&
        !(s_allow[ACT_PE] && ((g_pe_quick_mask >> p->power) & 1u)))
        *p->enabled = 0;
    return 0;
}

static int pe_atb_update(void *ctx, void *user) {
    (void)ctx; (void)user;
    pe_atb_tick();
    return 0;
}
static int pe_atb_loaded(void *ctx, void *user) {
    (void)ctx; (void)user;
    memset(&s_st, 0, sizeof s_st);
    return 0;
}
static int pe_atb_scene(void *ctx, void *user) {
    pe_reset();
    return pe_atb_loaded(ctx, user);
}
static int pe_atb_frame(void *ctx, void *user) {
    if (!pe_in_battle()) pe_atb_scene(ctx, user);
    return 0;
}
void pe_atb_items_shutdown(void) {
    if (s_st.tinted && s_st.have_normal) pe_bar_colors(0, NULL);
    pe_reset();
}
void pe_atb_items_register(const char *mod_id) {
    pe_hook_add(PE_HOOK_BATTLE_UPDATE, pe_atb_update, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_POWER_AVAILABLE, pe_power_available, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_FRAME, pe_atb_frame, NULL, PE_PRIORITY_LATE, mod_id);
    pe_hook_add(PE_HOOK_SCENE_START, pe_atb_scene, NULL, PE_PRIORITY_NORMAL, mod_id);
    pe_hook_add(PE_HOOK_SAVE_READ, pe_atb_loaded, NULL, PE_PRIORITY_NORMAL, mod_id);
}

#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_checkpoint_rebalance.h"
int pe_rb_atb_walk(const unsigned char*in,unsigned char*out,int apply){if((!in&&!out)||(0))return 0;if(apply&&(!in||!pe_rb_atb_walk(in,0,0)))return 0;PeCheckpointWire w={in,out,0,apply,1};
PE_CK_S(w,g_pe_half_enabled,0,1,1);for(int i=0;i<ACT_COUNT;i++)PE_CK_S(w,g_pe_half_allow[i],0,1,1);PE_CK_U(w,g_pe_quick_mask,0,0xfffff,1);PE_CK_S(w,s_st.borrowed,0,1,0);PE_CK_S(w,s_st.borrow_pending,0,1,0);PE_CK_S(w,s_st.turn_active,0,1,0);PE_CK_S(w,s_st.track_full,0,1,0);PE_CK_S(w,s_st.tab_saved,0,1,0);PE_CK_S(w,s_st.have_normal,0,1,0);PE_CK_S(w,s_st.tinted,0,1,0);PE_CK_U(w,s_st.saved_gauge,0,65535,0);PE_CK_U(w,s_st.saved_rate,0,65535,0);PE_CK_U(w,s_st.last_rate,0,65535,0);PE_CK_U(w,s_st.saved_tabs,0,UINT32_MAX,0);PE_CK_U(w,s_st.debt,0,9000,0);PE_CK_U(w,s_st.last_gauge,0,65535,0);PE_CK_S(w,s_st.action,ACT_OTHER,ACT_COUNT-1,0);PE_CK_S(w,s_st.pe_index,-1,19,0);for(int i=0;i<50;i++)PE_CK_U(w,s_st.bag[i],0,65535,0);PE_CK_U(w,s_st.weapon_slot,0,255,0);PE_CK_U(w,s_st.armor_slot,0,255,0);for(int i=0;i<2;i++)for(int j=0;j<4;j++)for(int k=0;k<3;k++)PE_CK_U(w,s_st.normal[i][j][k],0,255,0);
PE_CK_U(w,g_MenuItemContextFlag,0,UINT32_MAX,0);
/* These live HUD byte fields retain their original packet/tag ownership. */
for(int slot=0;slot<2;slot++){RenderGouraudQuad*q=&g_BattleAtbGauges[slot];uint8_t*colors[]={&q->r0,&q->g0,&q->b0,&q->r1,&q->g1,&q->b1,&q->r2,&q->g2,&q->b2,&q->r3,&q->g3,&q->b3};for(unsigned i=0;i<12;i++)PE_CK_U(w,*colors[i],0,255,0);}
return pe_rb_finish(&w);}
#endif
