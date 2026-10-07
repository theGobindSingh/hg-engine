#include "config.h"

#include "constants/file.h"
#include "game_rules.h"
#include "script.h"
#include "types.h"

/*
 * AUTO-DIALOGUE (RULE_AUTO_DIALOGUE, flag 2312), script half. The four "wait for a button" script commands yield to a native
 * callback until the player presses; with the rule ON they return FALSE (continue the script at once). Rule OFF reproduces
 * the retail handlers exactly: SetupNativeScript(ctx, retail callback); return TRUE (348 first reads its halfword delay
 * operand into ctx->data[0]). Handlers hooked in `hooks`; callbacks are the retail ones (Thumb bit set).
 * Field overlay 1 and this file's overlay 131 (OVERLAY_FIELD_EXTENSION) are loaded together (gLinkedOverlayList).
 */
extern void LONG_CALL SetupNativeScript(SCRIPTCONTEXT *ctx, ScrCmdFunc ptr);

BOOL ScrCmd_WaitABPress(SCRIPTCONTEXT *ctx)
{
    if (GameRule_IsEnabled(RULE_AUTO_DIALOGUE)) {
        return FALSE;
    }
    SetupNativeScript(ctx, (ScrCmdFunc)0x02041001);
    return TRUE;
}

BOOL ScrCmd_WaitButtonOrDelay(SCRIPTCONTEXT *ctx)
{
    u16 delay = ScriptGetVar(ctx);
    if (GameRule_IsEnabled(RULE_AUTO_DIALOGUE)) {
        return FALSE;
    }
    ctx->data[0] = delay;
    SetupNativeScript(ctx, (ScrCmdFunc)0x02041041);
    return TRUE;
}

BOOL ScrCmd_WaitButton(SCRIPTCONTEXT *ctx)
{
    if (GameRule_IsEnabled(RULE_AUTO_DIALOGUE)) {
        return FALSE;
    }
    SetupNativeScript(ctx, (ScrCmdFunc)0x02041075);
    return TRUE;
}

BOOL ScrCmd_WaitButtonOrDpad(SCRIPTCONTEXT *ctx)
{
    if (GameRule_IsEnabled(RULE_AUTO_DIALOGUE)) {
        return FALSE;
    }
    SetupNativeScript(ctx, (ScrCmdFunc)0x020410F1);
    return TRUE;
}
