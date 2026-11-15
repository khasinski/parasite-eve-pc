#include "common.h"
#include "pe_hooks.h"
#include <string.h>
#include "pe1/battle.h"
#include "pe1/battle_runtime.h"
#include "pe1/collision_database.h"
#include "pe1/entity_animation.h"
#include "pe1/game_state.h"
#include "pe1/entity_floor.h"
#include "pe1/field_actor.h"
#include "pe1/field_collision.h"
#include "pe1/field_movement.h"
#include "pe1/floor_clip.h"
#include "pe1/floor_edge_point.h"
#include "pe1/floor_polygon.h"
#include "pe1/gte.h"
#include "pe1/gte_types.h"
#include "pe1/player_entity.h"
#include "pe1/scene_assets.h"

RampEdge *g_SceneDataTable2;
u16 g_FieldCollisionTriIndex;








int Akao_LoadVoiceBank(BattleEntity *actor);

void Entity_AdvanceAnim(BattleEntity *entity) {
    int flags;
    int new_flags;
    int old_frame;
    int step;
    int limit;
    int next_frame;
    int end_frame;

    if (entity != 0) {
        if (entity->parent != 0 && !(entity->parent->entityFlags & 0x800000)) {
            Entity_AdvanceAnim(entity->parent);
        }

        Akao_LoadVoiceBank(entity);
        new_flags = (entity->entityFlags & -9) | 0x800000;
        entity->entityFlags = new_flags;
        old_frame = entity->animFrame;
        entity->animPrev.fixed = old_frame;
        flags = entity->entityFlags;

        if (!(flags & 0x100)) {
            if (flags & 0x200) {
                u32 whole_frame = old_frame;

                if ((whole_frame >> 16) == entity->animStopFrame) {
                    return;
                }
            }

            if (flags & 0x200000) {
                Entity_SetAction(entity, entity->parent->actionMode);
                entity->animFrame = entity->parent->animFrame;
                return;
            }

            step = entity->animStep;
            PE_HOOK(PE_HOOK_ACTOR_STEP, (&(PeHookActorStep){entity, NULL, &step}));
            limit = entity->animStopFrame;
            next_frame = (s32)((u32)old_frame + (u32)step);
            end_frame = (s32)((u32)limit << 16);

            if (flags & 0x200) {
                if (old_frame < end_frame) {
                    if (end_frame < next_frame) {
                        entity->animFrame = end_frame;
                        return;
                    }
                }
                if (end_frame < old_frame) {
                    if (next_frame < end_frame) {
                        entity->animFrame = end_frame;
                        return;
                    }
                }
            }

            if (entity->animLastFrame < (next_frame >> 16)) {
                limit = (next_frame >> 16) % (entity->animLastFrame + 1);
                old_frame = 0;
                entity->entityFlags |= 8;
                next_frame = limit << 16;
            } else if (next_frame < 0) {
                old_frame = entity->animLastFrame << 16;
                next_frame += (entity->animLastFrame + 1) << 16;
                entity->entityFlags |= 8;
            }

            if (entity->entityFlags & 0x200) {
                if (!(end_frame < old_frame)) {
                    if (end_frame < next_frame) {
                        entity->animFrame = end_frame;
                        return;
                    }
                }
                if (old_frame < end_frame) {
                    entity->animFrame = next_frame;
                    return;
                }
                if (next_frame < end_frame) {
                    entity->animFrame = end_frame;
                    return;
                }
            }

store_next:
            entity->animFrame = next_frame;
        }
    }
}

void Entity_SetActionMode(BattleEntity *arg0, int arg1) {
    BattleEntity *entity;
    int mode;
    unsigned int idx;
    BattleEntity *it;
    void **table;
    void *action;

    entity = arg0;
    mode = arg1;
    idx = entity->modelIndex;
    table = idx < 10 ? g_GameState.bank_rows[idx] : 0;
    /* Some models have no animation for a pose the battle code asks for
     * (Eve's parts in M273 have no Stop pose 2). Retail then reads the frame
     * count from PS1 address 2, which is mapped kernel RAM; here the entity
     * keeps its current animation. */
    if (!table || (unsigned short)mode >= 48 || table[(unsigned short)mode] == NULL)
        return;
    action = table[(unsigned short)mode];
    PE_HOOK(PE_HOOK_ACTION_MODE, (&(PeHookActionMode){entity, mode}));
    entity->actionMode = mode;
    entity->animFrame = 0;
    entity->animPrev.fixed = 0;
    entity->actionData = action;
    entity->entityFlags &= -0x201;
    entity->animLastFrame = ((RenderAnimationDataHeader *)entity->actionData)->frame_count - 1;

    if (entity->entityFlags & 0x100000) {
        it = (BattleEntity *)g_FieldActorListHead;
        if (it != 0) {
            do {
                if (it->parent == entity && (it->entityFlags & 0x200000)) {
                    Entity_SetActionMode(it, (unsigned short)mode);
                }
                it = it->next;
            } while (it != 0);
        }
    }
}

void Entity_SetAction(BattleEntity *arg0, int arg1) {
    BattleEntity *entity;
    int raw_mode;
    unsigned int mode;
    unsigned int idx;
    BattleEntity *it;
    void **table;
    RenderAnimationDataHeader *action;

    entity = arg0;
    raw_mode = arg1;
    mode = (unsigned short)raw_mode;
    if (entity->actionMode != mode) {
        idx = entity->modelIndex;
        table = idx < 10 ? g_GameState.bank_rows[idx] : 0;
        /* A missing pose keeps the current animation, as in Entity_SetActionMode. */
        if (!table || mode >= 48 || table[mode] == NULL)
            return;
        entity->animFrame = 0;
        entity->animPrev.fixed = 0;
        entity->actionMode = raw_mode;
        action = table[mode];
        entity->actionData = action;
        entity->animLastFrame = action->frame_count - 1;
    }

    entity->entityFlags &= -0x201;
    if (entity->entityFlags & 0x100000) {
        it = (BattleEntity *)g_FieldActorListHead;
        if (it != 0) {
            do {
                if (it->parent == entity && (it->entityFlags & 0x200000)) {
                    Entity_SetAction(it, (unsigned short)raw_mode);
                }
                it = it->next;
            } while (it != 0);
        }
    }
}

/* The room collision database: clearing the per-room collision state and
 * relocating the loaded database's offsets into pointers. */

unsigned short g_FloorBlockedEdgeMaxX, g_FloorBlockedEdgeMinX, g_FloorBlockedEdgeMaxZ, g_FloorBlockedEdgeMinZ;
unsigned short g_CollisionRadius;
short g_EntityCollisionWallParam;
CollisionRegionTable g_RegionHeightTable;
struct CollisionPlane *g_CollisionPlaneTable;
CollisionDatabase *g_CollisionDb;
PolygonVertex *g_EntityCollisionWallSlot;
PolygonVertex *g_FloorPolygonVertices;
u16 g_FloorPolygonVertexCount;

