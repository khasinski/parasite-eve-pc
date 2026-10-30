#include "pe_hooks.h"
#include "pe1/geom_state.h"
#include "pe1/asset_tim.h"
#include "pe1/battle_runtime.h"
#include "pe1/field_movement.h"
#include "pe1/gte.h"
#include "pe1/gte_types.h"
#include "pe1/pad_state.h"
#include "pe1/player_entity.h"
#include "pe1/scene_flags.h"
#include "pe1/render_camera.h"

/* Game-side TIM container helpers: upload a TIM's image and optional CLUT,
 * and return its CLUT/image rectangles and pixel pointers. No Psy-Q
 * signature of any SDK version matches these bytes; they are linked as the
 * last game object, directly before the LIBC veneers. */

int *Asset_LoadTimImage(TimFile *tim) {
    TimBlock *clut;
    TimBlock *image;
    int *pixels;

    PE_HOOK(PE_HOOK_TIM_UPLOAD, tim);
    clut = 0;
    if ((tim->flags & 8) != 0) {
        clut = &tim->first_block;
        image = (TimBlock *)&clut->bytes[clut->header.length];
    } else {
        image = &tim->first_block;
    }

    pixels = image->header.pixels;
    LoadImage(&image->header.rect, pixels);
    if (clut != 0) {
        LoadImage(&clut->header.rect, clut->header.pixels);
    }
    return pixels;
}

RECT *Asset_GetTimClutRect(TimFile *tim) {
    if (tim->flags & 8) {
        return &tim->first_block.header.rect;
    }
    return 0;
}

static inline TimBlock *ImageBlock(TimFile *tim) {
    TimBlock *image;

    if (tim->flags & 8) {
        TimBlock *clut = &tim->first_block;
        image = (TimBlock *)&clut->bytes[clut->header.length];
    } else {
        image = &tim->first_block;
    }
    return image;
}

RECT *Asset_GetTimImageRect(TimFile *tim) {
    return &ImageBlock(tim)->header.rect;
}

int *Asset_GetTimImagePixels(TimFile *tim) {
    return ImageBlock(tim)->header.pixels;
}

int *Asset_GetTimClutPixels(TimFile *tim) {
    if (tim->flags & 8) {
        return tim->first_block.header.pixels;
    }
    return 0;
}

/* Field player movement state handlers: idle, walk, run, turn-in-place and
 * the idle resync. All five are contiguous, compile under the default
 * profile and share the pad/analog and camera-angle globals. */

/* Mods can scale this input step without persisting their setting in the
 * actor's script-controlled moveFactor. With mods off this is the retail value. */
static int Scene_PlayerMoveFactor(BattleEntity *entity, int running) {
    int factor = entity->moveFactor;
    PE_HOOK(PE_HOOK_PLAYER_MOVE,
        (&(PeHookPlayerMove){entity, &factor, running, (g_GameStateFlags & 2) != 0}));
    return factor;
}

void Scene_Init(BattleEntity *entity, int *actionState) {
    if (g_GameStateFlags & 2) {
        Scene_SyncEntityAction(entity, actionState);
        return;
    }
    if (*actionState != 0x15) {
        entity->motionX = 0;
        entity->motionY = 0;
        entity->motionZ = 0;
        Entity_SetActionMode(entity, 0x15);
        *actionState = 0x15;
    }
}

