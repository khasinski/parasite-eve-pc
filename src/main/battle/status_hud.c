#include "common.h"
#include "pe1/battle.h"
#include "pe1/field_actor.h"
#include "pe1/battle_entity_anim.h"
#include "pe1/battle_main.h"
#include "pe1/battle_runtime.h"
#include "pe1/battle_status.h"
#include "pe1/battle_update.h"
#include "pe1/draw_buffers.h"
#include "pe1/draw_state.h"
#include "pe1/game_state.h"
#include "pe1/menu_state.h"
#include "pe1/psyq_gpu.h"
#include "pe1/render_animation_frame.h"
#include "pe1/render_buffers.h"
#include "pe1/render_packets.h"
#include "pe1/render_prim.h"
#include "pe1/script_state.h"

/* Shared state of the original HUD module. */
s32 g_BattleStatusPanelSlot;
u8 g_BattleSpritePrimCountdown;
u8 g_MenuActiveMode;
BattleHudOrigin g_HudOrigin;
int g_BattleAttackAnimFrame;

void Draw_InitDrawModeSprtPacket(void *, int);
void Draw_InitDrawModeTilePacket(void *, int);
void SetSemiTrans(void *, int);
void SetShadeTex(void *, int);
void SetPolyF3(void *);
void SetPolyFT4(RenderTexturedQuad *);
void SetTile(void *);
u16 GetClut(int, int);

/* Initialize the HUD packet pools for both draw buffers. The legacy symbol
 * name is retained; this routine sets up more than the HP bar.
 * Sprite fields and packet headers refer to the same native packet pools.
 */