void Entity_ResetStateGlobals(void) {
    unsigned int i;
    unsigned int count;
    unsigned int *ptr;

    g_RegionHeightTable.entries = 0;

    for (i = 0; i < 8; i += 4) {
        *(short *)&((char *)&g_FloorBlockedEdgePointA)[i] = 0;
        *(short *)&g_FloorBlockedEdgeZHalves[i] = 0;
    }

    count = 0;
    g_SceneDataTable2 = 0;
    ptr = g_FloorEdgeVisitedBitmap;

    while (count < 20) {
        *ptr = 0;
        count++;
        ptr++;
    }

    g_FieldCollisionTriIndex = 0;
    g_FloorBlockedEdgeMinZ = 0;
    g_FloorBlockedEdgeMaxZ = 0;
    g_FloorBlockedEdgeMinX = 0;
    g_FloorBlockedEdgeMaxX = 0;
    g_CollisionRadius = 0;
    g_CollisionPlaneTable = 0;
    g_CollisionDb = 0;
    g_EntityCollisionWallSlot = 0;
    g_FloorPolygonVertices = 0;
    g_EntityCollisionWallParam = 0;
    g_FloorPolygonVertexCount = 0;
}

/* Ordinary memory holds the relocated header and region pointers. Disc bytes
 * remain immutable; data tables retain their original packed layouts. */
static CollisionDatabase g_CollisionRuntime;
static CollisionRegionEntry g_CollisionRegions[65535];

void Entity_RelocateSceneData(void)
{
    const u8 *loaded = g_RoomCollisionData[0];
    const CollisionDatabaseFile *file = (const CollisionDatabaseFile *)loaded;
    CollisionDatabase *base = &g_CollisionRuntime;
    if (!loaded) {
        g_CollisionDb = 0;
        g_RegionHeightTable.entries = 0;
        g_CollisionPlaneTable = 0;
        g_SceneDataTable2 = 0;
        return;
    }
    memcpy(base, file, 24);
    base->vertices.pointer = (u8 *)loaded + file->vertices;
    base->triangles.pointer = (u8 *)loaded + file->triangles;
    base->planes.pointer = file->planes ? (struct CollisionPlane *)(loaded + file->planes) : 0;
    base->rampEdges.pointer = (struct RampEdge *)(loaded + file->rampEdges);
    base->regions = g_CollisionRegions;
    base->visitedWordCount = (file->visitedBits >> 5) + 1;
    for (u32 i = 0; i < file->regionCount; ++i)
        base->regions[i].height = (s16 *)(loaded + file->regions[i]);
    g_CollisionDb = base;
    g_RegionHeightTable.entries = base->regions;
    g_CollisionPlaneTable = base->planes.pointer;
    g_SceneDataTable2 = base->rampEdges.pointer;
}


void Entity_UpdateList(void) {
    FieldActor *node;

    if (g_FieldMoveLock & 4) {
        Entity_ApplyCollisionResponse(g_PlayerEntity);
    }

    node = g_FieldActorListHead;
    while (node != 0) {
        if ((node->flags & 0x80) == 0) {
            Entity_UpdateAndRender((struct BattleEntity *)node);
        }
        node = node->next;
    }
}

void Entity_FindFloor(BattleEntity *actor)
{
    u8 *floor_data;
    u8 *simple_triangle;
    CollisionFace *complex_triangle;
    u16 *initial_indices;
    u16 *indices;
    u16 triangle_count;
    u32 floor_index;
    u32 triangle_index;
    u16 triangle_id;
    int floor_value;
    s16 x;
    s16 z;
    int x_height;
    int z_height;
    u8 scratch[8];

    if ((actor->entityFlags & 0x80) != 0) {
        return;
    }

    x = (u16)actor->posX.parts.integer;
    z = (u16)actor->posZ.parts.integer;
    if (g_CollisionDb->regionCount == 0) {
        return;
    }

    floor_index = 0;
    do {
        if (g_CollisionPlaneTable == 0) {
            floor_data = (u8 *)g_RegionHeightTable.entries[floor_index].flat;
            triangle_index = 0;
            floor_value = ((CollisionFlatRegion *)floor_data)->height;
            triangle_count = ((CollisionFlatRegion *)floor_data)->triangleCount;
            initial_indices = ((CollisionFlatRegion *)floor_data)->triangleIndices;
            indices = initial_indices;
            for (triangle_index = 0; triangle_index < triangle_count;
                 triangle_index++, indices++) {
                    triangle_id = *indices;
                    simple_triangle = (u8 *)&g_CollisionDb->triangles.xz[triangle_id];
                    if (Geo_PointInTri(simple_triangle, x, z)) {
                        actor->collisionFace = simple_triangle;
                        actor->collisionFaceMirror = simple_triangle;
                        if ((actor->entityFlags & 2) == 0) {
                            actor->posY.fixed = (s32)((u32)floor_value << 16);
                        }
                        return;
                    }
            }
        } else {
            floor_data = (u8 *)g_RegionHeightTable.entries[floor_index].plane;
            triangle_index = 0;
            triangle_count = ((CollisionPlaneRegion *)floor_data)->triangleCount;
            initial_indices = ((CollisionPlaneRegion *)floor_data)->triangleIndices;
            indices = initial_indices;
            for (triangle_index = 0; triangle_index < triangle_count;
                 triangle_index++, indices++) {
                    triangle_id = *indices;
                    complex_triangle = &g_CollisionDb->triangles.xyz[triangle_id].face;
                    if (Geo_PointInTri(complex_triangle, x, z)) {
                        actor->collisionFace = complex_triangle;
                        actor->collisionFaceMirror = complex_triangle;
                        if ((actor->entityFlags & 2) == 0) {
                            x_height = Math_FixedMul(
                                g_CollisionPlaneTable[complex_triangle->plane].a,
                                actor->posX.fixed);
                            z_height = Math_FixedMul(
                                g_CollisionPlaneTable[complex_triangle->plane].c,
                                actor->posZ.fixed);
                            actor->posY.fixed = Math_FixedMul(
                                (s32)((u32)complex_triangle->distance - (u32)x_height - (u32)z_height),
                                g_CollisionPlaneTable[complex_triangle->plane].inverseB);
                        }
                        return;
                    }
            }
        }
        floor_index++;
    } while (floor_index < g_CollisionDb->regionCount);
}

