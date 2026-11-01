#if PE_NATIVE_STATE_CPU_COMPONENTS
#include "pe_state_node_types.h"
#include "pe_state_lifetime.h"
#include "pe_state_actor_prepare.h"
#endif
#include "common.h"
#include "pe1/entity_allocation.h"
#include "pe1/entity_frame_update.h"
#include "pe1/render_buffers.h"
#include "pe1/script_node.h"

/* MIPS word arithmetic wraps before signed comparisons and shifts. */
static s32 signed_word(u32 value)
{
    return value <= INT32_MAX ? (s32)value : -1 - (s32)(UINT32_MAX - value);
}

typedef RenderAnimationLookupEntry HitSphere;

void Entity_RollbackPositionHierarchy(FieldActor *actor);

/* Start SELF's contact task for OTHER unless an active one already exists. */
static inline void Scene_LinkContactTask(FieldActor *self, FieldActor *other)
{
    ScriptNode *node;
    ScriptNode *head;
    int found;

    if (self->script_cursor_1a0 != 0) {
        found = 0;
        node = self->script_node_lists[1];
        while (!found) {
            if (node == 0) {
                break;
            }
            if ((node->flags & 1) && !(node->flags & 0x10) &&
                node->trigger_value == other->field_sfx_id) {
                found = 1;
            }
            node = node->next;
        }
        if (!found) {
            node = Script_AllocNode(self->script_cursor_1a0, 0);
            if (!node) return;
            node->flags |= 1;
            node->trigger_value = other->field_sfx_id;
            node->target18.coordinate = other->type_id;
#if PE_NATIVE_STATE_CPU_COMPONENTS

            PeStateNode_Tag(node, PE_NODE_COORDINATE);
#endif
            node->target1c = other->sub_id;
            head = self->script_node_lists[1];
            if (head != 0) {
                node->next = head;
                head->prev = node;
            }
            self->script_node_lists[1] = node;
        }
    }
}

/*
 * Per-frame actor contact pass. Clears the contact flags, then tests every
 * pair of active actors: body spheres first, then (for actors with an
 * allocation block) the x/z cylinders. Cylinder contact rolls back actors
 * moving into each other, restores their saved yaw and flags player contact;
 * touching or near-missing pairs start each other's contact task. In battle,
 * or when the cylinders are absent, the per-part hit spheres are tested and
 * the contact callbacks run (typed actors only flag 0x40000 instead).
 */
void Scene_UpdateEntityPositions(void)
{
    FieldActor *self;
    FieldActor *other;
    HitSphere *body;
    HitSphere *cylinder;
    HitSphere *selfPart;
    HitSphere *otherPart;
    int selfSide;
    int selfTop;
    int otherRadius;
    int selfRadius;
    int distance;
    int reach;
    int bx, by, bz; /* body sphere deltas, squared in place */
    int cx, cz;     /* cylinder deltas, squared in place */
    int px, py, pz; /* part sphere deltas, squared in place */
    unsigned int i;
    unsigned int j;
    int heading[2];
    int offset[2];
    int dot;

    for (self = g_FieldActorListHead; self != 0; self = self->next) {
        self->flags &= ~0x02040000;
    }

    for (self = g_FieldActorListHead; self != 0; self = self->next) {
        if (self->flags & 0x20) {
            continue;
        }
        body = &self->render_object.hit_body;
        cylinder = &self->render_object.hit_cylinder;
        selfSide = cylinder->radius * self->move_speed / 4096;
        selfTop = body->radius * self->move_speed / 4096;
        for (other = self->next; other != 0; other = other->next) {
            if (self->parent == other || other->parent == self) {
                continue;
            }
            if ((self->flags & 0x20000) && other != g_PlayerEntity) {
                continue;
            }
            if ((other->flags & 0x20000) && self != g_PlayerEntity) {
                continue;
            }
            if (other->flags & 0x20) {
                continue;
            }
            otherRadius = other->render_object.hit_body.radius * other->move_speed / 4096;
            bx = body->value0 - other->render_object.hit_body.value0;
            bx = signed_word((u32)bx * (u32)bx);
            by = body->value1 - other->render_object.hit_body.value1;
            by = signed_word((u32)by * (u32)by);
            bz = body->value2 - other->render_object.hit_body.value2;
            bz = signed_word((u32)bz * (u32)bz);
            reach = otherRadius + selfTop;
            reach = signed_word((u32)reach * (u32)reach);
            distance = signed_word((u32)bx + (u32)by + (u32)bz);
            if (self->model_state.allocation_active == 0 || other->model_state.allocation_active == 0) {
                if (distance <= reach) {
                link:
                    Scene_LinkContactTask(self, other);
                    Scene_LinkContactTask(other, self);
                }
                continue;
            }
            if (distance > reach) {
                continue;
            }
            if (cylinder->radius != 0 && other->render_object.hit_cylinder.radius != 0) {
                otherRadius = other->render_object.hit_cylinder.radius * other->move_speed / 4096;
                cx = cylinder->value0 - other->render_object.hit_cylinder.value0;
                cx = signed_word((u32)cx * (u32)cx);
                cz = cylinder->value2 - other->render_object.hit_cylinder.value2;
                cz = signed_word((u32)cz * (u32)cz);
                reach = otherRadius + selfSide;
                reach = signed_word((u32)reach * (u32)reach);
                distance = signed_word((u32)cx + (u32)cz);
                if (distance <= reach) {
                    heading[0] = signed_word((u32)self->pos_x - (u32)self->base_x) >> 16;
                    heading[1] = signed_word((u32)self->pos_z - (u32)self->base_z) >> 16;
                    offset[0] = cylinder->value0 - other->render_object.hit_cylinder.value0;
                    offset[1] = cylinder->value2 - other->render_object.hit_cylinder.value2;
                    dot = signed_word((u32)heading[0] * (u32)offset[0]);
                    dot = signed_word((u32)dot + (u32)heading[1] * (u32)offset[1]);
                    if (dot < 0) {
                        Entity_RollbackPositionHierarchy(self);
                    }
                    heading[0] = signed_word((u32)other->pos_x - (u32)other->base_x) >> 16;
                    heading[1] = signed_word((u32)other->pos_z - (u32)other->base_z) >> 16;
                    dot = signed_word((u32)heading[0] * (u32)offset[0]);
                    dot = signed_word((u32)dot + (u32)heading[1] * (u32)offset[1]);
                    if (dot > 0) {
                        Entity_RollbackPositionHierarchy(other);
                    }
                    if (self != g_PlayerEntity && !(self->flags & 0x01000000)) {
                        self->rot_y = self->saved_rot_y;
                    }
                    if (other != g_PlayerEntity && !(other->flags & 0x01000000)) {
                        other->rot_y = other->saved_rot_y;
                    }
                    if (self == g_PlayerEntity) {
                        other->flags |= 0x02000000;
                    } else if (other == g_PlayerEntity) {
                        self->flags |= 0x02000000;
                    }
                    Scene_LinkContactTask(self, other);
                    Scene_LinkContactTask(other, self);
                    if (!(g_GameStateBlock.flags & 2)) {
                        continue;
                    }
                } else if (!(g_GameStateBlock.flags & 2) && distance <= signed_word((u32)reach + 10000u)) {
                    /* Recorded crutch debt: retail's near miss jumps back into
                     * the contact-task block of the no-allocation case; no
                     * structured form places that block first. */
                    goto link;
                }
            }
            if (self->type_id != 0 && other->type_id != 0) {
                self->flags |= 0x40000;
                other->flags |= 0x40000;
                continue;
            }
            selfPart = self->render_object.animation_entries;
            for (i = 0; i < self->render_object.header->animation_entry_count; i++, selfPart++) {
                selfRadius = selfPart->radius * self->move_speed / 4096;
                otherPart = other->render_object.animation_entries;
                for (j = 0; j < other->render_object.header->animation_entry_count; j++, otherPart++) {
                    otherRadius = otherPart->radius * other->move_speed / 4096;
                    px = selfPart->value0 - otherPart->value0;
                    px = signed_word((u32)px * (u32)px);
                    py = selfPart->value1 - otherPart->value1;
                    py = signed_word((u32)py * (u32)py);
                    pz = selfPart->value2 - otherPart->value2;
                    pz = signed_word((u32)pz * (u32)pz);
                    reach = otherRadius + selfRadius;
                    reach = signed_word((u32)reach * (u32)reach);
                    distance = signed_word((u32)px + (u32)py + (u32)pz);
                    if (distance <= reach) {
                        if (self->contact_callback != 0) {
                            self->contact_callback(self, selfPart->animation_id, other,
                                                   otherPart->animation_id);
                        }
                        if (other->contact_callback != 0) {
                            other->contact_callback(other, otherPart->animation_id, self,
                                                    selfPart->animation_id);
                        }
                    }
                }
            }
        }
    }
}


