#include "config.h"

#include "constants/file.h"
#include "game_rules.h"
#include "overlay.h"
#include "types.h"

/*
 * AUTO-DIALOGUE (RULE_AUTO_DIALOGUE, flag 2312), page-break half. Replaces retail TextPrinter_Continue (arm9 0x02002A84),
 * called only by the page-break waits TextPrinter_WaitWithDownArrow (0x02002AEC) and TextPrinter_Wait (0x02002B10): TRUE = advance.
 * Retail: if TextPrinter_ContinueInputNew (0x02002220, tests gSystem+0x48 & 3) then PlaySE(0x5DC), set bit 7 of the byte at
 * 0x02111886 (sTextFlags byte 2, "has continued input") and return TRUE; else FALSE.
 * With the rule ON outside battle it advances at once, same bit, no sound. Battle text is left alone (battle overlay loaded).
 * The A/B speed-up during printing uses ContinueInputNew directly and is not touched.
 * Lives in overlay 129 (always resident); the field-script waits are in src/field/auto_dialogue.c.
 */
extern BOOL LONG_CALL TextPrinter_ContinueInputNew(void);
extern void LONG_CALL PlaySE(u32 seq);

#define GF_SAVE_PTR (*(volatile u32 *)0x021D2228) /* SaveBlock2_get's backing pointer: 0 until the save is set up (it asserts then) */
#define TEXT_FLAGS_BYTE2 (*(volatile u8 *)0x02111886)

BOOL TextPrinter_Continue(void *printer)
{
    (void)printer;
    if (GF_SAVE_PTR != 0 && GameRule_IsEnabled(RULE_AUTO_DIALOGUE) && !IsOverlayLoaded(OVERLAY_BATTLE)) {
        TEXT_FLAGS_BYTE2 |= 0x80;
        return TRUE;
    }
    if (TextPrinter_ContinueInputNew()) {
        PlaySE(0x5DC);
        TEXT_FLAGS_BYTE2 |= 0x80;
        return TRUE;
    }
    return FALSE;
}