void Battle_DrawHPBar(void) {
    BattleHudShades shades;
    s32 i;
    s32 j;
    u16 pageCode;
    u32 tileIndex;
    s32 highBlue;
    u8 glyphHeight;
    u8 markerV;
    u8 bufferIndex;
    u16 hudClut;
    RenderLinePacket *upperLine;
    RenderLinePacket *lowerLine;
    RenderTexturedQuad *labelQuad;
    s32 hudPage;
    u8 shade;
    RenderSpritePacket *hpLabel;
    RenderSpritePacket *pointerSprite;
    RenderSpritePacket *ammoIcon;
    RenderSpritePacket *statusIcon;
    RenderSpritePacket *ammoLabel;
    RenderSpritePacket *weaponIcon;
    u8 gridRow;
    RenderSpritePacket *cursorSprite;
    RenderColorTilePacket *background;
    RenderGouraudQuad *atbGradient;
    RenderColorTilePacket *atbTrack;
    RenderSpritePacket *atbMarker;
    RenderGouraudQuad *greenGradient;
    RenderSpritePacket *peLabel;
    RenderGouraudQuad *pinkGradient;
    DrawGlyphDescriptor *glyph;
    RenderSpritePacket *atbLabel;

    shades = g_BattleStatusShades[0];
    hudPage = GetTPage(0, 1, 0x100, 0x1E0);
    hudClut = GetClut(0x130, 0x1F8);
    bufferIndex = 0;
    do {
        glyph = Draw_LookupGlyphDescriptor(0x8B);
        labelQuad = &g_BattleEndPanels[bufferIndex];
        SetPolyFT4(labelQuad);
        labelQuad->u0 = glyph->u;
        labelQuad->v0 = glyph->v;
        labelQuad->u1 = glyph->u + glyph->width;
        labelQuad->v1 = glyph->v;
        labelQuad->u2 = glyph->u;
        labelQuad->v2 = glyph->v + glyph->height;
        labelQuad->u3 = glyph->u + glyph->width;
        labelQuad->v3 = glyph->v + glyph->height;
        g_BattleEndPanels[bufferIndex].tpage = GetTPage(0, 0, 0x1C0, 0);
        g_BattleEndPanels[bufferIndex].clut = glyph->clut;
        labelQuad->x0 = 0;
        highBlue = 0x82;
        labelQuad->y0 = 0;
        labelQuad->x1 = 0;
        labelQuad->y1 = 0;
        labelQuad->x2 = 0;
        labelQuad->y2 = 0;
        labelQuad->x3 = 0;
        labelQuad->y3 = 0;
        labelQuad->color.bytes.r = 0;
        labelQuad->color.bytes.g = 0;
        labelQuad->color.bytes.b = 0;
        SetSemiTrans(labelQuad, 1);
        {
            u8 buffer;
            buffer = bufferIndex;

            for (i = 0; (u8)i < 10; i++) {
                gridRow = (u8)i;
                for (j = 0; (u8)j < 4; j++) {
                    Draw_InitDrawModeSprtPacket(&g_BattlePanelDigits[buffer][gridRow][(u8)j], hudPage);
                    g_BattlePanelDigits[buffer][gridRow][(u8)j].sprite.clut = hudClut;
                }
            }
        }
        pageCode = GetTPage(0, 0, 0, 0);
        Draw_InitDrawModeTilePacket(&g_BattleStatusBackgrounds[bufferIndex], pageCode);
        background = &g_BattleStatusBackgrounds[bufferIndex].tile;
        background->r0 = 0x30;
        background->g0 = 0x30;
        background->b0 = 0x30;
        SetSemiTrans(background, 1);
        atbTrack = &g_BattleAtbTracks[bufferIndex];
        SetTile(atbTrack);
        atbGradient = &g_BattleAtbGauges[bufferIndex];
        atbTrack->r0 = 0x1D;
        atbTrack->g0 = 0x3E;
        atbTrack->b0 = 0x32;
        atbTrack->w = 0x38;
        atbTrack->h = 3;
        SetPolyG4(atbGradient);
        Draw_InitDrawModeSprtPacket(&g_BattleAtbMarkers[bufferIndex], hudPage);
        atbMarker = &g_BattleAtbMarkers[bufferIndex].sprite;
        atbMarker->u = 0xC8;
        atbMarker->v = 0xE0;
        g_BattleAtbMarkers[bufferIndex].sprite.clut = hudClut;
        atbMarker->width = 4;
        atbMarker->height = 8;
        atbGradient->b0 = highBlue;
        atbGradient->g1 = 0xFF;
        atbGradient->r0 = 0;
        atbGradient->g0 = 0x46;
        atbGradient->r1 = 0x9F;
        atbGradient->b2 = highBlue;
        atbGradient->b1 = 0xF9;
        atbGradient->r2 = 0;
        atbGradient->g2 = 0x46;
        atbGradient->r3 = 0x9F;
        atbGradient->g3 = 0xFF;
        atbGradient->b3 = 0xF9;
        atbMarker->color.bytes.r = 0x9F;
        atbMarker->color.bytes.g = 0xFF;
        atbMarker->color.bytes.b = 0xF9;

        for (i = 0; (u8)i < 4; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleHpDigits[bufferIndex][(u8)i], hudPage);
            g_BattleHpDigits[bufferIndex][(u8)i].sprite.clut = hudClut;
            g_BattleHpDigits[bufferIndex][(u8)i].sprite.width = 6;
            g_BattleHpDigits[bufferIndex][(u8)i].sprite.height = 0xA;
        }
        for (i = 0; (u8)i < 5; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleMaxHpDigits[bufferIndex][(u8)i], hudPage);
            g_BattleMaxHpDigits[bufferIndex][(u8)i].sprite.clut = hudClut;
            g_BattleMaxHpDigits[bufferIndex][(u8)i].sprite.width = 6;
            g_BattleMaxHpDigits[bufferIndex][(u8)i].sprite.height = 0xA;
        }
        greenGradient = &g_BattlePeGauges[bufferIndex][0];
        SetPolyG4(greenGradient);
        pinkGradient = &g_BattlePeGauges[bufferIndex][1];
        SetPolyG4(pinkGradient);
        {
            u8 red;
            u8 blue;
            red = 0x4A;
            blue = 0x3B;
            greenGradient->g0 = highBlue;
            greenGradient->r0 = 0;
            greenGradient->b0 = 0x36;
            greenGradient->r1 = red;
            greenGradient->g1 = 0xFF;
            greenGradient->b1 = blue;
            greenGradient->g2 = highBlue;
            greenGradient->r2 = 0;
            greenGradient->b2 = 0x36;
            greenGradient->r3 = red;
            greenGradient->g3 = 0xFF;
            greenGradient->b3 = blue;
        }
        BATTLE_SET_GAUGE_GRADIENT(*pinkGradient, 0xFF, 0x3D, 0x81, 0x83, 0x13, 1);
        Draw_InitDrawModeSprtPacket(&g_BattleAtbLabels[bufferIndex], hudPage);
        atbLabel = &g_BattleAtbLabels[bufferIndex].sprite;
        SetShadeTex(atbLabel, 1);
        markerV = 0xF4;
        atbLabel->u = 0x50;
        atbLabel->v = markerV;
        BATTLE_SET_SPRITE_COLOR(*atbLabel, 0x80, 0x80, 0x80);
        g_BattleAtbLabels[bufferIndex].sprite.clut = hudClut;
        atbLabel->width = 8;
        atbLabel->height = 4;
        Draw_InitDrawModeSprtPacket(&g_BattleHpLabels[bufferIndex], hudPage);
        hpLabel = &g_BattleHpLabels[bufferIndex].sprite;
        SetShadeTex(hpLabel, 1);
        hpLabel->u = 0x58;
        hpLabel->v = markerV;
        BATTLE_SET_SPRITE_COLOR(*hpLabel, 0x80, 0x80, 0x80);
        g_BattleHpLabels[bufferIndex].sprite.clut = hudClut;
        hpLabel->width = 8;
        hpLabel->height = 4;
        Draw_InitDrawModeSprtPacket(&g_BattlePeLabels[bufferIndex], hudPage);
        peLabel = &g_BattlePeLabels[bufferIndex].sprite;
        SetShadeTex(peLabel, 1);
        peLabel->u = 0x60;
        peLabel->v = markerV;
        BATTLE_SET_SPRITE_COLOR(*peLabel, 0x80, 0x80, 0x80);
        g_BattlePeLabels[bufferIndex].sprite.clut = hudClut;
        peLabel->width = 8;
        peLabel->height = 4;

        for (i = 0; (u8)i < 3; i++) {
            tileIndex = (u8)i;
            SetTile(&BATTLE_STATUS_TILES[bufferIndex][tileIndex]);
            BATTLE_STATUS_TILES[bufferIndex][tileIndex].r0 = shade = shades.values[tileIndex];
            BATTLE_STATUS_TILES[bufferIndex][tileIndex].g0 = shade;
            BATTLE_STATUS_TILES[bufferIndex][tileIndex].b0 = shade;
        }
        Draw_InitDrawModeSprtPacket(&g_BattleStatusPointerSprites[bufferIndex], hudPage);
        upperLine = &g_BattleStatusPointerLines[bufferIndex][0];
        pointerSprite = &g_BattleStatusPointerSprites[bufferIndex].sprite;
        pointerSprite->u = 0xE8;
        pointerSprite->v = 0xE0;
        g_BattleStatusPointerSprites[bufferIndex].sprite.clut = hudClut;
        pointerSprite->width = 0x18;
        pointerSprite->height = 0x18;
        SetLineF2(upperLine);
        lowerLine = &g_BattleStatusPointerLines[bufferIndex][1];
        SetLineF2(lowerLine);
        upperLine->r = 0xE0;
        upperLine->g = 0xE0;
        upperLine->b = 0xE0;
        lowerLine->r = 0x60;
        lowerLine->g = 0x60;
        lowerLine->b = 0x60;
        SetPolyF3(&g_BattleStatusPointerTriangles[bufferIndex]);
        for (i = 0; (u8)i < 3; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleTargetHighlightSprites[bufferIndex][(u8)i], hudPage);
            g_BattleTargetHighlightSprites[bufferIndex][(u8)i].sprite.clut = hudClut;
            g_BattleTargetHighlightSprites[bufferIndex][(u8)i].sprite.width = 0x18;
            g_BattleTargetHighlightSprites[bufferIndex][(u8)i].sprite.height = 8;
            BATTLE_SET_SPRITE_COLOR(g_BattleTargetHighlightSprites[bufferIndex][(u8)i].sprite,
                                    0x80, 0x80, 0x80);
        }
        for (i = 0; (u8)i < 10; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleRemainingActionSprites[bufferIndex][(u8)i], hudPage);
            BATTLE_SET_SPRITE_COLOR(g_BattleRemainingActionSprites[bufferIndex][(u8)i].sprite,
                                    0x80, 0x80, 0x80);
        }
        Draw_InitDrawModeSprtPacket(&g_BattleAmmoLabelSprites[bufferIndex], hudPage);
        ammoLabel = &g_BattleAmmoLabelSprites[bufferIndex].sprite;
        ammoLabel->u = 0x58;
        ammoLabel->v = 0xEF;
        g_BattleAmmoLabelSprites[bufferIndex].sprite.clut = hudClut;
        ammoLabel->width = 0x24;
        ammoLabel->height = 5;
        BATTLE_SET_SPRITE_COLOR(*ammoLabel, 0x80, 0x80, 0x80);
        for (i = 0; (u8)i < 4; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleAmmoDigitSprites[bufferIndex][(u8)i], hudPage);
            g_BattleAmmoDigitSprites[bufferIndex][(u8)i].sprite.clut = hudClut;
            g_BattleAmmoDigitSprites[bufferIndex][(u8)i].sprite.width = 6;
            g_BattleAmmoDigitSprites[bufferIndex][(u8)i].sprite.height = 6;
            BATTLE_SET_SPRITE_COLOR(g_BattleAmmoDigitSprites[bufferIndex][(u8)i].sprite,
                                    0x80, 0x80, 0x80);
        }
        Draw_InitDrawModeSprtPacket(&g_BattleStatusValueLabelSprites[bufferIndex], hudPage);
        statusIcon = &g_BattleStatusValueLabelSprites[bufferIndex].sprite;
        statusIcon->u = 0x68;
        statusIcon->v = 0xF4;
        g_BattleStatusValueLabelSprites[bufferIndex].sprite.clut = GetClut(0x130, 0x1F9);
        statusIcon->width = 0x18;
        statusIcon->height = 4;
        BATTLE_SET_SPRITE_COLOR(*statusIcon, 0x80, 0x80, 0x80);
        Draw_InitDrawModeSprtPacket(&g_BattleStatusValueMarkers[bufferIndex], hudPage);
        ammoIcon = &g_BattleStatusValueMarkers[bufferIndex].sprite;
        ammoIcon->u = 0x7C;
        ammoIcon->v = 0xEF;
        g_BattleStatusValueMarkers[bufferIndex].sprite.clut = hudClut;
        ammoIcon->width = 0x24;
        ammoIcon->height = 5;
        BATTLE_SET_SPRITE_COLOR(*ammoIcon, 0x80, 0x80, 0x80);
        for (i = 0; (u8)i < 2; i++) {
            Draw_InitDrawModeSprtPacket(&g_BattleStatusValueDigits[bufferIndex][(u8)i], hudPage);
            g_BattleStatusValueDigits[bufferIndex][(u8)i].sprite.clut = hudClut;
            g_BattleStatusValueDigits[bufferIndex][(u8)i].sprite.width = 6;
            g_BattleStatusValueDigits[bufferIndex][(u8)i].sprite.height = 6;
        }
        Draw_InitDrawModeSprtPacket(&g_BattleStatusSymbolSprites[bufferIndex], hudPage);
        g_BattleStatusSymbolSprites[bufferIndex].sprite.clut = hudClut;
        weaponIcon = &g_BattleStatusSymbolSprites[bufferIndex].sprite;
        BATTLE_SET_SPRITE_COLOR(*weaponIcon, 0x80, 0x80, 0);

        for (i = 0; (u8)i < 13; i++) {
            glyph = Draw_LookupGlyphDescriptor(((u8)i) + 0x6A);
            pageCode = GetTPage(0, 0, 0x1C0, 0);
            Draw_InitDrawModeSprtPacket(&g_StatusIconPackets[bufferIndex][(u8)i], pageCode);
#define GLYPH_SPRITE g_StatusIconPackets[bufferIndex][(u8)i].sprite
            GLYPH_SPRITE.u = glyph->u;
            GLYPH_SPRITE.v = glyph->v;
            g_StatusIconPackets[bufferIndex][(u8)i].sprite.clut = glyph->clut;
            GLYPH_SPRITE.width = glyph->width;
            glyphHeight = glyph->height;
            BATTLE_SET_SPRITE_COLOR(GLYPH_SPRITE, 0x80, 0x80, 0x80);
            GLYPH_SPRITE.height = glyphHeight;
#undef GLYPH_SPRITE
        }
        pageCode = GetTPage(0, 0, 0x1C0, 0);
        Draw_InitDrawModeSprtPacket(&g_StatusMarkerPackets[bufferIndex], pageCode);
        cursorSprite = &g_StatusMarkerPackets[bufferIndex].sprite;
        bufferIndex += 1;
        cursorSprite->width = 0x10;
        cursorSprite->height = 0x10;
        BATTLE_SET_SPRITE_COLOR(*cursorSprite, 0x80, 0x80, 0x80);
    } while (bufferIndex < 2U);
}

