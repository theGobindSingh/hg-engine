#include "../include/config.h"
#include "../include/constants/file.h"
#include "../include/item.h"
#include "../include/mr_paint.h"
#include "../include/repel.h"
#include "../include/roamer.h"
#include "../include/script.h"
#include "../include/types.h"

#define SCRIPT_NEW_CMD_REPEL_USE 0
// Mr. Paint (feature 3, slice 0.3.1): checks the pending-inspiration var src/bag.c's
// Bag_AddItem set, and if set, buffers the HM/TM's item id into itemVar (for the calling
// script's own buffer_item_name) and writes the 1-based move index (into
// gMrPaintMoveEntries / data/text/040.txt 121-130) into resultVar, 0 meaning nothing pending.
// Two result operands, unlike NEW_COMMAND_QUEUE_NEW_REPEL's one - the caller needs both which
// message to show (resultVar) and what item name to buffer into it (itemVar).
#define SCRIPT_NEW_CMD_MR_PAINT_PENDING_INSPIRATION 1
// Mr. Paint (side feature 0.4.12 "the ball animation"): re-binds the follower's 3D model in the
// middle of the swap script, while opcode 600 has it hidden. Carries no operand at all - arg0 is
// read before the switch for every command and is simply ignored here, the way
// NEW_COMMAND_MR_PAINT_SWAP_FOLLOWER_MODEL emits a 0 for it. The state it needs (the tag the
// follower was drawn with before the swap) lives in a file-static in src/mr_paint_follower.c,
// written by the same toggle that queued this script, so there is nothing for a script operand to
// carry. Must match NEW_COMMAND_MR_PAINT_SWAP_FOLLOWER_MODEL in armips/include/scriptmacros.s.
#define SCRIPT_NEW_CMD_MR_PAINT_SWAP_FOLLOWER_MODEL 2
// Mr. Paint (side feature 0.4.13 "the follower comes back"): clears the hidden latch opcode 606
// sets, so the follower is drawn again the moment the swap script ends instead of at the player's
// next step. Operand-less for the same reason as the command above - arg0 is read before the
// switch and ignored here - because everything it needs is reachable from ctx->fsys. Must match
// NEW_COMMAND_MR_PAINT_SHOW_FOLLOWER in armips/include/scriptmacros.s.
#define SCRIPT_NEW_CMD_MR_PAINT_SHOW_FOLLOWER 3
// RESTORED 0.4.30 (docs/mr-paint-swap-flicker.md), Center-only: records the outgoing follower's
// tile/facing before opcode 600 hides it, for MrPaintReleaseAtRecordedTile (opcode 7) below. Was
// RETIRED 0.4.28 (docs/mr-paint-swap-bike.md) when the Bag/Y toggle switched to the bike's
// deferred-to-the-next-step release, which needs no recorded tile - the Poke Center's own
// pokecen_anim still does, since it needs an already-released follower with no player step in
// between, so this id is back in use for _mr_paint_swap_body_center alone.
#define SCRIPT_NEW_CMD_MR_PAINT_RECORD_FOLLOWER_TILE 4
// Mr. Paint (side feature 0.4.28 "release like the bike", James 0.4.27 item 1): arms the follower
// object exactly the way Task_MountOrDismountBicycle's own dismount does
// (ov01_02205790/sub_02069E84/sub_02069DC8), then leaves the emerge and the un-hide to retail's
// own per-step handler on the player's next step, replacing 0.4.23-0.4.27's immediate
// position-and-emerge. Operand-less. Must match NEW_COMMAND_MR_PAINT_EMERGE_AT_RECORDED_TILE in
// armips/include/scriptmacros.s (opcode id unchanged since 0.4.23; only the behaviour changed).
#define SCRIPT_NEW_CMD_MR_PAINT_EMERGE_AT_RECORDED_TILE 5
// Mr. Paint (side feature 0.4.24 "Poke Center nurse recall"): runs the IDENTITY half of the Bag/Y
// toggle (FollowMon_ChangeMon plus its restore guard) from inside the nurse's own common script,
// writing into resultVar (arg0, this command's one operand) 0/1/2 per src/mr_paint_follower.c's
// MrPaintNurseRecall. Must match NEW_COMMAND_MR_PAINT_NURSE_RECALL in
// armips/include/scriptmacros.s.
#define SCRIPT_NEW_CMD_MR_PAINT_NURSE_RECALL 6
// Mr. Paint (side feature 0.4.30 "Center immediate release", Center-only): restores the recorded
// tile from opcode 4 above, arms and plays the native emerge effect immediately (no player step),
// and un-hides - MrPaintReleaseAtRecordedTile's restored pre-0.4.28 body. Must match
// NEW_COMMAND_MR_PAINT_RELEASE_AT_RECORDED_TILE in armips/include/scriptmacros.s.
#define SCRIPT_NEW_CMD_MR_PAINT_RELEASE_AT_RECORDED_TILE 7
// Mr. Paint (side feature 0.4.33 "Teleport trick"): runs retail's own FieldMove_CheckTeleport
// against the current field and, only when it reports OK, starts retail's own Task_FieldTeleport
// via TaskManager_Call - see src/mr_paint.c's MrPaintTeleport for the full contract. Writes
// MrPaintTeleport's own return value into resultVar (arg0): 1 = can't be used here (NOT_HERE),
// 3 = a story companion is following (HAVE_FOLLOWER), 0 = OK, and this case additionally yields
// the SCRIPT (returns TRUE) so Task_FieldTeleport owns control until the warp finishes - the same
// contract ScrCmd_FollowMonInteract's TaskManager_Call above already uses. Must match
// NEW_COMMAND_MR_PAINT_TELEPORT in armips/include/scriptmacros.s.
#define SCRIPT_NEW_CMD_MR_PAINT_TELEPORT 8