void Entity_ResolvePosition(BattleEntity *actor, int index) {
    BattleEntity *actor_s2;
    CollisionPlane *table_base;
    int index_v1;
    int arg_y;
    int arg_z;
    CollisionTriangleHeader *entry;
    int value;

    actor_s2 = actor;
    table_base = g_CollisionPlaneTable;
    index_v1 = index;

    if (table_base == 0) {
        char *base;
        CollisionRegionEntry *table;
        int idx;

        idx = index_v1 & 0xFFFF;
        base = (char *)g_CollisionDb;
        base = (char *)((CollisionDatabase *)base)->triangles.pointer;
        entry = (CollisionTriangleHeader *)&((CollisionTriangleXZ *)base)[idx];
        actor_s2->collisionFace = entry;
        actor_s2->collisionFaceMirror = entry;
        table = g_RegionHeightTable.entries;
        value = table[entry->region].flat->height;
        arg_y = actor_s2->posX.parts.integer;
        arg_z = actor_s2->posZ.parts.integer;
        value = (s32)((u32)value << 16);
        actor_s2->posY.fixed = value;
    } else {
        CollisionFace *entry_s0;
        int first;
        int idx;
        int id;
        int second;

        idx = index_v1 & 0xFFFF;
        entry_s0 = &g_CollisionDb->triangles.xyz[idx].face;
        actor_s2->collisionFace = entry_s0;
        actor_s2->collisionFaceMirror = entry_s0;
        {
            int id_v1;
            id_v1 = entry_s0->plane;
            id = id_v1;
        }
        first = Math_FixedMul(table_base[id].a, actor_s2->posX.fixed);
        id = entry_s0->plane;
        second = Math_FixedMul(g_CollisionPlaneTable[id].c, actor_s2->posZ.fixed);
        id = entry_s0->plane;
        value = Math_FixedMul((s32)((u32)entry_s0->distance - (u32)first - (u32)second), g_CollisionPlaneTable[id].inverseB);
        actor_s2->posY.fixed = value;
        arg_y = actor_s2->posX.parts.integer;
        arg_z = actor_s2->posZ.parts.integer;
        entry = (CollisionTriangleHeader *)entry_s0;
    }

    Geo_PointInTri(entry, arg_y, arg_z);
    actor_s2->baseX = actor_s2->posX.fixed;
    actor_s2->baseY = actor_s2->posY.fixed;
    actor_s2->baseZ = actor_s2->posZ.fixed;
}

/* Undoes the move: the actor goes back to its last valid position. */
static inline void Entity_RevertMove(BattleEntity *actor)
{
    actor->posX.fixed = actor->baseX;
    actor->posY.fixed = actor->baseY;
    actor->posZ.fixed = actor->baseZ;
    actor->entityFlags |= 0x80000;
}

/* Keeps an actor on the walkable mesh after it moved: finds the triangle
 * under the new position (sliding the player along a ramp edge first when
 * needed), then snaps the height to the plane or region floor, or rolls the
 * move back when no floor is reachable. */
void Entity_UpdateAndRender(BattleEntity *actor)
{
    s16 x, z, oldX, oldZ;
    void *face;
    CollisionPlane *planes;
    u16 saved, radius;
    unsigned int i, j;

    x = actor->posX.fixed >> 16;
    z = actor->posZ.fixed >> 16;
    actor->entityFlags &= ~0x80000;
    oldX = actor->baseX >> 16;
    oldZ = actor->baseZ >> 16;
    if (oldX == x && oldZ == z && actor->posY.fixed == actor->baseY) {
        if (actor == ((BattleEntity *)g_PlayerEntity))
            D_8009D2E8 &= ~8;
        return;
    }

    radius = actor->renderObject.hit_cylinder.radius;
    face = actor->collisionFace;
    actor->collisionFaceMirror = face;
    g_CollisionRadius = radius;
    g_CollisionRadius = radius * actor->moveSpeed / 4096;

    if (actor == ((BattleEntity *)g_PlayerEntity)) {
        if (D_8009D2E8 & 8) {
            int area, oldArea, a, b;
            int sxy;

            /* Signed areas of the ramp edge against the new and the old
             * position. */
            sxy = ((s32)((u32)z << 16)) | (u16)x;
            gte_ldsxy3(g_FloorBlockedEdgePointA.packed, g_FloorBlockedEdgePrev.packed, sxy);
            gte_nclip();
            gte_stmac0(&area);
            sxy = ((s32)((u32)oldZ << 16)) | (u16)oldX;
            gte_ldsxy2(sxy);
            gte_nclip();
            gte_stmac0(&oldArea);
            a = area;
            b = oldArea;
            /* Still on the edge when the actor crossed it or did not move
             * closer to it, and the radius stays inside the edge box. */
            if ((a ^ b) < 0 || (a = abs(a), b >= 0 ? b >= a : -b >= a)) {
                u16 r = g_CollisionRadius;
                u16 maxX = g_FloorBlockedEdgeMaxX, minX = g_FloorBlockedEdgeMinX;
                u16 maxZ = g_FloorBlockedEdgeMaxZ, minZ = g_FloorBlockedEdgeMinZ;
                s16 width = maxX - minX;
                s16 height = maxZ - minZ;
                int d;

                d = x - r;
                if (!((s16)maxX < d &&
                      (width > height || z > (s16)maxZ ||
                       (d = z + r) < (s16)minZ))) {
                    d = x + r;
                    if (!(d < (s16)minX &&
                          (width > height || (s16)maxZ < (d = z - r) ||
                           (d = z + r) < (s16)minZ))) {
                        d = z - r;
                        if (!((s16)maxZ < d &&
                              (height > width || (s16)maxX < (d = x - r) ||
                               (d = x + r) < (s16)minX))) {
                            d = z + r;
                            if (!(d < (s16)minZ &&
                                  (height > width || (s16)maxX < (d = x - r) ||
                                   (d = x + r) < (s16)minX)))
                                /* Recorded debt: retail enters the slide
                                 * code from here, past the first floor
                                 * search (goto). */
                                goto slide;
                        }
                    }
                }
            }
        }
        D_8009D2E8 &= ~8;
    }

    for (i = 0; i < g_CollisionDb->visitedWordCount; i++)
        g_FloorEdgeVisitedBitmap[i] = 0;
    if (!Geo_ClipToFloorBoundary(x, z, face)) {
        if (actor != ((BattleEntity *)g_PlayerEntity)) {
            Entity_RevertMove(actor);
            return;
        }
    slide:
        Entity_SlideOnRamp(actor);
        x = actor->posX.parts.integer;
        z = actor->posZ.parts.integer;
        for (j = 0; j < g_CollisionDb->visitedWordCount; j++)
            g_FloorEdgeVisitedBitmap[j] = 0;
        saved = g_FieldCollisionTriIndex;
        i = Geo_ClipToFloorBoundary(x, z, face);
        if (!i) {
            if (saved == g_FieldCollisionTriIndex) {
                for (j = 0; j < g_CollisionDb->visitedWordCount; j++)
                    g_FloorEdgeVisitedBitmap[j] = 0;
                g_FloorEdgeVisitedBitmap[g_FieldCollisionTriIndex >> 5] = 1u << (g_FieldCollisionTriIndex & 0x1F);
                i = Geo_ClipToFloorBoundary(x, z, face);
                if (i) {
                    Entity_RevertMove(actor);
                    return;
                }
            }
            Entity_SlideOnRamp(actor);
            x = actor->posX.parts.integer;
            z = actor->posZ.parts.integer;
            for (j = 0; j < g_CollisionDb->visitedWordCount; j++)
                g_FloorEdgeVisitedBitmap[j] = 0;
            i = Geo_ClipToFloorBoundary(x, z, face);
            if (!i) {
                Entity_RevertMove(actor);
                return;
            }
        }
    }

    if (!Geo_PointInTri(face, x, z))
        face = Geo_ClipToFloorBoundarySub(face, 0, x, z, oldX, oldZ);
    /* A boundary/degenerate crossing may have no reachable neighbour. Keep
     * the last valid position, as for an unsuccessful boundary search above. */
    if (!face) {
        Entity_RevertMove(actor);
        return;
    }

    if ((planes = g_CollisionPlaneTable) != 0) {
        CollisionFace *floor = face;

        if (actor->entityFlags & 2) {
            int y = actor->posY.fixed;
            int a = Math_FixedMul(planes[floor->plane].a, actor->posX.fixed);
            int c = Math_FixedMul(g_CollisionPlaneTable[floor->plane].c, actor->posZ.fixed);

            actor->posY.fixed = Math_FixedMul(floor->distance - a - c,
                                              g_CollisionPlaneTable[floor->plane].inverseB);
            if (y < actor->posY.fixed) {
                actor->posY.fixed = y;
            } else {
                actor->entityFlags &= ~2;
                actor->motionY = 0;
            }
        } else {
            int a = Math_FixedMul(planes[floor->plane].a, actor->posX.fixed);
            int c = Math_FixedMul(g_CollisionPlaneTable[floor->plane].c, actor->posZ.fixed);

            actor->posY.fixed = Math_FixedMul(floor->distance - a - c,
                                              g_CollisionPlaneTable[floor->plane].inverseB);
        }
    } else {
        CollisionTriangleHeader *floor = face;
        u8 region = floor->region;
        u32 flags = actor->entityFlags;
        int height = g_RegionHeightTable.entries[region].flat->height;

        if (flags & 2) {
            if (actor->posY.fixed >= height && actor->baseY < height) {
                actor->entityFlags = flags & ~2;
                actor->posY.fixed = height;
                actor->motionY = 0;
            }
        } else if (region != ((CollisionTriangleHeader *)actor->collisionFace)->region) {
            int delta;
            int step;

            delta = height - actor->posY.parts.integer;
            step = actor->stepHeight;
            if (delta < 0)
                delta = -delta;
            step = (s32)((u32)step << 16);
            if (delta >= step) {
                Entity_RevertMove(actor);
                return;
            }
            actor->posY.fixed = (s32)((u32)height << 16);
        }
    }
    actor->collisionFace = face;
}