void Battle_BuildStatusPrimHeader(RenderObjectEntity *, s8);
void Battle_DrawSpellName(void);
void Battle_DrawStatusSymbol(s8);
void Battle_DrawStatusOverlay(RenderObjectEntity *object, int mode, int outOfRange, int entityId) {
    int modeArg = mode;
    if ((s8)modeArg != 8) {
        int modeMinusOne = modeArg - 1;
        if ((u8)modeMinusOne < 2) {
            u32 slot = g_ActiveDrawSlot;
            g_BattleStatusBarTiles[slot * 3].x0 = 0x78;
            g_BattleStatusBarTiles[slot * 3].y0 = 0x68;
        } else {
            Battle_BuildStatusPrimHeader(object, (s8)outOfRange);
        }
        switch ((s8)mode) {
        case 0:
            Battle_LayoutStatusPrimRow(0x28);
            Battle_DrawSpellName();
            Battle_DrawATBGauge();
            Battle_DrawStatusValue((s16)entityId, 0x1E);
            break;
        case 1:
        case 2:
            Battle_LayoutStatusPrimRow(0x20);
            Battle_DrawStatusSymbol((s8)mode);
            Battle_DrawATBGauge();
            break;
        case 3: {
            int slot = g_ActiveDrawSlot;
            RenderPagedSpritePacket *packet = &g_BattleStatusValueLabelSprites[slot];
            Battle_LayoutStatusPrimRow(0x1C);
            packet->sprite.x = g_BattleStatusBarTiles[slot * 3].x0 + 0x1C;
            packet->sprite.y = g_BattleStatusBarTiles[slot * 3].y0 + 8;
            AddPrim(D_800B0E38.ordering[slot] + 4, packet);
            Battle_DrawStatusValue((s16)entityId, 0x12);
            break;
        }
        case 4:
            Battle_LayoutStatusPrimRow(0x28);
            Battle_DrawStatusSymbol((s8)mode);
            Battle_DrawATBGauge();
            Battle_DrawStatusValue((s16)entityId, 0x1E);
            break;
        case 5:
        case 6:
        case 7:
            Battle_LayoutStatusPrimRow(0x20);
            Battle_DrawStatusSymbol((s8)mode);
            Battle_DrawStatusValue((s16)entityId, 0x16);
            break;
        }
    } else {
        u32 slot = g_ActiveDrawSlot;
        g_BattleStatusBarTiles[slot * 3].x0 = 0x78;
        g_BattleStatusBarTiles[slot * 3].y0 = 0x6C;
        Battle_LayoutStatusPrimRow(0x18);
        Battle_DrawStatusSymbol(8);
    }
    if (((BattleTurnBits *)&g_ActiveActor->action->turnWord)->slot >= 2)
        Battle_DrawTargetHighlight();
}

/* Builds the rotating target pointer and the two connector lines that tie
 * the floating status panel to the selected combatant. */

/* Packets of the active draw slot. Every access re-reads the slot, which
 * retail does after each byte store. */
#define STATUS_POINTER_SPRITE (&g_BattleStatusPointerSprites[g_ActiveDrawSlot].sprite)
#define STATUS_POINTER_TRI (&g_BattleStatusPointerTriangles[g_ActiveDrawSlot])