void *Entity_AllocBlock(u32 size) {
#if PE_NATIVE_STATE_CPU_COMPONENTS
    void *restored=0;int prepared=PeStateActor_ConsumeAllocation(size,&restored);
    if(prepared)return prepared>0?restored:0;
#endif
    s32 blocks;
    u8 slot;
    u32 count;
    u8 *scratch;
    u32 offset;
    u32 end;
    s32 free;
    u32 used;

    blocks = 8;
    if (size <= ENTITY_ALLOCATION_BLOCK_SIZE) {
        blocks = 1;
    } else if (size <= ENTITY_ALLOCATION_BLOCK_SIZE * 2) {
        blocks = 2;
    } else if (size <= ENTITY_ALLOCATION_BLOCK_SIZE * 4) {
        blocks = 4;
    }
    slot = 0;
    count = blocks & 0xFF;
    scratch = g_RenderScratchBufferBase[0];
    if (!scratch) return 0;
    while (slot < 0x10U) {
        offset = slot;
        if (g_EntityAllocationTable[slot].address == 0) {
            free = 1;
            if (slot < slot + count) {
                end = blocks + slot;
                if (end > 16) return 0;
                do {
                    EntityAllocationBlock *run = &g_EntityAllocationTable[offset];
                    if (run->address != 0) {
                        free = 0;
                        slot += blocks;
                        break;
                    }
                    offset++;
                } while (offset < end);
            }
            if (free != 0) {
                uintptr_t address;

                g_EntityAllocationTable[slot].blockCount = count;
                address = (uintptr_t)(scratch + slot * ENTITY_ALLOCATION_BLOCK_SIZE);
                g_EntityAllocationTable[slot].address = address;
#if PE_NATIVE_STATE_CPU_COMPONENTS
                PeStateResource_Changed();
#endif
                return (void *)address;
            }
        } else {
            used = g_EntityAllocationTable[slot].blockCount;
            if (count < used) {
                slot += used;
            } else {
                slot += blocks;
            }
        }
    }
    return 0;
}

void Entity_FreeAllocationBlock(uintptr_t arg0) {
    int i;

    i = 0;
    while ((unsigned int)(i & 0xFF) < 0x10U) {
        if (g_EntityAllocationTable[i & 0xFF].address == arg0) {
            g_EntityAllocationTable[i & 0xFF].address = 0;
#if PE_NATIVE_STATE_CPU_COMPONENTS
            PeStateResource_Changed();
#endif
            return;
        }
        i++;
    }
}