/* Shared native state; external variables have one storage definition. */
FloorEdgePoint g_FloorBlockedEdges[2];
u32 g_FloorEdgeVisitedBitmap[2048];
FloorNeighbours g_FloorNeighboursInit = {{-1, -1, -1}};
RampEdge *g_SceneDataTable2;
u16 g_FieldCollisionTriIndex;
u16 g_FloorBlockedEdgeMaxX, g_FloorBlockedEdgeMinX, g_FloorBlockedEdgeMaxZ, g_FloorBlockedEdgeMinZ;
unsigned short g_CollisionRadius;
CollisionDatabase *g_CollisionDb;
CollisionPlane *g_CollisionPlaneTable;
CollisionRegionTable g_RegionHeightTable;
struct CollisionPlane *g_CollisionPlaneTable;
CollisionDatabase *g_CollisionDb;

/* Tests the circle of radius g_CollisionRadius around (x, z) against the edges of
 * a walkable triangle (flat XZ format when there is no plane table, sloped
 * XYZ format otherwise). An edge the circle touches without a walkable
 * neighbour is published as the blocking boundary (g_FloorBlockedEdgePointA..g_FloorBlockedEdgeMinZ,
 * edge id in g_FieldCollisionTriIndex) and 0 is returned; touched edges with a neighbour
 * queue that triangle, which is then walked recursively. Every edge is
 * tested once per query (g_FloorEdgeVisitedBitmap bitmap). Returns 1 when nothing
 * blocks. */
