#include "config.h"

#include "battle.h"
#include "game_rules.h"
#include "pokemon.h"
#include "trainer_data.h"

/*
 * AUTO BATTLE debug rule (RULE_AUTO_BATTLE, flag 2311): in a plain trainer battle the player's own battlers choose their
 * command with the retail trainer AI instead of opening the command menu. For passive testing only; the forced replacement
 * after a faint, the Shift prompt, learn-move and evolution prompts stay manual.
 *
 * Injection point: BattleContext_Main calls this BEFORE dispatching CONTROLLER_COMMAND_SELECTION_SCREEN_INPUT (retail
 * ov12_02248848, a per-battler state machine over ctx->com_seq_no[]). Retail state 0 sends the "choose a command" request
 * to the client (ov12_02262B80, which opens the menu) the first time it runs for a battler, so the choice has to be in place
 * before that first dispatch. We write exactly what retail leaves behind after a confirmed selection and jump the battler to
 * SSI_STATE_13 (the same state retail state 0 jumps to for a battler that cannot select, e.g. a recharging or locked mon,
 * so SSI_STATE_13 is known to be safe without a menu having been opened).
 *
 * Retail state writes mirrored here (all verified in the vanilla ov12 disassembly):
 *   fight : playerActions[b] = { FIGHT_INPUT, target, slot + 1, SELECT_FIGHT_COMMAND }, waza_no_pos[b] = slot,
 *           waza_no_select[b] = move id, rec_select_flag[b] |= 3
 *   Struggle (every move blocked): oneTurnFlag[b].struggle_flag = 1, playerActions[b] = { FIGHT_INPUT, -, -, SELECT_FIGHT_COMMAND }
 *   switch: playerActions[b] = { POKEMON_INPUT, -, slot, SELECT_POKEMON_COMMAND }, reshuffle_sel_mons_no[b] = slot
 */

/* AI think bits for the player's battlers: the set hg-engine gives its strongest trainers (Lance, Red and about 190 other
 * entries in data/Trainers.c): F_PRIORITIZE_SUPER_EFFECTIVE | F_EVALUATE_ATTACKS | F_EXPERT_ATTACKS. The AI setup adds the
 * doubles bit by itself. */
#define AUTO_BATTLE_AI_FLAGS F_TRAINER_EXPERT_AI

/* ctx->aiWorkTable.ai_status_flag bit that makes ov10 skip its setup (retail "resume" marker). */
#define AI_STATUS_FLAG_SKIP_SETUP 0x10

/* Byte of the client's CLIENT_PARAM that retail state 0 tests (ov12_02261264, == 1) to decide that a client is AI-driven
 * (foe, Tag partner): those get their request at once. A local human has 0 here and is sent the menu request later (state 2). */
#define CLIENT_PARAM_AI_BYTE 0x196

#define AI_RESULT_NONE   0xFF
#define AI_COMMAND_FIGHT  1
#define AI_COMMAND_SWITCH 3

/* retail uses a ctx->ai_reshuffle_sel_mons_no value of 6 for "no switch target picked" */
#define AI_NO_RESHUFFLE 6

static BOOL AutoBattle_IsSelectableMove(struct BattleSystem *bsys, struct BattleStruct *ctx, int battlerId, int slot, u32 blockedMoves)
{
    BattleMessage msg;

    if (slot < 0 || slot > 3) {
        return FALSE;
    }
    if (blockedMoves & No2Bit(slot)) {
        return FALSE;
    }
    if (ctx->battlemon[battlerId].move[slot] == 0) {
        return FALSE;
    }
    return ov12_02251A28(bsys, ctx, battlerId, slot, &msg);
}

static BOOL AutoBattle_IsFreeSwitchSlot(struct BattleSystem *bsys, struct BattleStruct *ctx, int battlerId, int slot, int maxBattlers, u32 chosenSlots)
{
    struct PartyPokemon *mon;
    int other;

    if (slot < 0 || slot >= 6 || slot >= BattleWorkPokeCountGet(bsys, battlerId)) {
        return FALSE;
    }
    if (chosenSlots & No2Bit(slot)) {
        return FALSE;
    }
    for (other = battlerId & 1; other < maxBattlers; other += 2) {
        if (ctx->sel_mons_no[other] == slot) {
            return FALSE; /* already on the field */
        }
    }
    mon = BattleWorkPokemonParamGet(bsys, battlerId, slot);
    if (mon == NULL) {
        return FALSE;
    }
    if (GetMonData(mon, MON_DATA_SPECIES, NULL) == 0 || GetMonData(mon, MON_DATA_IS_EGG, NULL) || GetMonData(mon, MON_DATA_HP, NULL) == 0) {
        return FALSE;
    }
    return TRUE;
}