void Battle_BuildStatusPrimHeader(RenderObjectEntity *object, s8 outOfRange)
{
    GteMatrix rotation;
    GteMatrix translation;
    GteShortVector pointer[3] = { { -3, -16, 0 }, { 3, -16, 0 }, { 0, 0, 0 } };
    GteVector projected[3];
    GteShortVector angles = { 0, 0, (g_FrameRngCounter << 6) & 0xFFF };
    GteVector offset = { object->projected_target_x, object->projected_target_y, 0 };
    s32 flag;
    s16 x;
    s16 y;

    STATUS_POINTER_SPRITE->x = object->projected_target_x - 12;
    STATUS_POINTER_SPRITE->y = object->projected_target_y - 12;
    if (outOfRange == 0) {
        STATUS_POINTER_SPRITE->color.bytes.r = 0x96;
        STATUS_POINTER_SPRITE->color.bytes.g = 0x14;
        STATUS_POINTER_SPRITE->color.bytes.b = 0x14;
        STATUS_POINTER_TRI->r = 0x96;
        STATUS_POINTER_TRI->g = 0x14;
        STATUS_POINTER_TRI->b = 0x14;
    } else {
        STATUS_POINTER_SPRITE->color.bytes.r = 0x32;
        STATUS_POINTER_SPRITE->color.bytes.g = 0xA;
        STATUS_POINTER_SPRITE->color.bytes.b = 0xA;
        STATUS_POINTER_TRI->r = 0x32;
        STATUS_POINTER_TRI->g = 0xA;
        STATUS_POINTER_TRI->b = 0xA;
    }

    RotMatrix(&angles, &rotation);
    SetRotMatrix(&rotation);
    TransMatrix(&translation, &offset);
    SetTransMatrix(&translation);
    RotTrans(&pointer[0], &projected[0], &flag);
    RotTrans(&pointer[1], &projected[1], &flag);
    RotTrans(&pointer[2], &projected[2], &flag);
    STATUS_POINTER_TRI->x0 = projected[0].x;
    STATUS_POINTER_TRI->y0 = projected[0].y;
    STATUS_POINTER_TRI->x1 = projected[1].x;
    STATUS_POINTER_TRI->y1 = projected[1].y;
    STATUS_POINTER_TRI->x2 = projected[2].x;
    STATUS_POINTER_TRI->y2 = projected[2].y;
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5,
            (unsigned int *)STATUS_POINTER_TRI);

    /* Connector lines run to the panel on whichever side has room. */
    x = object->projected_target_x;
    if (x + 134 >= 301) {
        RenderLinePacket *lines;
        s16 panelX;

        x -= 8;
        lines = g_BattleStatusPointerLines[g_ActiveDrawSlot];
        lines[1].x0 = x;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].x0 = x;
        panelX = object->projected_target_x;
        lines[1].x1 = panelX - 35;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].x1 = panelX - 35;
        g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].x0 = panelX - 115;
    } else {
        RenderLinePacket *lines;
        s16 lineX;
        s16 panelX;

        lineX = x + 8;
        lines = g_BattleStatusPointerLines[g_ActiveDrawSlot];
        lines[1].x0 = lineX;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].x0 = lineX;
        panelX = object->projected_target_x + 35;
        g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].x0 = panelX;
        lines[1].x1 = panelX;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].x1 = panelX;
    }
    y = object->projected_target_y;
    if (y - 82 < 0) {
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].y0 = y + 8;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][1].y0 = object->projected_target_y + 9;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].y1 = g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].y0 =
            object->projected_target_y + 35;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][1].y1 = object->projected_target_y + 36;
    } else {
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].y0 = y - 8;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][1].y0 = object->projected_target_y - 7;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][0].y1 = g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].y0 =
            object->projected_target_y - 35;
        g_BattleStatusPointerLines[g_ActiveDrawSlot][1].y1 = object->projected_target_y - 34;
    }
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 7,
            (unsigned int *)&g_BattleStatusPointerLines[g_ActiveDrawSlot][1]);
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 6,
            (unsigned int *)&g_BattleStatusPointerLines[g_ActiveDrawSlot][0]);
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5,
            (unsigned int *)&g_BattleStatusPointerSprites[g_ActiveDrawSlot]);
}

void Battle_LayoutStatusPrimRow(int bottomY)
{
    int slot = g_ActiveDrawSlot;
    for (int row = 0; row < 3; ++row) {
        BattleStatusLinePrim *line = &g_BattleStatusBarTiles[slot * 3 + row];
        line->x0 = g_BattleStatusBarTiles[slot * 3].x0 + row;
        line->y0 = g_BattleStatusBarTiles[slot * 3].y0 + row;
        line->x1 = 0x50 - row * 2;
        line->y1 = (s8)bottomY - row * 2;
        AddPrim(D_800B0E38.ordering[slot] + 7 - row, line);
    }
}

static inline RenderSpritePacket *HighlightSpriteAt(RenderSpritePacket *base, int slot, unsigned byte_offset)
{
    BattleStatusPrimAddress at;

    at.sprite = base;
    at.address = slot * 3 * sizeof(BattleGaugePrim) + byte_offset + at.address;
    return at.sprite;
}

void Battle_DrawTargetHighlight(void)
{
    unsigned char index = 0;
    RenderSpritePacket *base;
    RenderSpritePacket *sprite;
    BattleGaugePrim *packets;
    int slot;
    unsigned slot_offset;
    unsigned first_offset;
    unsigned index_offset;
    unsigned char texture_v;
    if (((g_ActiveActor->action->turnWord >> 4) & 3) == 0) {
        return;
    }
    texture_v = 224;
    base = &g_BattleTargetHighlightSprites[0][0].sprite;
    packets = (BattleGaugePrim *)((char *)base - PE1_OFFSETOF(BattleGaugePrim, sprite));
    do {
        if (index == g_BattleActiveTurnSlot - 1) {
            first_offset = index * sizeof(BattleGaugePrim);
            HighlightSpriteAt(base, g_ActiveDrawSlot, first_offset)->u = index * 24 + 104;
        } else {
            first_offset = index * sizeof(BattleGaugePrim);
            HighlightSpriteAt(base, g_ActiveDrawSlot, first_offset)->u = 176;
        }
        HighlightSpriteAt(base, g_ActiveDrawSlot, first_offset)->v = texture_v;
        /* Preserve the retail store before recomputing the next packet offset. */
        index_offset = index * sizeof(BattleGaugePrim);
        slot = g_ActiveDrawSlot;
        slot_offset = slot * 3 * sizeof(BattleGaugePrim);
        {
            BattleStatusPrimAddress at;

            at.sprite = base;
            at.address = slot_offset + index_offset + at.address;
            sprite = at.sprite;
        }
        sprite->x = g_BattleStatusBarTiles[slot * 3].x0 + index * 24;
        sprite->y = g_BattleStatusBarTiles[slot * 3].y0 - 8;
        {
            BattleStatusPrimAddress at;

            at.gauge = packets;
            at.address = slot_offset + (index_offset + at.address);
            AddPrim(D_800B0E38.ordering[slot] + 7, at.words);
        }
        ++index;
    } while (index < (int)((g_ActiveActor->action->turnWord >> 4) & 3));
}

#define NULL ((void *)0)

void Battle_DrawSpellName(void) {
    int slot = g_ActiveDrawSlot;
    int y = g_BattleStatusBarTiles[slot * 3].y0 + 4;
    BattleAction *action = g_ActiveActor->action;
    if ((action->attackWord & 0x300000) == 0x200000) {
        RenderPagedSpritePacket *packet = &g_BattleRemainingActionSprites[slot][0];
        packet->sprite.u = 0xE0;
        packet->sprite.v = 0xE0;
        packet->sprite.width = 8;
        packet->sprite.height = 16;
        packet->sprite.x = g_BattleStatusBarTiles[slot * 3].x0 + 0x3C;
        packet->sprite.y = y;
        packet->sprite.clut = GetClut(0x130, 0x1FD);
        AddPrim((RenderNativeTag *)g_OtBufferTable[slot] + 4, packet);
        return;
    }
    int x = g_BattleStatusBarTiles[slot * 3].x0 + 0x40;
    for (int i = 0; i < g_BattleRemainingActions && i < 10; ++i) {
        RenderPagedSpritePacket *packet = &g_BattleRemainingActionSprites[slot][i];
        packet->sprite.width = 4;
        packet->sprite.height = 16;
        packet->sprite.v = 0xE0;
        if ((action->attackWord & 0x300000) == 0x300000) {
            packet->sprite.u = 0xD8;
            packet->sprite.clut = GetClut(0x130, 0x1FB);
        } else if ((action->turnWord & 0xC0) == 0x80) {
            packet->sprite.u = 0xDC;
            packet->sprite.clut = GetClut(0x130, 0x1FC);
        } else {
            packet->sprite.u = 0xD4;
            packet->sprite.clut = GetClut(0x130, 0x1FA);
        }
        packet->sprite.x = x;
        packet->sprite.y = y;
        x -= 6;
        AddPrim((RenderNativeTag *)g_OtBufferTable[slot] + 4, packet);
    }
}