int Geo_ClipToFloorBoundary(s16 x, s16 z, void *triangle)
{
    FloorNeighbours neighbours;
    int edgeId;
    u32 nextIndex;
    u16 nextX, nextZ;
    u32 prevIndex;
    u16 prevX, prevZ;
    u32 *bits;
    int d;

    neighbours = g_FloorNeighboursInit;
    if (!g_CollisionPlaneTable) {
        CollisionTriangleXZ *tri = triangle;
        int px = x;
        int pz = z;
        u32 slot = 0;
        CollisionVertexTable vertex;

        nextIndex = tri->layout.links.vertices[2];
        COLLISION_VERTEX(vertex, nextIndex, CollisionVertexXZ);
        nextX = vertex.xz->x;
        nextZ = vertex.xz->z;
        do {
            u32 bit = tri->layout.links.edgeIds[slot];

            prevIndex = nextIndex;
            prevX = nextX;
            prevZ = nextZ;
            bits = &g_FloorEdgeVisitedBitmap[bit >> 5];
            bit = 1u << (bit & 0x1F);
            nextIndex = tri->layout.links.vertices[slot];
            COLLISION_VERTEX(vertex, nextIndex, CollisionVertexXZ);
            nextX = vertex.xz->x;
            nextZ = vertex.xz->z;
            if (!(*bits & bit)) {
                u16 r;
                int dx, dz;

                *bits |= bit;
                r = g_CollisionRadius;
                d = px - r;
                if ((s16)nextX < d && (s16)prevX < d)
                    continue;
                d = px + r;
                if (d < (s16)nextX && d < (s16)prevX)
                    continue;
                d = pz - r;
                if ((s16)nextZ < d && (s16)prevZ < d)
                    continue;
                d = pz + r;
                if (d < (s16)nextZ && d < (s16)prevZ)
                    continue;
                dz = pz - (s16)nextZ;
                dx = px - (s16)nextX;
                edgeId = tri->layout.links.edgeIds[slot];
                d = g_SceneDataTable2[edgeId].length.parts.integer;
                d = (dz * ((s16)prevX - (s16)nextX)
                     - dx * ((s16)prevZ - (s16)nextZ)) / d;
                if (d < 0)
                    d = -d;
                if (d > r)
                    continue;
                if (nextIndex < prevIndex) {
                    d = Math_FixedMul(g_SceneDataTable2[edgeId].directionX, (s32)((u32)dx << 16));
                    d += Math_FixedMul(g_SceneDataTable2[edgeId].directionZ, (s32)((u32)dz << 16));
                    if (d < 0) {
                        d = dx * dx + dz * dz;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                    if (g_SceneDataTable2[edgeId].length.fixed < d) {
                        int ex = px - (s16)prevX;
                        int ez = pz - (s16)prevZ;

                        d = ex * ex + ez * ez;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                } else {
                    int ex = px - (s16)prevX;
                    int ez = pz - (s16)prevZ;

                    d = Math_FixedMul(g_SceneDataTable2[edgeId].directionX, (s32)((u32)ex << 16));
                    d += Math_FixedMul(g_SceneDataTable2[edgeId].directionZ, (s32)((u32)ez << 16));
                    if (d < 0) {
                        d = ex * ex + ez * ez;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                    if (g_SceneDataTable2[edgeId].length.fixed < d) {
                        d = dx * dx + dz * dz;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                }
                {
                    u16 neighbour = tri->layout.links.neighbours[slot];

                    if (neighbour == 0xFFFF
                        || (g_CollisionDb->triangles.xz[neighbour].kind & 0x80)) {
                        g_FloorBlockedEdgePointA.point.x = nextX;
                        g_FloorBlockedEdgePointA.point.z = nextZ;
                        g_FloorBlockedEdgePrev.point.x = prevX;
                        g_FloorBlockedEdgePrev.point.z = prevZ;
                        if ((s16)nextX > (s16)prevX) {
                            g_FloorBlockedEdgeMaxX = nextX;
                            g_FloorBlockedEdgeMinX = prevX;
                        } else {
                            g_FloorBlockedEdgeMaxX = prevX;
                            g_FloorBlockedEdgeMinX = nextX;
                        }
                        if ((s16)nextZ > (s16)prevZ) {
                            g_FloorBlockedEdgeMaxZ = nextZ;
                            g_FloorBlockedEdgeMinZ = prevZ;
                        } else {
                            g_FloorBlockedEdgeMaxZ = prevZ;
                            g_FloorBlockedEdgeMinZ = nextZ;
                        }
                        g_FieldCollisionTriIndex = edgeId;
                        return 0;
                    }
                    neighbours.index[slot] = neighbour;
                }
            }
        } while (++slot < 3);
        for (slot = 0; slot < 3; slot++) {
            if (neighbours.index[slot] >= 0
                && !Geo_ClipToFloorBoundary(
                       x, z, &g_CollisionDb->triangles.xz[neighbours.index[slot]]))
                return 0;
        }
        return 1;
    } else {
        CollisionTriangleXYZ *tri = triangle;
        int px = x;
        int pz = z;
        u32 slot = 0;
        CollisionVertexTable vertex;

        nextIndex = tri->layout.links.vertices[2];
        COLLISION_VERTEX(vertex, nextIndex, CollisionVertexXYZ);
        nextX = vertex.xyz->x;
        nextZ = vertex.xyz->z;
        do {
            u32 bit = tri->layout.links.edgeIds[slot];

            prevIndex = nextIndex;
            prevX = nextX;
            prevZ = nextZ;
            bits = &g_FloorEdgeVisitedBitmap[bit >> 5];
            bit = 1u << (bit & 0x1F);
            nextIndex = tri->layout.links.vertices[slot];
            COLLISION_VERTEX(vertex, nextIndex, CollisionVertexXYZ);
            nextX = vertex.xyz->x;
            nextZ = vertex.xyz->z;
            if (!(*bits & bit)) {
                u16 r;
                int dx, dz;

                *bits |= bit;
                r = g_CollisionRadius;
                d = px - r;
                if ((s16)nextX < d && (s16)prevX < d)
                    continue;
                d = px + r;
                if (d < (s16)nextX && d < (s16)prevX)
                    continue;
                d = pz - r;
                if ((s16)nextZ < d && (s16)prevZ < d)
                    continue;
                d = pz + r;
                if (d < (s16)nextZ && d < (s16)prevZ)
                    continue;
                dz = pz - (s16)nextZ;
                dx = px - (s16)nextX;
                edgeId = tri->layout.links.edgeIds[slot];
                d = g_SceneDataTable2[edgeId].length.parts.integer;
                d = (dz * ((s16)prevX - (s16)nextX)
                     - dx * ((s16)prevZ - (s16)nextZ)) / d;
                if (d < 0)
                    d = -d;
                if (d > r)
                    continue;
                if (nextIndex < prevIndex) {
                    d = Math_FixedMul(g_SceneDataTable2[edgeId].directionX, (s32)((u32)dx << 16));
                    d += Math_FixedMul(g_SceneDataTable2[edgeId].directionZ, (s32)((u32)dz << 16));
                    if (d < 0) {
                        d = dx * dx + dz * dz;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                    if (g_SceneDataTable2[edgeId].length.fixed < d) {
                        int ex = px - (s16)prevX;
                        int ez = pz - (s16)prevZ;

                        d = ex * ex + ez * ez;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                } else {
                    int ex = px - (s16)prevX;
                    int ez = pz - (s16)prevZ;

                    d = Math_FixedMul(g_SceneDataTable2[edgeId].directionX, (s32)((u32)ex << 16));
                    d += Math_FixedMul(g_SceneDataTable2[edgeId].directionZ, (s32)((u32)ez << 16));
                    if (d < 0) {
                        d = ex * ex + ez * ez;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                    if (g_SceneDataTable2[edgeId].length.fixed < d) {
                        d = dx * dx + dz * dz;
                        if (d > g_CollisionRadius * g_CollisionRadius)
                            continue;
                    }
                }
                {
                    u16 neighbour = tri->layout.links.neighbours[slot];

                    if (neighbour == 0xFFFF
                        || (g_CollisionDb->triangles.xyz[neighbour].kind & 0x80)) {
                        g_FloorBlockedEdgePointA.point.x = nextX;
                        g_FloorBlockedEdgePointA.point.z = nextZ;
                        g_FloorBlockedEdgePrev.point.x = prevX;
                        g_FloorBlockedEdgePrev.point.z = prevZ;
                        if ((s16)nextX > (s16)prevX) {
                            g_FloorBlockedEdgeMaxX = nextX;
                            g_FloorBlockedEdgeMinX = prevX;
                        } else {
                            g_FloorBlockedEdgeMaxX = prevX;
                            g_FloorBlockedEdgeMinX = nextX;
                        }
                        if ((s16)nextZ > (s16)prevZ) {
                            g_FloorBlockedEdgeMaxZ = nextZ;
                            g_FloorBlockedEdgeMinZ = prevZ;
                        } else {
                            g_FloorBlockedEdgeMaxZ = prevZ;
                            g_FloorBlockedEdgeMinZ = nextZ;
                        }
                        g_FieldCollisionTriIndex = edgeId;
                        return 0;
                    }
                    neighbours.index[slot] = neighbour;
                }
            }
        } while (++slot < 3);
        for (slot = 0; slot < 3; slot++) {
            if (neighbours.index[slot] >= 0
                && !Geo_ClipToFloorBoundary(
                       x, z, &g_CollisionDb->triangles.xyz[neighbours.index[slot]]))
                return 0;
        }
        return 1;
    }
}

/* Floor-mesh triangle walk: follow a segment across neighbouring walkable
 * triangles, and the point-in-triangle test it uses. Contiguous -G8 pair on
 * the collision database (main_tu_evidence G0227 joins the whole floor
 * resolution range). */

/* Walks from the triangle `indices` across the edge crossed by the segment
 * (x0, z0)-(x1, z1) into neighbouring triangles, never stepping back into
 * `previous`, until one contains the start point. Returns that triangle or
 * 0 when the segment leaves the walkable mesh. */
void *Geo_ClipToFloorBoundarySub(void *triangle, void *previous, s16 x0, s16 z0,
                                 s16 x1, s16 z1)
{
    int deltaX, deltaZ;
    s16 maxX, minX, maxZ, minZ;
    u16 nextX, nextZ, previousX, previousZ;
    u16 edge;

    deltaX = x1 - x0;
    deltaZ = z1 - z0;
    if (deltaX < 0) {
        maxX = x0;
        minX = x1;
    } else {
        maxX = x1;
        minX = x0;
    }
    if (deltaZ < 0) {
        maxZ = z0;
        minZ = z1;
    } else {
        maxZ = z1;
        minZ = z0;
    }

    if (g_CollisionPlaneTable) {
        CollisionVertexTable vertex;

        COLLISION_VERTEX(vertex, ((CollisionTriangleXYZ *)triangle)->layout.links.vertices[2], CollisionVertexXYZ);
        nextX = vertex.xyz->x;
        nextZ = vertex.xyz->z;
    } else {
        CollisionVertexTable vertex;

        COLLISION_VERTEX(vertex, ((CollisionTriangleXZ *)triangle)->layout.links.vertices[2], CollisionVertexXZ);
        nextX = vertex.xz->x;
        nextZ = vertex.xz->z;
    }

    for (edge = 0; edge < 3; edge++) {
        int edgeX, edgeZ, startX, startZ, denominator, along;
        void *neighbour;

        previousX = nextX;
        previousZ = nextZ;
        if (g_CollisionPlaneTable) {
            CollisionVertexTable vertex;

            COLLISION_VERTEX(vertex, ((CollisionTriangleXYZ *)triangle)->layout.links.vertices[edge], CollisionVertexXYZ);
            nextX = vertex.xyz->x;
            nextZ = vertex.xyz->z;
        } else {
            CollisionVertexTable vertex;

            COLLISION_VERTEX(vertex, ((CollisionTriangleXZ *)triangle)->layout.links.vertices[edge], CollisionVertexXZ);
            nextX = vertex.xz->x;
            nextZ = vertex.xz->z;
        }

        edgeX = (s16)nextX - (s16)previousX;
        if (edgeX > 0) {
            if (maxX < (s16)previousX || minX >= (s16)nextX)
                continue;
        } else {
            if (maxX < (s16)nextX || minX >= (s16)previousX)
                continue;
        }
        edgeZ = (s16)nextZ - (s16)previousZ;
        if (edgeZ > 0) {
            if (maxZ < (s16)previousZ || minZ >= (s16)nextZ)
                continue;
        } else {
            if (maxZ < (s16)nextZ || minZ >= (s16)previousZ)
                continue;
        }

        startX = x0 - (s16)nextX;
        startZ = z0 - (s16)nextZ;
        along = edgeZ * startX - edgeX * startZ;
        denominator = deltaZ * edgeX - deltaX * edgeZ;
        if (denominator > 0) {
            if (along < 0 || denominator < along)
                continue;
        } else {
            if (along > 0 || along < denominator)
                continue;
        }
        along = deltaX * startZ - deltaZ * startX;
        if (denominator > 0) {
            if (along < 0 || denominator < along)
                continue;
        } else {
            if (along > 0 || along < denominator)
                continue;
        }
        if (denominator == 0)
            continue;

        if (!g_CollisionPlaneTable) {
            neighbour = &g_CollisionDb->triangles.xz[((CollisionTriangleXZ *)triangle)->layout.links.neighbours[edge]];
            if (neighbour == previous)
                continue;
            if (Geo_PointInTri(neighbour, x0, z0))
                return neighbour;
            neighbour = Geo_ClipToFloorBoundarySub(neighbour, triangle, x0, z0,
                                                   x1, z1);
            if (neighbour)
                return neighbour;
        } else {
            neighbour = &g_CollisionDb->triangles.xyz[((CollisionTriangleXYZ *)triangle)->layout.links.neighbours[edge]];
            if (neighbour == previous)
                continue;
            if (Geo_PointInTri(neighbour, x0, z0))
                return neighbour;
            neighbour = Geo_ClipToFloorBoundarySub(neighbour, triangle, x0, z0,
                                                   x1, z1);
            if (neighbour)
                return neighbour;
        }
    }
    return 0;
}

int Geo_PointInTri(void *triangle, s16 x, s16 z)
{
    u16 nextX, nextZ, previousX, previousZ;
    unsigned edge;
    unsigned inside;

    if (g_CollisionPlaneTable) {
        CollisionVertexTable vertex;

        COLLISION_VERTEX(vertex, ((CollisionTriangleXYZ *)triangle)->layout.links.vertices[2], CollisionVertexXYZ);
        nextZ = vertex.xyz->z;
        nextX = vertex.xyz->x;
    } else {
        CollisionVertexTable vertex;

        COLLISION_VERTEX(vertex, ((CollisionTriangleXZ *)triangle)->layout.links.vertices[2], CollisionVertexXZ);
        nextZ = vertex.xz->z;
        nextX = vertex.xz->x;
    }

    edge = 0;
    inside = 0;
    while (edge < 3) {
        previousX = nextX;
        previousZ = nextZ;
        if (g_CollisionPlaneTable) {
            CollisionVertexTable vertex;

            COLLISION_VERTEX(vertex, ((CollisionTriangleXYZ *)triangle)->layout.links.vertices[edge], CollisionVertexXYZ);
            nextZ = vertex.xyz->z;
            nextX = vertex.xyz->x;
        } else {
            CollisionVertexTable vertex;

            COLLISION_VERTEX(vertex, ((CollisionTriangleXZ *)triangle)->layout.links.vertices[edge], CollisionVertexXZ);
            nextZ = vertex.xz->z;
            nextX = vertex.xz->x;
        }

        if ((z >= (s16)nextZ && z < (s16)previousZ) ||
            (z < (s16)nextZ && z >= (s16)previousZ)) {
            if (x < (s16)nextX && x < (s16)previousX) {
                inside = !inside;
            } else if (x < (s16)nextX || x < (s16)previousX) {
                int deltaZ = (s16)previousZ - (s16)nextZ;
                int side = (s32)((u32)((s16)previousX - (s16)nextX) * (u32)(z - (s16)nextZ));

                if (deltaZ < 0) {
                    deltaZ = (s32)((u32)deltaZ * (u32)(x - (s16)nextX));
                    if (side < deltaZ)
                        inside = !inside;
                } else {
                    deltaZ = (s32)((u32)deltaZ * (u32)(x - (s16)nextX));
                    if (deltaZ < side)
                        inside = !inside;
                }
            }
        }
        ++edge;
    }
    return inside;
}

void Entity_SlideOnRamp(BattleEntity *entity)
{
    s32 x = entity->baseX;
    s32 z = entity->baseZ;
    s32 deltaX = entity->posX.fixed - x;
    s32 directionX = g_SceneDataTable2[g_FieldCollisionTriIndex].directionX;
    s32 directionZ = g_SceneDataTable2[g_FieldCollisionTriIndex].directionZ;
    s32 projection, numerator;
    u32 loopEdgeX, loopEdgeZ;
    s32 edgeX, edgeZ;
    s32 radius, direction;

    D_8009D2E8 |= 8;
    projection = Math_FixedMul(directionX, deltaX);
    projection += Math_FixedMul(directionZ, entity->posZ.fixed - z);
    x = Math_FixedMul(directionX, projection);
    z = Math_FixedMul(directionZ, projection);
    z += entity->baseZ;
    x += entity->baseX;

    /* Retail keeps the low word of the cross product before signed division. */
    edgeX = g_FloorBlockedEdgePrev.point.x - g_FloorBlockedEdgePointA.point.x;
    edgeZ = g_FloorBlockedEdgePrev.point.z - g_FloorBlockedEdgePointA.point.z;
    numerator = ((z >> 16) - g_FloorBlockedEdgePointA.point.z) * edgeX
                - ((x >> 16) - g_FloorBlockedEdgePointA.point.x) * edgeZ;
    projection = g_SceneDataTable2[g_FieldCollisionTriIndex].length.parts.integer;
    projection = numerator / projection;
    if (projection < 0)
        projection = -projection;
    if (projection > g_CollisionRadius) {
        entity->posX.fixed = x;
        entity->posZ.fixed = z;
        return;
    }

    /* Search outward in +X, -X, +Z, -Z order until clear of the edge. */
    for (radius = 0; ; radius++) {
        for (direction = 0; direction < 4; ++direction) {
            directionX = x;
            directionZ = z;
            switch (direction) {
            case 0: directionX += radius << 16; break;
            case 1: directionX -= radius << 16; break;
            case 2: directionZ += radius << 16; break;
            case 3: directionZ -= radius << 16; break;
            }
            loopEdgeX = g_FloorBlockedEdgePrev.point.x - g_FloorBlockedEdgePointA.point.x;
            loopEdgeZ = g_FloorBlockedEdgePrev.point.z - g_FloorBlockedEdgePointA.point.z;
            numerator = ((directionZ >> 16) - g_FloorBlockedEdgePointA.point.z) * loopEdgeX
                        - ((directionX >> 16) - g_FloorBlockedEdgePointA.point.x) * loopEdgeZ;
            projection = g_SceneDataTable2[g_FieldCollisionTriIndex].length.parts.integer;
            projection = numerator / projection;
            if (projection < 0)
                projection = -projection;
            if (projection > g_CollisionRadius) {
                entity->posX.fixed = directionX;
                entity->posZ.fixed = directionZ;
                return;
            }
        }
    }
}

/* Containment in the shared 16.16 floor polygon vertices. */

int Geo_PointInPoly(int x, int z, const PolygonVertex *vertices, unsigned short count)
{
    unsigned short i;
    int inside;
    int previousX, previousZ, currentX, currentZ;
    int cross, dz;
    short intersects;
    x >>= 16;
    z >>= 16;
    i = 0;
    inside = 0;
    currentX = vertices[count - 1].x;
    currentZ = vertices[count - 1].z;
    for (;;) {
        previousX = currentX;
        previousZ = currentZ;
        currentZ = vertices[i].z;
        currentX = vertices[i].x;
        if ((z >= currentZ && z < previousZ) ||
            (z < currentZ && z >= previousZ)) {
            if (x < currentX && x < previousX) {
                inside = !inside;
            } else if (x < currentX || x < previousX) {
                /* Keep the low 32 bits, as the retail MULT/MFLO does,
                 * without signed multiplication overflow in C. */
                cross = (int)((long long)(previousX - currentX) * (z - currentZ));
                dz = previousZ - currentZ;
                if (dz < 0) {
                    dz = (int)((long long)dz * (x - currentX));
                    intersects = cross < dz;
                } else {
                    dz = (int)((long long)dz * (x - currentX));
                    intersects = dz < cross;
                }
                if (intersects)
                    inside = !inside;
            }
        }
        ++i;
        if (i >= count)
            break;
    }
    return inside;
}

void Entity_SlideOnWall(BattleEntity *input, const PolygonVertex *vertices,
                       u16 count, s16 edge, int oldX, int oldZ)
{
    BattleEntity *entity;
    GteVector vector, unit;
    s32 endX, endZ;
    s32 startX, startZ;
    s32 x, z, projection, length;
    s32 radius, direction, trialX, trialZ;
    entity = input;
    endX = vertices[edge].x;
    endZ = vertices[edge].z;
    if (edge > 0) {
        startX = vertices[edge - 1].x;
        startZ = vertices[edge - 1].z;
    } else {
        startX = vertices[count - 1].x;
        startZ = vertices[count - 1].z;
    }
    vector.x = endX - startX;
    vector.y = 0;
    vector.z = endZ - startZ;
    VectorNormal(&vector, &unit);
    unit.x *= 16;
    unit.z *= 16;
    x = entity->baseX;
    z = entity->baseZ;
    projection = Math_FixedMul(unit.x, entity->posX.fixed - x);
    projection += Math_FixedMul(unit.z, entity->posZ.fixed - z);
    x = Math_FixedMul(unit.x, projection);
    z = Math_FixedMul(unit.z, projection);
    x += entity->baseX;
    z += entity->baseZ;
    vector.x = startX - endX;
    vector.y = 0;
    vector.z = startZ - endZ;
    length = SquareRoot0(vector.x * vector.x + vector.z * vector.z);
    projection = (((z >> 16) - endZ) * vector.x - ((x >> 16) - endX) * vector.z) / length;
    if (projection < 0)
        projection = -projection;
    if (projection > g_CollisionRadius) {
        entity->posX.fixed = x;
        entity->posZ.fixed = z;
        return;
    }
    for (radius = 0; ; radius++) {
        for (direction = 0; direction < 4; ++direction) {
            trialX = x;
            trialZ = z;
            switch (direction) {
            case 0: trialX += radius << 16; break;
            case 1: trialX -= radius << 16; break;
            case 2: trialZ += radius << 16; break;
            case 3: trialZ -= radius << 16; break;
            }
            projection = (((trialZ >> 16) - endZ) * vector.x -
                          ((trialX >> 16) - endX) * vector.z) / length;
            if (projection < 0)
                projection = -projection;
            if (projection > g_CollisionRadius) {
                entity->posX.fixed = trialX;
                entity->posZ.fixed = trialZ;
                return;
            }
        }
    }
}

/* Returns the first polygon edge (index of its end vertex) whose segment
 * passes within g_CollisionRadius of (x, z), or -1 when no edge is that close. */
int Geo_FindNearestEdge(int x, int z, PolygonVertex *vertices,
                        unsigned short count)
{
    GteVector edge;
    s16 pointX = x;
    s16 pointZ = z;
    s16 prevX = vertices[count - 1].x;
    s16 prevZ = vertices[count - 1].z;
    unsigned short i;

    for (i = 0; i < count; i++) {
        s16 startX, startZ;
        int radius, vertexX, vertexZ;
        int endX, endZ;
        int length, distance, offsetX, offsetZ;

        startX = prevX;
        startZ = prevZ;
        radius = g_CollisionRadius;
        vertexX = vertices[i].x;
        prevX = vertexX;
        endX = prevX;
        vertexZ = vertices[i].z;
        prevZ = vertexZ;
        distance = pointX - radius;
        if (endX < distance && startX < distance)
            continue;
        distance = pointX + radius;
        if (distance < endX && distance < startX)
            continue;
        endZ = prevZ;
        distance = pointZ - radius;
        if (endZ < distance && startZ < distance)
            continue;
        distance = pointZ + radius;
        if (distance < endZ && distance < startZ)
            continue;
        edge.x = startX - endX;
        edge.y = 0;
        edge.z = startZ - endZ;
        length = SquareRoot0(edge.x * edge.x + edge.z * edge.z);
        offsetZ = pointZ - endZ;
        offsetX = pointX - endX;
        distance = (offsetZ * edge.x - offsetX * edge.z) / length;
        if (distance < 0)
            distance = -distance;
        if (distance > g_CollisionRadius)
            continue;
        VectorNormal(&edge, &edge);
        distance = Math_FixedMul(edge.x * 16, (s32)((u32)offsetX << 16));
        distance += Math_FixedMul(edge.z * 16, (s32)((u32)offsetZ << 16));
        if (distance < 0) {
            distance = offsetX * offsetX + offsetZ * offsetZ;
            if (distance > g_CollisionRadius * g_CollisionRadius)
                continue;
        }
        if (length < distance >> 16) {
            distance = (pointX - startX) * (pointX - startX) +
                       (pointZ - startZ) * (pointZ - startZ);
            if (distance > g_CollisionRadius * g_CollisionRadius)
                continue;
        }
        return i;
    }
    return -1;
}

/* Collision response uses the same active player as movement and scripts. */


void Entity_ApplyCollisionResponse(struct FieldActor *player)
{
    BattleEntity *actor = ((BattleEntity *)g_PlayerEntity);
    int oldX, oldZ;
    short edge;
    int radius = (u16)actor->renderObject.hit_cylinder.radius;

    g_CollisionRadius = radius;
    /* Preserve the low-word multiply and signed division used by retail. */
    g_CollisionRadius = (int)((long long)radius * actor->moveSpeed) / 4096;
    oldX = (short)(actor->baseX >> 16);
    oldZ = (short)(actor->baseZ >> 16);
    edge = Geo_FindNearestEdge(actor->posX.parts.integer, actor->posZ.parts.integer,
                              g_EntityCollisionWallSlot, g_EntityCollisionWallParam);
    if (edge >= 0) {
        BattleEntity *current;

        Entity_SlideOnWall(((BattleEntity *)g_PlayerEntity), g_EntityCollisionWallSlot, g_EntityCollisionWallParam, edge, oldX, oldZ);
        current = ((BattleEntity *)g_PlayerEntity);
        if ((short)Geo_FindNearestEdge(current->posX.parts.integer,
                                      current->posZ.parts.integer,
                                      g_EntityCollisionWallSlot, g_EntityCollisionWallParam) >= 0) {
            BattleEntity *restore = ((BattleEntity *)g_PlayerEntity);
            restore->posX.fixed = restore->baseX;
            restore->posY.fixed = restore->baseY;
            restore->posZ.fixed = restore->baseZ;
        }
    }
}

void Entity_CheckActionIdMatch(FieldActor *arg0, int arg1, FieldActor *arg2, int arg3) {
    EnemyCombatant *entry;
    int i;
    int kind;
    int offset;
    int value;

    (void)arg1;
    entry = (EnemyCombatant *)arg2->state;
    if (entry == 0) {
        return;
    }

    /* An absent effect reads an inactive byte at PS1 address zero. */
    if (!entry->effect || entry->effect->state != 1) {
        return;
    }

    kind = (entry->coreFlags >> 21) & 7;
    if (kind >= 3) {
        return;
    }

    if (entry->hpAlive <= 0) {
        return;
    }

    i = 0;
    arg3 = (short)arg3;
    do {
        offset = (entry->coreFlags >> 19) & 0x1C;
        value = (entry->contactActionIds + offset)[i & 0xFFFF];
        if (arg3 == value) {
            ((Combatant *)arg0->state)->stateFlags |= 0x4000;
            entry->coreFlags |= 0x80000000;
            return;
        }

        if (value < 0) {
            return;
        }

        i++;
    } while ((u16)i < 4);
}