/* Opposing battler to aim at when the AI gave no target (fallback fight in a double battle). */
static int AutoBattle_DefaultOpponent(struct BattleStruct *ctx, int battlerId)
{
    int first = BATTLER_OPPONENT(battlerId);

    if (ctx->battlemon[first].hp > 0) {
        return first;
    }
    return BATTLER_ALLY(first);
}

static void AutoBattle_SetFight(struct BattleSystem *bsys, struct BattleStruct *ctx, int battlerId, int slot, int aiTarget)
{
    u16 move = ctx->battlemon[battlerId].move[slot];
    u16 range = ctx->moveTbl[move].target;
    u32 target = ctx->playerActions[battlerId][1];
    u8 rec = 3;

    if (!(BattleTypeGet(bsys) & BATTLE_TYPE_DOUBLES)) {
        /* retail ov12_0224DB64, single battle: a user/field/self-side range aims at the user, everything else at the foe */
        target = (range & 0x251) ? (u32)battlerId : (u32)BATTLER_OPPONENT(battlerId);
    } else if (!(range == RANGE_ALLY && (ctx->no_reshuffle_client & No2Bit(BATTLER_ALLY(battlerId))))) {
        /* retail asks the client for a target in doubles; an AI client answers with ai_dir_select_client */
        target = (aiTarget >= 0 && aiTarget < CLIENT_MAX) ? (u32)aiTarget : (u32)AutoBattle_DefaultOpponent(ctx, battlerId);
        rec |= 4;
    }

    ctx->playerActions[battlerId][0] = CONTROLLER_COMMAND_FIGHT_INPUT;
    ctx->playerActions[battlerId][1] = target;
    ctx->playerActions[battlerId][2] = slot + 1;
    ctx->playerActions[battlerId][3] = SELECT_FIGHT_COMMAND;
    ctx->waza_no_pos[battlerId] = slot;
    ctx->waza_no_select[battlerId] = move;
    ctx->rec_select_flag[battlerId] |= rec;
    ctx->com_seq_no[battlerId] = SSI_STATE_13;
}

static void AutoBattle_SetStruggle(struct BattleSystem *bsys, struct BattleStruct *ctx, int battlerId)
{
    ctx->oneTurnFlag[battlerId].struggle_flag = 1;
    ctx->playerActions[battlerId][0] = CONTROLLER_COMMAND_FIGHT_INPUT;
    ctx->playerActions[battlerId][3] = SELECT_FIGHT_COMMAND;
    ctx->rec_select_flag[battlerId] |= 1;
    if (BattleWorkBattleStatusFlagGet(bsys) & 0x10) {
        ctx->com_seq_no[battlerId] = SSI_STATE_13;
    } else {
        /* retail goes through SSI_STATE_END (which closes the menu) to the "has no moves left!" message state; the menu was
         * never opened here, so go straight to that state (it sets its own return state, SSI_STATE_13) */
        ctx->com_seq_no[battlerId] = SSI_STATE_NO_MOVES;
    }
}

static void AutoBattle_SetSwitch(struct BattleStruct *ctx, int battlerId, int slot)
{
    ctx->playerActions[battlerId][0] = CONTROLLER_COMMAND_POKEMON_INPUT;
    ctx->playerActions[battlerId][2] = slot;
    ctx->playerActions[battlerId][3] = SELECT_POKEMON_COMMAND;
    ctx->reshuffle_sel_mons_no[battlerId] = slot;
    ctx->rec_select_flag[battlerId] |= 1;
    ctx->com_seq_no[battlerId] = SSI_STATE_13;
}

