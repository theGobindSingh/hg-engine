#include "config.h"

#include "game_rules.h"
#include "save.h"
#include "types.h"

/*
 * LOCK SET MODE (RULE_LOCK_SET_MODE, flag 2313): Battle Style is locked to Set (value 1).
 *  - GameRules_Commit writes battleStyle = 1 into the save when the rule is ON.
 *  - Options_GetBattleStyle (arm9 0x0202AD90, 8 bytes, hooked with a register-3 tail jump) is replaced by
 *    LockSetMode_GetBattleStyle: Set whenever the rule is ON, whatever the stored bit says (covers the Options screen's
 *    initial value, which it reads through this getter, and every battle read).
 *  - Options screen (overlay 54): the cursor may not rest on the Battle Style row (menu entry 2), and its two touch
 *    hitboxes (5, 6) are dead. Row selection is `(cur + 6) % 7` (Up, blx 0x020F2BA4 at 0x021E66F0) and `(cur + 1) % 7`
 *    (Down, 0x021E6726); the retail blx ARM divmod returns r0 = quotient, r1 = remainder, and the caller only keeps r1.
 *    The replacements below return the same pair as a u64 (low = r0, high = r1) and, when ON, skip row 2:
 *    Up lands on row 1 instead, Down on row 3. No libgcc division is used (the divisor is always 7 and x < 14).
 *    The touch hook wraps TouchscreenHitbox_FindRectAtTouchNew (0x02025224) at 0x021E682C and returns -1 (no hit) for 5 and 6.
 * Lives in overlay 129 (always resident).
 */
#define GF_SAVE_PTR (*(volatile u32 *)0x021D2228) /* SaveBlock2_get's backing pointer: 0 until the save is set up */
#define OPTIONS_ROW_BATTLE_STYLE 2

extern int LONG_CALL TouchscreenHitbox_FindRectAtTouchNew(const void *hitboxes);

static BOOL LockSetMode_On(void)
{
    return GF_SAVE_PTR != 0 && GameRule_IsEnabled(RULE_LOCK_SET_MODE);
}

u32 LockSetMode_GetBattleStyle(struct OPTIONS *options)
{
    if (LockSetMode_On()) {
        return 1;
    }
    return options->battleStyle;
}

static u64 LockSetMode_DivMod7(u32 x, u32 skipTo)
{
    u32 quot = 0;
    u32 rem = x;
    while (rem >= 7) {
        rem -= 7;
        quot++;
    }
    if (rem == OPTIONS_ROW_BATTLE_STYLE && LockSetMode_On()) {
        rem = skipTo;
    }
    return ((u64)rem << 32) | quot;
}

u64 LockSetMode_UpMod(u32 x)
{
    return LockSetMode_DivMod7(x, 1);
}

u64 LockSetMode_DownMod(u32 x)
{
    return LockSetMode_DivMod7(x, 3);
}

int LockSetMode_TouchHit(const void *hitboxes)
{
    int hit = TouchscreenHitbox_FindRectAtTouchNew(hitboxes);
    if ((hit == 5 || hit == 6) && LockSetMode_On()) {
        return -1;
    }
    return hit;
}
