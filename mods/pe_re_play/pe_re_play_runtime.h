#ifndef PE_RE_PLAY_RUNTIME_H
#define PE_RE_PLAY_RUNTIME_H

/* Host UI bindings. Their storage remains valid for the lifetime of the runtime. */
typedef struct PeRePlayControls {
    int *pick;
    unsigned (*game_count)(void);
    const char *(*game_name)(unsigned);
    int *player;
    int *masks;
    int *size;
    void (*start)(void);
    void (*resume)(void);
    void (*stop)(void);
    int *placement_markers;
    int *placement_meshes;
    int *placement_idle;
    void (*to_door)(void);
    void (*to_pickup)(void);
    void (*to_ramp)(void);
    void (*to_ledge)(void);
    void (*to_message)(void);
    void (*to_item_spot)(void);
    void (*give_use_here)(void);
    void (*to_typewriter)(void);
    void (*give_ribbon)(void);
    void (*to_item_box)(void);
    void (*to_event)(void);
    int *jump_stage;
    int *jump_room;
    void (*to_room)(void);
    int *door_pick;
    void (*to_that_door)(void);
    void (*refresh)(void);
    void (*text_scene)(char *, unsigned);
    void (*text_status)(char *, unsigned);
    void (*text_pocket)(char *, unsigned);
    void (*text_scripts)(char *, unsigned);
    void (*text_enemies)(char *, unsigned);
    int *enemy_debug_slot;
    void (*text_enemy_movement)(char *, unsigned);
} PeRePlayControls;

int pe_re_play_init(const char *store);
void pe_re_play_shutdown(void);
const PeRePlayControls *pe_re_play_controls(void);

#endif