static void AutoBattle_ChooseAction(struct BattleSystem *bsys, struct BattleStruct *ctx, int battlerId, int maxBattlers, u32 *chosenSlots)
{
    u32 savedAiBits = bsys->trainers[battlerId].aibit;
    u32 blockedMoves;
    int aiTarget = -1;
    int slot = -1;
    int i;

    bsys->trainers[battlerId].aibit = AUTO_BATTLE_AI_FLAGS;

    /* 1. command: the trainer AI's own fight / item / switch decision (ov10 0x022205BC, as the AI client's command handler
     * ov12_0225E104 calls it). Item (2) is not offered to the player, so it falls through to a fight. */
    ctx->ai_reshuffle_sel_mons_no[battlerId] = AI_NO_RESHUFFLE;
    if (TrainerAI_PickCommand(bsys, battlerId) == AI_COMMAND_SWITCH) {
        slot = ctx->ai_reshuffle_sel_mons_no[battlerId];
        if (AutoBattle_IsFreeSwitchSlot(bsys, ctx, battlerId, slot, maxBattlers, *chosenSlots)) {
            bsys->trainers[battlerId].aibit = savedAiBits;
            *chosenSlots |= No2Bit(slot);
            AutoBattle_SetSwitch(ctx, battlerId, slot);
            return;
        }
        ctx->ai_reshuffle_sel_mons_no[battlerId] = AI_NO_RESHUFFLE;
        slot = -1;
    }

    /* 2. Struggle exactly as retail ov12_02248B00: every move blocked (no PP, disabled, taunt, choice, encore...) */
    blockedMoves = StruggleCheck(bsys, ctx, battlerId, 0, 0xFFFFFFFF) & 0xF;
    if (blockedMoves == 0xF) {
        bsys->trainers[battlerId].aibit = savedAiBits;
        AutoBattle_SetStruggle(bsys, ctx, battlerId);
        return;
    }

    /* 3. move: ov10 0x0221BEF4 (it runs its own setup, copying the think bits from bsys->trainers[client].aibit and
     * zeroing blocked moves). Results 4 and 5 (escape / safari) and 0xFF (no target in doubles) are not used. */
    ctx->aiWorkTable.ai_status_flag &= ~AI_STATUS_FLAG_SKIP_SETUP;
    slot = TrainerAI_PickMove(bsys, battlerId) & 0xFF;
    aiTarget = ctx->aiWorkTable.ai_dir_select_client[battlerId];
    ctx->aiWorkTable.ai_status_flag = 0;
    bsys->trainers[battlerId].aibit = savedAiBits;

    if (slot == AI_RESULT_NONE || !AutoBattle_IsSelectableMove(bsys, ctx, battlerId, slot, blockedMoves)) {
        slot = -1;
        aiTarget = -1;
        for (i = 0; i < 4; i++) {
            if (AutoBattle_IsSelectableMove(bsys, ctx, battlerId, i, blockedMoves)) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        AutoBattle_SetStruggle(bsys, ctx, battlerId);
        return;
    }
    AutoBattle_SetFight(bsys, ctx, battlerId, slot, aiTarget);
}

void LONG_CALL AutoBattle_SelectPlayerActions(struct BattleSystem *bsys, struct BattleStruct *ctx)
{
    u32 battleType;
    u32 chosenSlots = 0;
    int maxBattlers;
    int battlerId;

    if (ctx->server_seq_no != CONTROLLER_COMMAND_SELECTION_SCREEN_INPUT) {
        return;
    }

    battleType = BattleTypeGet(bsys);
    if (!(battleType & BATTLE_TYPE_TRAINER) || (battleType & (BATTLE_TYPE_LINK | BATTLE_TYPE_AI | BATTLE_TYPE_ROAMER | BATTLE_TYPE_TUTORIAL))) {
        return;
    }
    if (!GameRule_IsEnabled(RULE_AUTO_BATTLE)) {
        return;
    }

    maxBattlers = BattleWorkClientSetMaxGet(bsys);
    for (battlerId = 0; battlerId < maxBattlers && battlerId < CLIENT_MAX; battlerId += 2) {
        /* only the points where retail state 0 would send the command menu to a local human client */
        if (ctx->com_seq_no[battlerId] != SSI_STATE_SELECT_COMMAND_INIT) {
            continue;
        }
        if (ctx->no_reshuffle_client & No2Bit(battlerId)) {
            continue;
        }
        if (!BattleSystem_CanSelectCommand(ctx, battlerId)) {
            continue;
        }
        if (((u8 *)BattleWorkClientParamGet(bsys, battlerId))[CLIENT_PARAM_AI_BYTE] == 1) {
            continue; /* an AI-driven client: a foe, a Tag / Multi partner */
        }
        AutoBattle_ChooseAction(bsys, ctx, battlerId, maxBattlers, &chosenSlots);
    }
}