void Scene_UpdatePlayerEntity(BattleEntity *entity, int *actionState)
{
    GteVector movement = {0};
    int speed;
    int diagonal;
    int facing;
    u32 pad;

    if (g_GameStateFlags & 2) {
        Scene_UpdateEntityFacing(entity, actionState);
        return;
    }
    if (*actionState != 22) {
        Entity_SetActionMode(entity, 22);
        *actionState = 22;
    }
    speed = Math_FixedMul(0x50000, Scene_PlayerMoveFactor(entity, 0));
    speed = Math_FixedMul(speed, entity->moveSpeed << 4);
    if ((g_PadPackets[0].header & 0xF000) == 0x7000) {
        int angle;
        angle = ratan2(g_PadPackets[0].leftY - 128, g_PadPackets[0].leftX - 128) - 0x400;
        if (angle < 0) angle += 0x1000;
        entity->facingAngle = g_GeomScroll.state.position.baseAngleAnalog + angle;
        movement.x = Math_FixedMul(-speed, rsin(angle) * 16);
        movement.y = 0;
        movement.z = Math_FixedMul(-speed, rcos(angle) * 16);
    } else {
        diagonal = Math_FixedMul(speed, 0xB504);
        pad = g_FieldPadBits;
        entity->facingAngle = g_GeomScroll.state.position.baseAngleDigital;
        if (pad & FIELD_PAD_DOWN) {
            if (pad & FIELD_PAD_LEFT) {
                entity->facingAngle += 0x600;
                movement.x = -diagonal;
                movement.y = 0;
                movement.z = diagonal;
            } else if (pad & FIELD_PAD_RIGHT) {
                entity->facingAngle += 0xA00;
                movement.x = diagonal;
                movement.y = 0;
                movement.z = diagonal;
            } else {
                entity->facingAngle += 0x800;
                movement.x = 0;
                movement.y = 0;
                movement.z = speed;
            }
        } else if (pad & FIELD_PAD_UP) {
            if (pad & FIELD_PAD_LEFT) {
                entity->facingAngle += 0x200;
                movement.x = -diagonal;
                movement.y = 0;
                movement.z = -diagonal;
            } else if (pad & FIELD_PAD_RIGHT) {
                entity->facingAngle += 0xE00;
                movement.x = diagonal;
                movement.y = 0;
                movement.z = -diagonal;
            } else {
                entity->facingAngle = g_GeomScroll.state.position.baseAngleDigital;
                movement.x = 0;
                movement.y = 0;
                movement.z = -speed;
            }
        } else if (pad & FIELD_PAD_LEFT) {
            entity->facingAngle += 0x400;
            movement.x = -speed;
            movement.y = 0;
            movement.z = 0;
        } else if (pad & FIELD_PAD_RIGHT) {
            entity->facingAngle += 0xC00;
            movement.x = speed;
            movement.y = 0;
            movement.z = 0;
        }
    }
    facing = entity->facingAngle;
    if (((BattleEntity *)g_PlayerEntity) && (D_8009D2E8 & FIELD_MOVE_CONFUSED)) {
        u32 stateFlags = ((Combatant *)((BattleEntity *)g_PlayerEntity)->core)->stateFlags;

        facing += 0x800;
        facing += (stateFlags >> 7) & 0xC00;
    }
    facing &= 0xFFF;
    entity->facingAngle = facing;
    ApplyMatrixLV(&g_GeomScroll.state.position.cameraBasis, &movement, (GteVector *)&entity->motionX);
    if (D_8009D2E8 & FIELD_MOVE_CONFUSED) {
        entity->facingAngle = (0x1000 - (u16)entity->facingAngle) & 0xFFF;
        entity->motionX = -entity->motionX;
    }
}