void Battle_DrawStatusSymbol(s8 which) {
    if ((unsigned)which >= 9) return;
    unsigned index = which * 2;
    int slot = g_ActiveDrawSlot;
    RenderPagedSpritePacket *packet = &g_BattleStatusSymbolSprites[slot];
    packet->sprite.u = g_BattleStatusSymbolUv.bytes[index];
    packet->sprite.v = g_BattleStatusSymbolUv.bytes[index + 1];
    packet->sprite.width = g_BattleStatusSymbolSize.bytes[index];
    packet->sprite.height = g_BattleStatusSymbolSize.bytes[index + 1];
    packet->sprite.x = g_BattleStatusBarTiles[slot * 3].x0 + g_BattleStatusSymbolOffset.bytes[index];
    packet->sprite.y = g_BattleStatusBarTiles[slot * 3].y0 + g_BattleStatusSymbolOffset.bytes[index + 1];
    AddPrim(D_800B0E38.ordering[slot] + 4, packet);
}

void Battle_DrawATBGauge(void)
{
    short ammo;
    signed char i;
    short y;
    RenderSpritePacket *sprite;
    BattleGaugePrim *packet;
    RenderSpritePacket *base;
    unsigned category;
    /* Keep the queue index initialized across the ammo-base callback. */
    i = 0;
    y = g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].y0 + 22;
    category = (g_ActiveActor->action->attackWord >> 20) & 3;
    ammo = Inv_GetWeaponCategoryAmmoBase(category - 1);
    base = &g_BattleAmmoLabelSprites[0].sprite;
    packet = (BattleGaugePrim *)((char *)base - PE1_OFFSETOF(BattleGaugePrim, sprite));

    packet += g_ActiveDrawSlot;
    sprite = &g_BattleAmmoLabelSprites[g_ActiveDrawSlot].sprite;
    ammo += *(unsigned short *)&g_ActiveActor->action->attackWord & 0x3ff;
    sprite->x = g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].x0 + 8;
    sprite->y = y;
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 4, (unsigned *)packet);
    while (i < Pad_GetMenuPressedBitOrDisabled()) {
        int kind = g_BattleActionQueue[i].command;
        if (kind == 1) {
            --ammo;
        } else if (kind == 2) {
            unsigned mode = g_ActiveActor->action->turnWord & 0xc0;
            if (mode == 0xc0) {
                ammo -= *(unsigned char *)&g_ActiveActor->action->turnWord & 15;
            } else if (mode == 0x40) {
                --ammo;
            }
        } else if (kind == 0x189) {
            --ammo;
        }
        ++i;
    }
    if (ammo < 0) {
        ammo = 0;
    }
    Battle_DrawDecimalNumber(g_BattleAmmoDigitSprites[g_ActiveDrawSlot], g_BattleStatusBarTiles[g_ActiveDrawSlot * 3].x0 + 64, y - 1, ammo, 0);
}

void Battle_DrawStatusValue(int value, int yOffset)
{
    int slot = g_ActiveDrawSlot;
    RenderPagedSpritePacket *marker = &g_BattleStatusValueMarkers[slot];
    int y = g_BattleStatusBarTiles[slot * 3].y0 + yOffset;
    marker->sprite.x = g_BattleStatusBarTiles[slot * 3].x0 + 8;
    marker->sprite.y = y;
    AddPrim(D_800B0E38.ordering[slot] + 4, marker);
    Battle_DrawDecimalNumber(g_BattleStatusValueDigits[slot],
        g_BattleStatusBarTiles[slot * 3].x0 + 0x40, (s16)(y - 1), (s16)value, 1);
}

int Battle_DrawDecimalNumber(void *buffer, short x, short y, short value, short mode)
{
    unsigned char digits[6];
    signed char index = 0;
    signed char last;
    short remaining;
    short quotient;
    BattleGaugePrim *packet;
    /* Collect least-significant digits, then draw from the highest place. */
    remaining = value;
    do {
        quotient = remaining / 10;
        digits[index] = remaining - quotient * 10;
        remaining = quotient;
        if (!remaining) {
            break;
        }
        ++index;
    } while (1);
    last = index;
    x -= last * 6;
    for (; index >= 0; --index) {
        unsigned digit;
        /* Retail computes the packet address as a 32-bit integer sum. */
        {
            BattleStatusPrimAddress at;

            at.words = buffer;
            at.address = index * sizeof(*packet) + at.address;
            packet = at.gauge;
        }
        digit = digits[index];
        packet->sprite.v = 0xf2;
        packet->sprite.x = x;
        packet->sprite.y = y;
        packet->sprite.u = digit * 8;
        if (mode == 1) {
            packet->sprite.color.bytes.r = g_BattleStatusPointerSprites[g_ActiveDrawSlot].sprite.color.bytes.r + 40;
            packet->sprite.color.bytes.g = g_BattleStatusPointerSprites[g_ActiveDrawSlot].sprite.color.bytes.g;
            packet->sprite.color.bytes.b = g_BattleStatusPointerSprites[g_ActiveDrawSlot].sprite.color.bytes.b;
        }
        x += 6;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 4, (unsigned *)packet);
    }
    return last;
}

/* Digit sprite i of the current draw buffer and panel slot. Every use
 * indexes again: the byte stores make GCC reload both indices. */
#define PANEL_DIGIT(i) (&g_BattlePanelDigits[g_ActiveDrawSlot][g_BattleStatusPanelSlot][i].sprite)

/* Draw a floating damage/recovery number, or the enemy MISS label.
 * Each draw buffer has ten panels of five native page/sprite packets.
 */
void Battle_DrawStatusPanel(s32 mode, BattleStatusPanel *input) {
    if (!input || (unsigned)g_ActiveDrawSlot >= 2 || (unsigned)g_BattleStatusPanelSlot >= 10) return;
    BattleStatusPanel *panel = input;
    u8 digits[5];
    s32 screenY;
    s16 panelValue;
    s32 number;
    s32 screenX;
    RenderSpritePacket *missSprite;
    s16 value;
    s16 quotient;
    s8 n;
    s32 rise;

    panelValue = panel->value;
    number = panelValue;
    if ((panelValue < 0) && (number = 0, ((mode & 0xFF) == 1))) {
        RenderPagedSpritePacket *packet = &g_BattlePanelDigits[g_ActiveDrawSlot][g_BattleStatusPanelSlot][0];
        missSprite = &packet->sprite;
        missSprite->color.bytes.r = panel->timer * 4;
        missSprite->color.bytes.g = 0;
        missSprite->color.bytes.b = 0;
        missSprite->u = 0x50;
        missSprite->v = 0xE0;
        missSprite->width = 24;
        missSprite->height = 8;
        missSprite->x = panel->x - 16;
        missSprite->y = panel->y + panel->timer - 30;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 7, packet);
    } else {
        value = number;
        n = 0;
        for (;;) {
            quotient = value / 10;
            digits[n] = value - quotient * 10;
            value = quotient;
            if (value == 0)
                break;
            n++;
        }
        screenX = panel->x - (n + 1) * 4;
        rise = panel->timer - 30;
        screenY = panel->y + rise;
        for (; n >= 0; n--) {
            switch (panel->style) {
            case 0:
                PANEL_DIGIT(n)->color.bytes.r = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.g = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.b = panel->timer * 4;
                break;
            case 1:
                PANEL_DIGIT(n)->color.bytes.r = 0;
                PANEL_DIGIT(n)->color.bytes.g = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.b = 0;
                break;
            case 2:
                PANEL_DIGIT(n)->color.bytes.r = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.g = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.b = 0;
                break;
            case 3:
                PANEL_DIGIT(n)->color.bytes.r = panel->timer * 4;
                PANEL_DIGIT(n)->color.bytes.g = 0;
                PANEL_DIGIT(n)->color.bytes.b = panel->timer * 4;
                break;
            }
            screenX += 8;
            PANEL_DIGIT(n)->u = digits[n] * 8;
            PANEL_DIGIT(n)->v = 0xE0;
            PANEL_DIGIT(n)->x = screenX;
            PANEL_DIGIT(n)->y = screenY;
            PANEL_DIGIT(n)->width = 8;
            PANEL_DIGIT(n)->height = 8;
            AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 7, (u32 *)&g_BattlePanelDigits[g_ActiveDrawSlot][g_BattleStatusPanelSlot][n]);
        }
    }
    g_BattleStatusPanelSlot += 1;
}

