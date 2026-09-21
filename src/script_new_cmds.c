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
        MrPaintRebindFollowerModel(ctx->fsys);
        break;

    case SCRIPT_NEW_CMD_MR_PAINT_SHOW_FOLLOWER:
        MrPaintShowFollower(ctx->fsys);
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
