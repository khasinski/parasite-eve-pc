#ifndef PE_BATTLE_SPACE_H
#define PE_BATTLE_SPACE_H
/* Where a battle in an imported room is fought in the host engine's field (docs/RE_PLAY_BATTLES.md, D14).
 *
 * The host room under an imported game has scripts that watch the player's native position (exits,
 * encounter rolls, events). A battle fought at that position walks into them. So the battle is
 * fought at an anchor off the host room's map: the room's units map onto host units around it.
 *
 *     host = anchor + (room - room_at_start) / k
 *     room = room_at_start + (host - anchor) * k
 *
 * k is the imported room's units per host unit (RE play's scale). Host positions are 16-bit
 * (+-32767), so the whole room, mapped, has to fit beside the host room. Pure arithmetic: no engine. */

typedef struct PeBattleBox {
    double x0, z0, x1, z1;          /* inclusive bounds; x0 <= x1, z0 <= z1 */
} PeBattleBox;

typedef struct PeBattleSpace {
    double anchor_x, anchor_z;      /* host units: where the player stands when the battle starts */
    double room_x, room_z;          /* room units: where she stood in the imported room */
    double k;                       /* room units per host unit */
} PeBattleSpace;

#define PE_BATTLE_SPACE_LIMIT 32767.0   /* host positions are 16-bit */
#define PE_BATTLE_SPACE_MARGIN 512.0    /* kept clear of the 16-bit edge */
#define PE_BATTLE_SPACE_CLEARANCE 2048.0 /* kept between the host room and the battle */

/* Choose an anchor: the imported room (`room`, room units), mapped at k around `room_x, room_z`,
 * must lie inside the 16-bit space less the margin and clear of `host` (host units) by the
 * clearance. Tries beside the host room on each side. 0 and `out` filled, or -1 (no side fits). */
int pe_battle_space_choose(const PeBattleBox *host, const PeBattleBox *room, double room_x, double room_z,
                           double k, PeBattleSpace *out);

void pe_battle_space_to_host(const PeBattleSpace *s, double room_x, double room_z, double *host_x, double *host_z);
void pe_battle_space_to_room(const PeBattleSpace *s, double host_x, double host_z, double *room_x, double *room_z);

#endif