void Draw_SetupSprites(BattleEntity *entity, int unused, int icon) {
    int i;

    g_BattleSpritePrimCountdown = 0x1E;
    i = 0;
    icon = (u8)icon;
    do {
        RenderSpritePacket *marker;

        marker = &g_StatusMarkerPackets[(u8)i].sprite;
        marker->u = g_StatusIconPackets[(u8)i][icon].sprite.u;
        marker->v = g_StatusIconPackets[(u8)i][icon].sprite.v;
        marker->x = entity->renderObject.projected_x - 8;
        marker->y = entity->renderObject.projected_y - 0x10;
        g_StatusMarkerPackets[(u8)i].sprite.clut =
            g_StatusIconPackets[(u8)i][icon].sprite.clut;
        i++;
    } while ((u8)i < 2);
}

void Draw_QueuePrimitive(void) {
    int slot;

    slot = g_ActiveDrawSlot;
    g_StatusMarkerPackets[slot].sprite.y -= 2;
    AddPrim((RenderNativeTag *)g_OtBufferTable[slot] + 7, &g_StatusMarkerPackets[slot]);
    g_BattleSpritePrimCountdown--;
}

/* Queue an icon from the native two-buffer packet pool. */
#define EMIT_CODED(code_expr) do { \
    RenderPagedSpritePacket *icon = &g_StatusIconPackets[g_ActiveDrawSlot][(code_expr)]; \
    icon->sprite.x = s0; icon->sprite.y = s2; \
    s0 += s3; \
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 7, icon); \
} while (0)

void Draw_StatusIcons(void) {
    Combatant *s1;
    int s0;
    int s2;
    int s3;
    s32 *s4;
    u8 mode;
    int bits;
    int code;

    s1 = g_ActiveActor;
    mode = g_MenuActiveMode;
    s0 = 0xF;
    s4 = &s1->stateFlags;
    if (mode - 1U < 2U) {
        s0 = 0x121;
    }
    if (mode < 2) {
        s2 = 0xC1;
    } else {
        s2 = 0xF;
    }
    if ((short)s0 == 0x121) {
        s3 = -0x10;
    } else {
        s3 = 0x10;
    }

    {
        int bits0;
        bits0 = s1->stateFlags & 0xC;
        if (bits0 != 0) {
            code = bits0 ^ 0xC;
            code = code < 1;
            code <<= 2;
            EMIT_CODED(code);
        }
    }

    {
        int tmp;
        tmp = s1->stateFlags;
        bits = tmp & 0x30;
    }
    if (bits != 0) {
        code = 1;
        if (bits == 0x30) {
            code = 5;
        }
        EMIT_CODED(code);
    }

    {
        int tmp;
        tmp = *s4;
        bits = tmp & 0xC0;
    }
    if (bits != 0) {
        code = 2;
        if (bits == 0xC0) {
            code = 6;
        }
        EMIT_CODED(code);
    }

    {
        int tmp;
        tmp = *s4;
        bits = tmp & 3;
    }
    if (bits != 0) {
        code = 3;
        if (bits == 3) {
            code = 7;
        }
        EMIT_CODED(code);
    }

    if (*s4 & 0x1000) {
        EMIT_CODED(8);
    }
    if (*s4 & 0x100) {
        EMIT_CODED(9);
    }
    if (*s4 & 0x400) {
        EMIT_CODED(10);
    }
    if (*s4 & 0x800) {
        EMIT_CODED(11);
    }
    if (*s4 & 0x200) {
        EMIT_CODED(12);
    }
}

/* Active menu window mode and its on-screen bounds.
 * g_MenuActiveMode is private state of this pair. */

u8 g_MenuActiveMode;

void Battle_SetWindowBoundsByMode(int mode) {
    SaveWindowModeBounds bounds;
    u16 *entry;

    if ((u8)mode >= 4) return;
    bounds = g_MenuWindowModeBounds;
    entry = &bounds.values[(u8)mode * 2];
    /* A record store: GCC 2.7 would let a scalar store pass the next load. */
    g_HudOrigin.x = entry[0];
    g_MenuActiveMode = mode;
    g_MenuActiveWindowYOffset = entry[1];
}

unsigned int Menu_GetActiveMode(void) {
    return g_MenuActiveMode;
}

void Script_SignalExit(void) {
    g_BattleEnemyEscapeFlag = 1;
}

/* Clamp the active combatant's gauges, draw the ATB and PE bars and the
 * status background, then the HP numbers and status icons. Each background
 * branch writes the whole tile; the compiler merges their common tails. */