void Scene_UpdateEntityFacing(BattleEntity *entity, int *actionState)
{
    GteVector movement = {0};
    int speed;
    int diagonal;
    int facing;
    u32 pad;

    speed = AYA_MOVE_SPEED_NORMAL;
    if (g_GameStateFlags & 2) {
        int flags = ((Combatant *)entity->core)->stateFlags;
        if ((flags & AYA_STIFFNESS_MASK) == AYA_STIFFNESS_SLOW) {
            speed = AYA_MOVE_SPEED_STIFF;
        } else if (flags & AYA_HASTE) {
            speed = AYA_MOVE_SPEED_HASTE;
        }
        {
            u8 mode;
            mode = (u8)((Combatant *)entity->core)->actionMode13;
            if (*actionState != mode) {
                Entity_SetActionMode(entity, mode);
                *actionState = (u8)((Combatant *)entity->core)->actionMode13;
            }
        }
    } else if (*actionState != 23) {
        Entity_SetActionMode(entity, 23);
        *actionState = 23;
    }
    speed = Math_FixedMul(speed, Scene_PlayerMoveFactor(entity, 1));
    speed = Math_FixedMul(speed, entity->moveSpeed << 4);
    if ((g_PadPackets[0].header & 0xF000) == 0x7000) {
        int angle;
        angle = ratan2(g_PadPackets[0].leftY - 128, g_PadPackets[0].leftX - 128) - 0x400;
        if (angle < 0) angle += 0x1000;
        entity->facingAngle = g_GeomScroll.state.position.baseAngleAnalog + angle;
        movement.x = Math_FixedMul(-speed, rsin(angle) * 16);
        movement.y = 0;
        movement.z = Math_FixedMul(-speed, rcos(angle) * 16);
    } else {
        diagonal = Math_FixedMul(speed, 0xB504);
        pad = g_FieldPadBits;
        entity->facingAngle = g_GeomScroll.state.position.baseAngleDigital;
        if (pad & FIELD_PAD_DOWN) {
            if (pad & FIELD_PAD_LEFT) {
                entity->facingAngle += 0x600;
                movement.x = -diagonal;
                movement.y = 0;
                movement.z = diagonal;
            } else if (pad & FIELD_PAD_RIGHT) {
                entity->facingAngle += 0xA00;
                movement.x = diagonal;
                movement.y = 0;
                movement.z = diagonal;
            } else {
                entity->facingAngle += 0x800;
                movement.x = 0;
                movement.y = 0;
                movement.z = speed;
            }
        } else if (pad & FIELD_PAD_UP) {
            if (pad & FIELD_PAD_LEFT) {
                entity->facingAngle += 0x200;
                movement.x = -diagonal;
                movement.y = 0;
                movement.z = -diagonal;
            } else if (pad & FIELD_PAD_RIGHT) {
                entity->facingAngle += 0xE00;
                movement.x = diagonal;
                movement.y = 0;
                movement.z = -diagonal;
            } else {
                entity->facingAngle = g_GeomScroll.state.position.baseAngleDigital;
                movement.x = 0;
                movement.y = 0;
                movement.z = -speed;
            }
        } else if (pad & FIELD_PAD_LEFT) {
            entity->facingAngle += 0x400;
            movement.x = -speed;
            movement.y = 0;
            movement.z = 0;
        } else if (pad & FIELD_PAD_RIGHT) {
            entity->facingAngle += 0xC00;
            movement.x = speed;
            movement.y = 0;
            movement.z = 0;
        }
    }
    facing = entity->facingAngle;
    if (((BattleEntity *)g_PlayerEntity) && (D_8009D2E8 & FIELD_MOVE_CONFUSED)) {
        u32 stateFlags = ((Combatant *)((BattleEntity *)g_PlayerEntity)->core)->stateFlags;

        facing += 0x800;
        facing += (stateFlags >> 7) & 0xC00;
    }
    facing &= 0xFFF;
    entity->facingAngle = facing;
    ApplyMatrixLV(&g_GeomScroll.state.position.cameraBasis, &movement, (GteVector *)&entity->motionX);
    if (D_8009D2E8 & FIELD_MOVE_CONFUSED) {
        entity->facingAngle = (0x1000 - (u16)entity->facingAngle) & 0xFFF;
        entity->motionX = -entity->motionX;
    }
}

void Scene_SyncEntityAction(BattleEntity *entity, int *actionState) {
    u8 mode;

    if (!(g_GameStateFlags & 2)) {
        if (*actionState != 0x15) {
            entity->motionX = 0;
            entity->motionY = 0;
            entity->motionZ = 0;
            Entity_SetActionMode(entity, 0x15);
            *actionState = 0x15;
        }
    } else {
        mode = ((Combatant *)entity->core)->actionMode12;
        if (*actionState != mode) {
            Entity_SetAction(entity, mode);
            entity->motionX = 0;
            entity->motionY = 0;
            entity->motionZ = 0;
            *actionState = ((Combatant *)entity->core)->actionMode12;
        }
    }
}