#define SCRIPT_NEW_CMD_MAX 256

BOOL Script_RunNewCmd(SCRIPTCONTEXT *ctx)
{
    u8 sw = ScriptReadByte(ctx);
    u16 UNUSED arg0 = ScriptReadHalfword(ctx);

    switch (sw) {
    case SCRIPT_NEW_CMD_REPEL_USE:;
#ifdef IMPLEMENT_REUSABLE_REPELS
        u16 most_recent_repel = Repel_GetMostRecent();
        SetScriptVar(arg0, most_recent_repel);
        Repel_Use(most_recent_repel, HEAPID_MAIN_HEAP);
#endif
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_PENDING_INSPIRATION:;
        u16 itemVar = ScriptReadHalfword(ctx);
        u16 pendingItem = GetScriptVar(MR_PAINT_PENDING_ITEM_VAR);
        u16 msgIndex = 0;
        if (pendingItem != 0) {
            u16 move = ItemToMachineMove(pendingItem);
            msgIndex = MrPaintMessageIndexForMove(move);
            SetScriptVar(itemVar, pendingItem);
            SetScriptVar(MR_PAINT_PENDING_ITEM_VAR, 0);
        }
        SetScriptVar(arg0, msgIndex);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_SWAP_FOLLOWER_MODEL:
        // 0.4.27: only arm the load-complete poll when a swap actually started - see
        // MrPaintRebindFollowerModel / MrPaintBeginFollowerModelSwapWait in
        // src/mr_paint_follower.c. Returning TRUE here yields the script on the native callback,
        // the same contract src/mr_paint.c's ScrCmd_183 uses for its own native wait.
        if (MrPaintRebindFollowerModel(ctx->fsys)) {
            MrPaintBeginFollowerModelSwapWait(ctx);
            return TRUE;
        }
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_SHOW_FOLLOWER:
        MrPaintShowFollower(ctx->fsys);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_RECORD_FOLLOWER_TILE:
        MrPaintRecordFollowerTile(ctx->fsys);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_EMERGE_AT_RECORDED_TILE:
        MrPaintArmFollowerRelease(ctx->fsys);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_NURSE_RECALL:
        SetScriptVar(arg0, MrPaintNurseRecall(ctx->fsys));
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_RELEASE_AT_RECORDED_TILE:
        MrPaintReleaseAtRecordedTile(ctx->fsys);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_TELEPORT:;
        u16 mrPaintTeleportResult = MrPaintTeleport(ctx->fsys);
        SetScriptVar(arg0, mrPaintTeleportResult);
        if (mrPaintTeleportResult == 0) {
            // OK: Task_FieldTeleport now owns the field until the warp finishes. Yield the
            // calling script exactly the way ScrCmd_FollowMonInteract's TaskManager_Call does.
            return TRUE;
        }
        break;

    default:
        break;
    }

    return FALSE;
}

#ifdef EXPAND_ROAMERS
BOOL LONG_CALL ScrCmd_CreateRoamer(SCRIPTCONTEXT *ctx)
{
    u8 roamerNo = ScriptReadByte(ctx);
    Save_CreateRoamerByID(ctx->fsys->savedata, roamerNo);
    return FALSE;
}
#endif // EXPAND_ROAMERS
