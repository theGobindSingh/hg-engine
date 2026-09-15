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
