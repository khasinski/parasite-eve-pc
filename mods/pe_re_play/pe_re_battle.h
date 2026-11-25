#ifndef PE_RE_BATTLE_H
#define PE_RE_BATTLE_H

#include "pe_re_battle_rules.h"

typedef enum {
    PE_RE_BATTLE_IDLE, PE_RE_BATTLE_STARTING, PE_RE_BATTLE_RUNNING,
    PE_RE_BATTLE_ENDING, PE_RE_BATTLE_GRACE
} PeReBattlePhase;

typedef enum {
    PE_RE_BATTLE_FOE_NONE, PE_RE_BATTLE_FOE_QUEUED, PE_RE_BATTLE_FOE_JOINED,
    PE_RE_BATTLE_FOE_DEAD, PE_RE_BATTLE_FOE_LEFT
} PeReBattleFoeState;

typedef struct {
    uint8_t slot;
    PeReBattleFoeState state;
    double x, z;
    int visible;
} PeReBattleFoe;

typedef struct {
    double seconds, distance;
    int halve_on_repeat;
} PeReBattleSettings;

typedef struct {
    unsigned escapes;
    int frames;
    double distance;
} PeReBattleGrace;

typedef struct {
    PeReBattlePhase phase;
    const PeReBattleRules *rules;
    uint32_t stage, room;
    int floor;
    double aya_x, aya_z;
    int camera;
    PeReBattleFoe foe[PE_RE_ENEMIES_MAX]; /* indexed by room slot */
    uint8_t initial_slots[PE_RE_ENEMIES_MAX]; /* first roster, in join order */
    int initial_count;
    int joined;
    uint8_t waiting[PE_RE_ENEMIES_MAX];
    int waiting_count;
    PeReBattleSettings settings;
    PeReBattleGrace grace[PE_RE_ENEMIES_MAX];
    int grace_frames; /* longest active grace remaining */
} PeReBattle;

typedef enum {
    PE_RE_BATTLE_EV_NONE, PE_RE_BATTLE_EV_JOIN, PE_RE_BATTLE_EV_CAMERA, PE_RE_BATTLE_EV_END
} PeReBattleEventKind;
typedef struct { PeReBattleEventKind kind; uint8_t slot; int camera; } PeReBattleEvent;

/* Initialize before use to set the grace defaults. The adapter may override settings from F1. */
void pe_re_battle_init(PeReBattle *);

/* A zero-initialized director is also usable with grace disabled. The lead is already selected by the encounter adapter.
 * A null in_view adds only the lead. Other foes must return 1 to join. */
int pe_re_battle_begin(PeReBattle *, PeReSession *, const PeReBattleRules *,
                       const PeReEnemyPlacement *lead, int (*in_view)(const PeReEnemyPlacement *));
/* Drive once per frame, including during field grace. The visibility callback uses the
 * current backdrop. A JOIN is committed only when there is room for its event. */
int pe_re_battle_step(PeReBattle *, PeReSession *, double aya_x, double aya_z, int time_runs,
                      int (*in_view)(const PeReEnemyPlacement *), PeReBattleEvent *, int max);
void pe_re_battle_foe_moved(PeReBattle *, uint8_t slot, double x, double z);
void pe_re_battle_foe_died(PeReBattle *, uint8_t slot);
/* On escape, retain survivors' room positions and start their individual grace periods. */
void pe_re_battle_end(PeReBattle *, PeReSession *, int escaped);
/* Global gate allows other foes during grace. Use the slot gate when choosing a lead. */
int pe_re_battle_encounter_allowed(const PeReBattle *);
int pe_re_battle_encounter_allowed_for(const PeReBattle *, const PeReSession *, uint8_t slot);
/* Live joined slots, visible first and then by distance to Aya. Writes at most max slots. */
int pe_re_battle_target_order(const PeReBattle *, uint8_t *slots, int max);

typedef struct { double x, z; } PeReBattlePoint;
typedef struct { PeReBattlePoint min, max; } PeReBattleBounds;

/* Room bounds relative to Aya at battle start, in room units. Host bounds and zone clearance
 * use host units. Returns 1 if the scaled room fits beside the zones. */
int pe_re_battle_anchor(PeReBattleBounds room, PeReBattleBounds host, PeReBattleBounds zones,
                        double scale, double clearance, PeReBattlePoint *out);
PeReBattlePoint pe_re_battle_to_host(PeReBattlePoint room, PeReBattlePoint origin,
                                    PeReBattlePoint anchor, double scale);
PeReBattlePoint pe_re_battle_to_room(PeReBattlePoint host, PeReBattlePoint origin,
                                    PeReBattlePoint anchor, double scale);

#endif