void Scene_UpdateEntityFacingFromPad(BattleEntity *entity, int *actionState) {
    (void)actionState;
    int angle;
    Entity_SetAction(entity, 0x11);
    if ((g_PadPackets[0].header & 0xF000) == 0x7000) {
        angle = ratan2(g_PadPackets[0].leftY - 128, g_PadPackets[0].leftX - 128) - 0x400;
        /* Keep the wrapped-angle correction based on the adjusted value. */
        if (angle < 0) angle += 0x1000;
        angle += g_GeomScroll.state.position.baseAngleAnalog;
    } else {
        if (g_FieldPadBits & FIELD_PAD_DOWN) {
            if (g_FieldPadBits & FIELD_PAD_LEFT) entity->facingAngle = 0x600;
            else if (g_FieldPadBits & FIELD_PAD_RIGHT) entity->facingAngle = 0xA00;
            else entity->facingAngle = 0x800;
        } else if (g_FieldPadBits & FIELD_PAD_UP) {
            if (g_FieldPadBits & FIELD_PAD_LEFT) entity->facingAngle = 0x200;
            else if (g_FieldPadBits & FIELD_PAD_RIGHT) entity->facingAngle = 0xE00;
            else entity->facingAngle = 0;
        } else if (g_FieldPadBits & FIELD_PAD_LEFT) entity->facingAngle = 0x400;
        else if (g_FieldPadBits & FIELD_PAD_RIGHT) entity->facingAngle = 0xC00;
        angle = entity->facingAngle + g_GeomScroll.state.position.baseAngleDigital;
    }
    if (((BattleEntity *)g_PlayerEntity) && (D_8009D2E8 & FIELD_MOVE_CONFUSED)) {
        angle += 0x800;
        {
            Combatant *core = ((BattleEntity *)g_PlayerEntity)->core;
            angle += ((unsigned int)core->stateFlags >> 7) & 0xC00;
        }
    }
    angle &= 0xFFF;
    entity->facingAngle = angle;
    /* Retail reloads both the mode flags and the stored facing angle. */
    if (D_8009D2E8 & FIELD_MOVE_CONFUSED) entity->facingAngle = (0x1000 - (u16)entity->facingAngle) & 0xFFF;
}


/* USA executable input records 0x800942F0..0x80094487, with host function
 * pointers. Preserve record order, fallbacks and shared table entries. */
static SceneFlagRecord mode15[] = {
    {3,1,0x78,Scene_UpdateEntityFacing},
    {2,0,0x78,Scene_UpdatePlayerEntity},
    {0,0,0,0}
};
static SceneFlagRecord mode16[] = {
    {3,1,0x78,Scene_UpdateEntityFacing},
    {2,0,0x78,Scene_UpdatePlayerEntity},
    {0,0,0,Scene_Init}
};
static SceneFlagRecord mode05[] = {
    {3,1,0x78,Scene_UpdateEntityFacing},
    {2,0,0x78,Scene_UpdatePlayerEntity},
    {0,0,0,Scene_Init}
};
static SceneFlagRecord mode04[] = {
    {2,0,0x78,Scene_UpdateEntityFacing}, {0,0,0,0}
};
static SceneFlagRecord mode11[] = {
    {2,0,0x78,Scene_UpdateEntityFacingFromPad}, {0,0,0,Scene_SyncEntityAction}
};
SceneFlagRecord *g_EntityInputRulesByMode[50] = {
    [4]=mode04, [5]=mode05, [16]=mode05, [17]=mode11,
    [21]=mode15, [22]=mode16, [23]=mode05
};
