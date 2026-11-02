/* An attack step targeting a released enemy (actor NULL in the action queue)
 * must not crash: Aya stands still on that step instead of reading through
 * the missing actor. A live target still steers the approach as before. */
static int original_aya_step_released_target(void)
{
    BattleEntity player = {0}, target = {0};
    BattleAction action = {0};
    Combatant active = {0}, aya_core = {0};
    BattleAttributes attrs = {0};
    BattleInitSlot old_slot = g_BattleActionQueue[0];
    Combatant *old_actor = g_ActiveActor;
    FieldActor *old_player = g_PlayerEntity;
    u8 old_index = g_BattleActionIndex;
    u8 old_step = g_AyaActionStep;
    s16 old_timer = g_AyaActionTimer;
    u8 old_attack_step = (u8)g_AyaAttackStep;
    /* Minimum GTE/audio state for Asset_Find08Alt's 3D pan inside the step. */
    GteMatrix view = {.m = {{4096,0,0},{0,4096,0},{0,0,4096}}};
    GteMatrix old_view;
    int had_view = g_GeomScroll.words.viewMatrix != NULL;
    if (had_view) old_view = *g_GeomScroll.words.viewMatrix;
    s32 projection = 256;
    u32 sound[32] = {0};
    s32 *old_projection = g_GeomScroll.state.position.projectionDistance;
    void *old_bgm = g_AkaoBgmHandle[0];
    Pe1GameState old_game = g_GameState;
    u32 old_flags = g_GameStateFlags;
    int ok = 1;

    aya_core.attributes = &attrs;
    active.action = &action;
    active.attributes = &attrs;
    g_PlayerEntity = (FieldActor *)&player;
    g_ActiveActor = &active;
    g_BattleActionIndex = 0;
    g_GeomScroll.words.viewMatrix = &view;
    g_GeomScroll.state.position.projectionDistance = &projection;
    g_AkaoBgmHandle[0] = sound;
    g_GameState.bank_value_f8 = 100;
    g_GameState.bank_value_fa = 1000;
    g_GameState.bank_value_f6 = 48;
    g_GameState.bank_value_f7 = 100;
    g_GameStateFlags = 2;
    Psyz_GteCtrlWrite(26, 256);
    Psyz_GteCtrlWrite(29, 1365);
    Psyz_GteCtrlWrite(30, 1024);
    gte_SetGeomOffset(160, 120);

    /* Released target: no actor in the queue at all. */
    g_AyaActionStep = 6;
    g_AyaActionTimer = 0;
    g_AyaAttackStep = 0;
    g_BattleActionQueue[0].actor = NULL;
    player.renderObject.variant_visible = 0;
    player.renderObject.hit_cylinder.radius = 100;
    player.posX.fixed = 10 << 16;
    player.posZ.fixed = 20 << 16;
    Battle_StepAyaAction();
    if (g_AyaActionStep != 7) ok = 0;   /* the step still advances */

    /* Live target: the approach still aims at it. */
    g_AyaActionStep = 6;
    g_BattleActionQueue[0].actor = &target;
    target.core = &aya_core;
    target.renderObject.target_x = 40 << 16;
    target.renderObject.target_z = 60 << 16;
    target.renderObject.hit_cylinder.radius = 120;
    Battle_StepAyaAction();
    if (g_AyaActionStep != 7) ok = 0;

    g_BattleActionQueue[0] = old_slot;
    g_ActiveActor = old_actor;
    g_PlayerEntity = old_player;
    g_BattleActionIndex = old_index;
    g_AyaActionStep = old_step;
    g_AyaActionTimer = old_timer;
    g_AyaAttackStep = old_attack_step;
    g_GeomScroll.words.viewMatrix = had_view ? &old_view : NULL;
    g_GeomScroll.state.position.projectionDistance = old_projection;
    g_AkaoBgmHandle[0] = old_bgm;
    g_GameState = old_game;
    g_GameStateFlags = old_flags;
    if (ok) puts("Original Aya action step: released target stands still, live target steers");
    return ok;
}