void Battle_DrawActiveStatus(void)
{
    s16 maximumHP;
    s32 maximumGauge;
    s32 currentGauge;
    s32 atbWidth;
    s32 peWidth;
    s32 peProduct;
    RenderColorTilePacket *atbTrack;
    RenderGouraudQuad *atbFill;
    RenderSpritePacket *atbMarker;
    RenderSpritePacket *atbLabel;
    RenderGouraudQuad *peFill;
    RenderGouraudQuad *peRemainder;
    RenderSpritePacket *peLabel;
    BattleModeTilePacket *tiles;

    if (!(g_GameStateFlags & 2)) {
        g_ActiveActor = D_8009D254->core;
    }
    if (g_ActiveActor->hpAlive >= 9001) {
        g_ActiveActor->hpAlive = 9000;
    }
    currentGauge = g_ActiveActor->exp_or_acc;
    maximumGauge = g_ActiveActor->maxAtk;
    if (maximumGauge < currentGauge) {
        g_ActiveActor->exp_or_acc = maximumGauge;
    } else if (currentGauge < 0) {
        g_ActiveActor->exp_or_acc = 0;
    }
    maximumHP = g_ActiveActor->maxHP;
    if (maximumHP < (s16)g_ActiveActor->curHP) {
        g_ActiveActor->curHP = maximumHP;
    }
    peProduct = (s16)(g_ActiveActor->exp_or_acc >> 16) * 0x38;
    atbWidth = (g_ActiveActor->hpAlive * 0x38) / 9000;
    peWidth = peProduct / (s16)(g_ActiveActor->maxAtk >> 16);
    if (g_GameStateFlags & 2) {
        atbTrack = &g_BattleAtbTracks[g_ActiveDrawSlot];
        atbTrack->x0 = g_HudOrigin.x + 8;
        atbTrack->y0 = g_HudOrigin.y + 4;
        atbFill = &g_BattleAtbGauges[g_ActiveDrawSlot];
        atbFill->x0 = g_HudOrigin.x + 8;
        atbFill->y0 = g_HudOrigin.y + 4;
        atbFill->x1 = g_HudOrigin.x + 8 + atbWidth;
        atbFill->y1 = g_HudOrigin.y + 4;
        atbFill->x2 = g_HudOrigin.x + 8;
        atbFill->y2 = g_HudOrigin.y + 7;
        atbFill->x3 = g_HudOrigin.x + 8 + atbWidth;
        atbFill->y3 = g_HudOrigin.y + 7;
        atbMarker = &g_BattleAtbMarkers[g_ActiveDrawSlot].sprite;
        atbMarker->x = g_BattleAtbGauges[g_ActiveDrawSlot].x1;
        atbMarker->y = g_BattleAtbGauges[g_ActiveDrawSlot].y1 - 2;
        atbLabel = &g_BattleAtbLabels[g_ActiveDrawSlot].sprite;
        atbLabel->x = g_HudOrigin.x + 0x44;
        atbLabel->y = g_HudOrigin.y + 3;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 6, (u32 *)atbTrack);
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5, (u32 *)&g_BattleAtbGauges[g_ActiveDrawSlot]);
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 4, (u32 *)&g_BattleAtbMarkers[g_ActiveDrawSlot]);
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 4, (u32 *)&g_BattleAtbLabels[g_ActiveDrawSlot]);
    }
    if (Menu_GetItemContextFlag() & 2) {
        peFill = &g_BattlePeGauges[g_ActiveDrawSlot][0];
        peFill->x0 = g_HudOrigin.x + 8;
        peFill->y0 = g_HudOrigin.y + 0x19;
        peFill->x1 = g_HudOrigin.x + 8 + peWidth;
        peFill->y1 = g_HudOrigin.y + 0x19;
        peFill->x2 = g_HudOrigin.x + 8;
        peFill->y2 = g_HudOrigin.y + 0x1C;
        peFill->x3 = g_HudOrigin.x + 8 + peWidth;
        peFill->y3 = g_HudOrigin.y + 0x1C;
        peRemainder = &g_BattlePeGauges[g_ActiveDrawSlot][1];
        peRemainder->x0 = g_BattlePeGauges[g_ActiveDrawSlot][0].x1;
        peRemainder->y0 = g_HudOrigin.y + 0x19;
        peRemainder->x1 = (u16)g_BattlePeGauges[g_ActiveDrawSlot][0].x1 + 0x38 - peWidth;
        peRemainder->y1 = g_HudOrigin.y + 0x19;
        peRemainder->x2 = g_BattlePeGauges[g_ActiveDrawSlot][0].x1;
        peRemainder->y2 = g_HudOrigin.y + 0x1C;
        peLabel = &g_BattlePeLabels[g_ActiveDrawSlot].sprite;
        peRemainder->x3 = (u16)g_BattlePeGauges[g_ActiveDrawSlot][0].x1 + 0x38 - peWidth;
        peRemainder->y3 = g_HudOrigin.y + 0x1C;
        peLabel->x = g_HudOrigin.x + 0x44;
        peLabel->y = g_HudOrigin.y + 0x18;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5, (u32 *)peFill);
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5, (u32 *)&g_BattlePeGauges[g_ActiveDrawSlot][1]);
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 4, (u32 *)&g_BattlePeLabels[g_ActiveDrawSlot]);
        if (g_GameStateFlags & 2) {
            tiles = g_BattleStatusBackgrounds;
            tiles[g_ActiveDrawSlot].tile.x0 = g_HudOrigin.x;
            tiles[g_ActiveDrawSlot].tile.y0 = g_HudOrigin.y;
            tiles[g_ActiveDrawSlot].tile.w = 0x54;
            tiles[g_ActiveDrawSlot].tile.h = 0x20;
        } else {
            tiles = g_BattleStatusBackgrounds;
            tiles[g_ActiveDrawSlot].tile.x0 = g_HudOrigin.x;
            tiles[g_ActiveDrawSlot].tile.y0 = g_HudOrigin.y + 7;
            tiles[g_ActiveDrawSlot].tile.w = 0x54;
            tiles[g_ActiveDrawSlot].tile.h = 0x19;
        }
    } else if (g_GameStateFlags & 2) {
        tiles = g_BattleStatusBackgrounds;
        tiles[g_ActiveDrawSlot].tile.x0 = g_HudOrigin.x;
        tiles[g_ActiveDrawSlot].tile.y0 = g_HudOrigin.y;
        tiles[g_ActiveDrawSlot].tile.w = 0x54;
        tiles[g_ActiveDrawSlot].tile.h = 0x19;
    } else {
        tiles = g_BattleStatusBackgrounds;
        tiles[g_ActiveDrawSlot].tile.x0 = g_HudOrigin.x;
        tiles[g_ActiveDrawSlot].tile.y0 = g_HudOrigin.y + 7;
        tiles[g_ActiveDrawSlot].tile.w = 0x54;
        tiles[g_ActiveDrawSlot].tile.h = 0x12;
    }
    AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 7, (u32 *)&g_BattleStatusBackgrounds[g_ActiveDrawSlot]);
    Battle_DrawEnemyHP(g_ActiveActor->maxHP, g_ActiveActor->curHP);
    Draw_StatusIcons();
}

/* Digit sprites of the current draw buffer. Every use indexes again: the
 * byte stores to the packets make GCC reload the draw slot. */
#define MAX_HP_DIGIT(i) (&g_BattleMaxHpDigits[g_ActiveDrawSlot][i].sprite)
#define CURRENT_HP_DIGIT(i) (&g_BattleHpDigits[g_ActiveDrawSlot][i].sprite)

/* Draw current/maximum HP from reversed decimal digits and the HP label.
 * The maximum is drawn right to left after a slash, the current value is
 * right-aligned against it; flashing digits mark the status flags. */
void Battle_DrawEnemyHP(s16 maximum, s16 current)
{
    u8 digits[8];
    s16 y;
    s16 x;
    s8 n;
    s8 last;
    s8 missing;
    s16 quotient;
    u8 pulse;
    RenderSpritePacket *label;

    y = g_HudOrigin.y + 11;
    n = 0;
    for (;;) {
        quotient = maximum / 10;
        digits[n] = maximum - quotient * 10;
        maximum = quotient;
        if (maximum == 0)
            break;
        n++;
    }
    n++;
    digits[n] = 10;
    last = n;
    x = g_HudOrigin.x - n * 6 + 56;
    for (; n >= 0; n--) {
        MAX_HP_DIGIT(n)->u = digits[n] * 8;
        MAX_HP_DIGIT(n)->v = 0xE8;
        MAX_HP_DIGIT(n)->x = x;
        MAX_HP_DIGIT(n)->y = y;
        if (!(((Combatant *)D_8009D254->core)->stateFlags & 0x800) || n == last) {
            MAX_HP_DIGIT(n)->color.bytes.r = 128;
            MAX_HP_DIGIT(n)->color.bytes.g = 128;
            MAX_HP_DIGIT(n)->color.bytes.b = 128;
        } else {
            pulse = g_BattleFrameCounter & 31;
            if (pulse >= 17)
                pulse = 32 - pulse;
            MAX_HP_DIGIT(n)->color.bytes.r = pulse * 6 + 32;
            MAX_HP_DIGIT(n)->color.bytes.g = pulse + 96;
            MAX_HP_DIGIT(n)->color.bytes.b = 128;
        }
        x += 6;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5, (u32 *)&g_BattleMaxHpDigits[g_ActiveDrawSlot][n]);
    }

    x = g_HudOrigin.x - (last * 2 + 1) * 6 + 62;
    n = 0;
    for (;;) {
        quotient = current / 10;
        digits[n] = current - quotient * 10;
        current = quotient;
        if (current == 0)
            break;
        n++;
    }
    missing = last - n - 1;
    if (missing > 0)
        x += missing * 6;
    for (; n >= 0; n--) {
        CURRENT_HP_DIGIT(n)->u = digits[n] * 8;
        CURRENT_HP_DIGIT(n)->v = 0xE8;
        CURRENT_HP_DIGIT(n)->x = x;
        CURRENT_HP_DIGIT(n)->y = y;
        if (((Combatant *)D_8009D254->core)->stateFlags & 0x400) {
            pulse = g_BattleFrameCounter & 31;
            if (pulse >= 17)
                pulse = 32 - pulse;
            CURRENT_HP_DIGIT(n)->color.bytes.r = pulse * 8;
            CURRENT_HP_DIGIT(n)->color.bytes.g = 128;
            CURRENT_HP_DIGIT(n)->color.bytes.b = pulse * 6 + 32;
        } else {
            CURRENT_HP_DIGIT(n)->color.bytes.r = 128;
            CURRENT_HP_DIGIT(n)->color.bytes.g = 128;
            CURRENT_HP_DIGIT(n)->color.bytes.b = 128;
        }
        x += 6;
        AddPrim(D_800B0E38.ordering[g_ActiveDrawSlot] + 5, (u32 *)&g_BattleHpDigits[g_ActiveDrawSlot][n]);
    }

    /* Place the HP label after the number pair. */
    {
        s32 frame = g_ActiveDrawSlot;
        label = &g_BattleHpLabels[frame].sprite;
        label->x = g_HudOrigin.x + 0x44;
        label->y = g_HudOrigin.y + 0xE;
        AddPrim(D_800B0E38.ordering[frame] + 4, (u32 *)&g_BattleHpLabels[frame]);
    }
}

void Render_AnimationFrame(void) {
    u32 frame = (u32)g_BattleAttackAnimFrame;
    u32 size, width;
    int draw_index = g_ActiveDrawSlot;
    RenderAnimPositionInput position = {0};
    if (!g_ActiveActor || !g_PlayerEntity || (unsigned)draw_index >= 2 ||
        g_DrawPacketOffset < 0 || g_DrawPacketOffset > RENDER_ANIM_BUFFER_BYTES) return;
    if (frame < 10) {
        if ((g_ActiveActor->stateFlags & 0x30) == 0x10) {
            size = (frame * 5) << 18;
        } else {
            if (!g_ActiveActor->action) return;
            size = (((u32)(u16)g_ActiveActor->action->range << 16) * frame) / 10;
        }
        width = frame << 6;
    } else {
        size = 0xC80000;
        if ((g_ActiveActor->stateFlags & 0x30) != 0x10) {
            if (!g_ActiveActor->action) return;
            size = (u32)(u16)g_ActiveActor->action->range << 16;
        }
        width = (frame << 4) + 10;
    }
    position.x = Pe1Fixed_Integer(&g_PlayerEntity->pos_x);
    position.y = Pe1Fixed_Integer(&g_PlayerEntity->pos_y);
    position.z = Pe1Fixed_Integer(&g_PlayerEntity->pos_z);
    if (frame == 0) Asset_Find08Alt(0x457, 0, position.x, position.y, position.z);
    Render_SetGteScreenOffset();
    if (g_OtBufferTable[draw_index] && g_RenderAnimBufferBase[draw_index]) {
        g_DrawPacketOffset += Render_DrawWireframeModel(g_GameState.room_geometry_table,
            &position, (s32)size, width & 0xFFFE,
            (RenderNativeTag *)g_OtBufferTable[draw_index] + 2,
            g_RenderAnimBufferBase[draw_index] + g_DrawPacketOffset,
            RENDER_ANIM_BUFFER_BYTES - g_DrawPacketOffset);
    }
    Render_ResetGteScreenOffset();
    g_BattleAttackAnimFrame = (s32)(frame + 1);
}

/* Status symbol templates (three 18-byte glyph strips the status primitive
 * flow copies) and the per-mode window bounds Battle_SetWindowBoundsByMode reads:
 * the data that sat between this module's rodata as main/rodata_0015FC.
 * They are defined after every user, so the users still copy them as the
 * byte-aligned objects their declarations describe; GCC emits them after the
 * status primitive constants and before the jump tables, which it writes at
 * the end of the file, as retail has them. */
const StatusSymbolTemplate g_BattleStatusSymbolUv = {{
    0x00, 0x00, 0x00, 0xF8, 0xD4, 0xF0, 0x00, 0x00, 0x90,
    0xE8, 0x28, 0xF8, 0xB4, 0xEF, 0x50, 0xF8, 0x58, 0xE8,
}};
const StatusSymbolTemplate g_BattleStatusSymbolSize = {{
    0x00, 0x00, 0x28, 0x07, 0x14, 0x07, 0x00, 0x00, 0x40,
    0x07, 0x28, 0x07, 0x1C, 0x07, 0x30, 0x07, 0x38, 0x07,
}};
const StatusSymbolTemplate g_BattleStatusSymbolOffset = {{
    0x00, 0x00, 0x14, 0x08, 0x1E, 0x08, 0x00, 0x00, 0x08,
    0x08, 0x14, 0x08, 0x1A, 0x08, 0x10, 0x08, 0x0C, 0x08,
}};
/* X and Y per mode: (221, 177), (15, 177), (15, 15), (221, 15). */
const SaveWindowModeBounds g_MenuWindowModeBounds = {{
    221, 177, 15, 177, 15, 15, 221, 15,
}};

/* Canonical packet pools for the original two HUD draw buffers. */
BattleStatusLinePrim g_BattleStatusBarTiles[6];
RenderPagedSpritePacket g_BattleStatusValueDigits[2][2];
BattleGaugePrim g_BattleTargetHighlightSprites[2][3];
BattleGaugePrim g_BattleStatusPointerSprites[2];
BattleGaugePrim g_BattleStatusValueLabelSprites[2];
BattleGaugePrim g_BattleAmmoLabelSprites[2];
RenderPagedSpritePacket g_BattleAmmoDigitSprites[2][4];
BattleModeTilePacket g_BattleStatusBackgrounds[2];
RenderColorTilePacket g_BattleAtbTracks[2];
RenderPagedSpritePacket g_BattleAtbLabels[2];
RenderPagedSpritePacket g_BattlePeLabels[2];
RenderGouraudQuad g_BattleAtbGauges[2];
RenderGouraudQuad g_BattlePeGauges[2][2];
RenderPagedSpritePacket g_BattleAtbMarkers[2];
RenderPagedSpritePacket g_BattleHpDigits[2][4], g_BattleMaxHpDigits[2][5];
RenderPagedSpritePacket g_BattleHpLabels[2];
RenderPagedSpritePacket g_BattleRemainingActionSprites[2][10];
RenderPagedSpritePacket g_BattleStatusValueMarkers[2];
RenderPagedSpritePacket g_BattleStatusSymbolSprites[2];
BattleHudShades g_BattleStatusShades[1] = {{-32,96,16}};
RenderPagedSpritePacket g_BattlePanelDigits[2][10][5];
RenderTexturedQuad g_BattleEndPanels[2];
RenderLinePacket g_BattleStatusPointerLines[2][2];
BattleStatusPointerPrim g_BattleStatusPointerTriangles[2];
RenderPagedSpritePacket g_StatusIconPackets[2][13];
RenderPagedSpritePacket g_StatusMarkerPackets[2];
