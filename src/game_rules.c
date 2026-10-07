#include "../include/game_rules.h"

#include "../include/constants/item.h"
#include "../include/game_mode_select.h"
#include "../include/save.h"
#include "../include/types.h"

const struct GameRuleDef gGameRules[RULE_COUNT] = {
    [RULE_LEVEL_CAPS]       = { GMS_MSG_LEVEL_CAPS, GMS_MSG_DESC_LEVEL_CAPS, FLAG_GMS_LEVEL_CAPS_OFF, 1, 0, GAMERULE_CAT_GAME },
    [RULE_SUPER_ITEMS]      = { GMS_MSG_SUPER_ITEMS, GMS_MSG_DESC_SUPER_ITEMS, FLAG_GMS_SUPER_ITEMS, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_MAX_MR_PAINT]     = { GMS_MSG_MAX_MR_PAINT, GMS_MSG_DESC_MAX_MR_PAINT, FLAG_GMS_MAX_MR_PAINT, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_MAX_FLY]          = { GMS_MSG_MAX_FLY, GMS_MSG_DESC_MAX_FLY, FLAG_GMS_MAX_FLY, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_ALL_KEY_ITEMS]    = { GMS_MSG_ALL_KEY_ITEMS, GMS_MSG_DESC_ALL_KEY_ITEMS, FLAG_GMS_ALL_KEY_ITEMS, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_MAX_CASH]         = { GMS_MSG_MAX_CASH, GMS_MSG_DESC_MAX_CASH, FLAG_GMS_MAX_CASH, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_MAX_SALE]         = { GMS_MSG_MAX_SALE, GMS_MSG_DESC_MAX_SALE, FLAG_GMS_MAX_SALE, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_PORYGIFT]         = { GMS_MSG_PORYGIFT, GMS_MSG_DESC_PORYGIFT, FLAG_GMS_PORYGIFT, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_AUTO_BATTLE]      = { GMS_MSG_AUTO_BATTLE, GMS_MSG_DESC_AUTO_BATTLE, FLAG_GMS_AUTO_BATTLE, 0, 0, GAMERULE_CAT_DEBUG },
    [RULE_AUTO_DIALOGUE]    = { GMS_MSG_AUTO_DIALOGUE, GMS_MSG_DESC_AUTO_DIALOGUE, FLAG_GMS_AUTO_DIALOGUE, 0, 0, GAMERULE_CAT_DEBUG },
};

/* Preset rule values, one explicit term per rule (0 = OFF). Every preset value is OFF until the client curates them,
 * and presets only show the GAME category (GAMERULE_CATS_GAME), so no debug flag can be set by starting a preset. */
#define RV(rule, on) ((u32)(on) << (rule))

const struct GamePresetDef gGamePresets[GMS_MODE_COUNT] = {
    [GMS_MODE_OVER_EASY]  = { GMS_MSG_OVER_EASY,  GMS_MSG_TOP_NOT_READY, RV(RULE_LEVEL_CAPS, 0) | RV(RULE_SUPER_ITEMS, 0) | RV(RULE_MAX_MR_PAINT, 0) | RV(RULE_MAX_FLY, 0) | RV(RULE_ALL_KEY_ITEMS, 0) | RV(RULE_MAX_CASH, 0) | RV(RULE_MAX_SALE, 0) | RV(RULE_PORYGIFT, 0) | RV(RULE_AUTO_BATTLE, 0) | RV(RULE_AUTO_DIALOGUE, 0), 0, 0, GAMERULE_CATS_GAME },
    [GMS_MODE_SOFTBOILED] = { GMS_MSG_SOFTBOILED, GMS_MSG_TOP_NOT_READY, RV(RULE_LEVEL_CAPS, 0) | RV(RULE_SUPER_ITEMS, 0) | RV(RULE_MAX_MR_PAINT, 0) | RV(RULE_MAX_FLY, 0) | RV(RULE_ALL_KEY_ITEMS, 0) | RV(RULE_MAX_CASH, 0) | RV(RULE_MAX_SALE, 0) | RV(RULE_PORYGIFT, 0) | RV(RULE_AUTO_BATTLE, 0) | RV(RULE_AUTO_DIALOGUE, 0), 0, 0, GAMERULE_CATS_GAME },
    [GMS_MODE_HARDBOILED] = { GMS_MSG_HARDBOILED, GMS_MSG_TOP_NOT_READY, RV(RULE_LEVEL_CAPS, 0) | RV(RULE_SUPER_ITEMS, 0) | RV(RULE_MAX_MR_PAINT, 0) | RV(RULE_MAX_FLY, 0) | RV(RULE_ALL_KEY_ITEMS, 0) | RV(RULE_MAX_CASH, 0) | RV(RULE_MAX_SALE, 0) | RV(RULE_PORYGIFT, 0) | RV(RULE_AUTO_BATTLE, 0) | RV(RULE_AUTO_DIALOGUE, 0), 0, 0, GAMERULE_CATS_GAME },
    [GMS_MODE_SCRAMBLED]  = { GMS_MSG_SCRAMBLED,  GMS_MSG_TOP_SCRAMBLED, 0, 1, 1, GAMERULE_CATS_ALL },
};

/* SUPER ITEMS rows: item, then the replacement message index in archive 222 (full, short) / 831 / 832 / 833. */
static const struct SuperItemDef sSuperItems[] = {
    { ITEM_RARE_CANDY,   2686, 2690, 537, 537, 537 },
    { ITEM_PREMIER_BALL, 2687, 2691, 538, 538, 538 },
    { ITEM_MAX_REPEL,    2688, 2692, 539, 539, 539 },
    { ITEM_ESCAPE_ROPE,  2689, 2693, 540, 540, 540 },
};

/* Return addresses (thumb bit cleared) of the Bag_TakeItem / Pocket_TakeItem call sites that consume the item. Rare Candy
 * is handled in PartyMenu_HandleUseItemOnMon and Repel re-use in Repel_Use, so they have no site here. */
static const u32 sPremierSites[] = { 0x0224ABF4, 0x022233A8, 0x0221DBF0 };
static const u32 sEscapeRopeSites[] = { 0x02065364 };
static const u32 sMaxRepelSites[] = { 0x021FBB58 };

/* james-game 0.5.5: return addresses (thumb bit cleared) of the retail `bl BufferItemName` (0x0200C0CC) calls that buffer the name
 * into a FIXED-WIDTH window slot, which must show the SHORT "Spr. ..." name. All other callers print sentences (FULL name).
 * Each is a Thumb BL in this ROM's bytes (scanned over arm9 + every overlay; no other overlay sharing the base has a BL ending there):
 *  - 0x0207E11C arm9: pret src/party_context_menu.c:966-968 (PartyMenu_PrintContextMenuMonInfo): NewString_ReadMsgData(msg_0300_00182)
 *    then BufferItemName(msgFormat, 1, held item) into window 39, the "ITEM / <held item>" line over the party context menu.
 *    ROM: 0x0207E100 movs r1,#0xB6 (182); 0x0207E118 bl 0x0200C0CC.
 *  - 0x0208C6D6 arm9 (pret asm/unk_0208C3E4.s:387 sub_0208C6B4, BL at 0x0208C6D2): the held item (halfword at +0x23E) is
 *    buffered, then NewString_ReadMsgData(msg 5) expands it into window +0x7D0 - the "Item / <name>" box of the Summary
 *    screen's left panel (every page). Found by sampling the trampoline LR slot 0x0200C0E4 while the Summary opened
 *    (tools/emu/steps/gms055-sum-watch.json: the slot read 0x0208C6D7 = this site, thumb bit set).
 *  - 0x02223624 ov8 (Battle, pret asm/overlay_08.s:14726 ov08_022235D4, BL at 0x02223620): one Battle Bag list button; the held-item
 *    halfword at [battle bag +0x90*pocket + 0x3C], text centred by FontID_String_GetWidth. Sampled: slot = 0x02223625 on opening the list.
 *  - 0x022239F8 ov8 (pret asm/overlay_08.s:15216 ov08_022239CC, BL at 0x022239F4): the Battle Bag selected-item pane ("<name> xN" over the
 *    description), msg 9. Sampled: slot = 0x022239F9 after tapping an item (tools/emu/steps/gms055-bbag-sel.json).
 *    Not listed: ov08_0221E1A8 (BL 0x0221E1EC, pret asm/overlay_08.s:4531) - never reached in game (the "Item used last" button prints no name). */
static BOOL SuperItems_InList(const u32 *sites, u32 n, u32 ret);
static const u32 sSuperShortNameSites[] = { 0x0207E11C, 0x0208C6D6, 0x02223624, 0x022239F8 };

BOOL LONG_CALL SuperItems_IsShortNameSite(u32 callerRet)
{
    return SuperItems_InList(sSuperShortNameSites, sizeof(sSuperShortNameSites) / sizeof(sSuperShortNameSites[0]), callerRet);
}

static SCRIPT_STATE *GameRules_Flags(void)
{
    return (SCRIPT_STATE *)SavArray_Flags_get(SaveBlock2_get());
}

BOOL LONG_CALL GameRule_IsEnabled(u32 rule)
{
    if (rule >= RULE_COUNT) {
        return TRUE;
    }
    BOOL flag = CheckScriptFlagPassSave(GameRules_Flags(), gGameRules[rule].storageFlag);
    return gGameRules[rule].flagMeansOff ? !flag : (flag != 0);
}

static void GameRules_WriteFlag(u16 flag, BOOL set)
{
    if (set) {
        SetScriptFlagPassSave(GameRules_Flags(), flag);
    } else {
        ClearScriptFlagPassSave(GameRules_Flags(), flag);
    }
}

/* MAX FLY: offsets from flag 2480 of the Fly destinations unlocked on commit (27 of the 38 slots; 23-26, 28, 29, 31, 32, 34, 36, 37 stay locked). */
static const u8 sMaxFlyFlags[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 27, 30, 33, 35 };
#define MAX_FLY_FLAG_BASE 2480

void LONG_CALL GameRules_Commit(u32 mode, u32 ruleValues)
{
    u32 shown = (mode < GMS_MODE_COUNT && mode != GMS_MODE_NONE) ? gGamePresets[mode].categories : GAMERULE_CATS_ALL;
    for (u32 i = 0; i < RULE_COUNT; i++) {
        BOOL on = (ruleValues >> i) & 1;
        if (!((shown >> gGameRules[i].category) & 1)) {
            on = FALSE; /* a category the chosen mode hides is never switched on */
        }
        GameRules_WriteFlag(gGameRules[i].storageFlag, gGameRules[i].flagMeansOff ? !on : on);
    }
    if (CheckScriptFlagPassSave(GameRules_Flags(), FLAG_GMS_MAX_FLY)) { /* set above only when the rule is ON and its category shown */
        for (u32 i = 0; i < sizeof(sMaxFlyFlags); i++) {
            GameRules_WriteFlag(MAX_FLY_FLAG_BASE + sMaxFlyFlags[i], TRUE);
        }
        Pokegear_SetMapUnlockLevel(SaveData_Pokegear_Get(SaveBlock2_get()), 2);
    }
    if (CheckScriptFlagPassSave(GameRules_Flags(), FLAG_GMS_MAX_MR_PAINT)) { /* MAX MR. PAINT: every move Mr. Paint can learn is known (2208-2219 except 2217 Dig) */
        for (u16 f = 2208; f <= 2219; f++) {
            if (f != 2217) {
                GameRules_WriteFlag(f, TRUE);
            }
        }
    }
    if (CheckScriptFlagPassSave(GameRules_Flags(), FLAG_GMS_MAX_CASH)) { /* MAX CASH: the starting wallet is the 999,999 cap, through the real SetMoney */
        PlayerProfile_SetMoney(Sav2_PlayerData_GetProfileAddr(SaveBlock2_get()), MAX_MONEY);
    }
    for (u32 i = 0; i < 3; i++) {
        GameRules_WriteFlag(FLAG_GMS_MODE_BIT0 + i, (mode >> i) & 1);
    }
}

const struct SuperItemDef *LONG_CALL SuperItems_Active(u16 item)
{
    if (!GameRule_IsEnabled(RULE_SUPER_ITEMS)) {
        return NULL;
    }
    for (u32 i = 0; i < sizeof(sSuperItems) / sizeof(sSuperItems[0]); i++) {
        if (sSuperItems[i].item == item) {
            return &sSuperItems[i];
        }
    }
    return NULL;
}

static BOOL SuperItems_InList(const u32 *sites, u32 n, u32 ret)
{
    for (u32 i = 0; i < n; i++) {
        if (sites[i] == ret) {
            return TRUE;
        }
    }
    return FALSE;
}

BOOL LONG_CALL SuperItems_SkipTake(u16 item, u32 callerRet)
{
    if (SuperItems_Active(item) == NULL) {
        return FALSE;
    }
    switch (item) {
    case ITEM_PREMIER_BALL:
        return SuperItems_InList(sPremierSites, sizeof(sPremierSites) / sizeof(sPremierSites[0]), callerRet);
    case ITEM_ESCAPE_ROPE:
        return SuperItems_InList(sEscapeRopeSites, sizeof(sEscapeRopeSites) / sizeof(sEscapeRopeSites[0]), callerRet);
    case ITEM_MAX_REPEL:
        return SuperItems_InList(sMaxRepelSites, sizeof(sMaxRepelSites) / sizeof(sMaxRepelSites[0]), callerRet);
    default:
        return FALSE;
    }
}

u32 LONG_CALL GameRules_GetMode(void)
{
    SCRIPT_STATE *flags = GameRules_Flags();
    u32 mode = 0;
    for (u32 i = 0; i < 3; i++) {
        mode |= (CheckScriptFlagPassSave(flags, FLAG_GMS_MODE_BIT0 + i) ? 1u : 0u) << i;
    }
    return mode;
}

u32 LONG_CALL GameRules_ScrambledDefaults(void)
{
    u32 values = 0;
    for (u32 i = 0; i < RULE_COUNT; i++) {
        if (gGameRules[i].scrambledDefault) {
            values |= 1u << i;
        }
    }
    return values;
}

/* MAX MR. PAINT Surf badge bypass: the single `bl PlayerProfile_TestBadgeFlag` at ov1 0x021E7534 (Surf prompt, Fog badge)
 * is redirected here by the `bl` line in `hooks`. Never hook the retail function itself: gyms and shops share it. */
BOOL LONG_CALL MaxMrPaint_TestBadgeFlag(struct PlayerProfile *profile, s32 badge)
{
    if (CheckScriptFlagPassSave(GameRules_Flags(), FLAG_GMS_MAX_MR_PAINT)) {
        return TRUE;
    }
    return PlayerProfile_TestBadgeFlag(profile, badge);
}

/* MAX SALE: the Poke Mart sale credit is the only AddMoney call in the Bag overlay (ov15, `bl 0x02029044` at 0x021FD27C);
 * the `bl` line in `hooks` retargets it here. Same signature as AddMoney (r0 = profile, r1 = amount). With the rule ON
 * the wallet is refilled to the cap after the real credit. Returns the new money. */
u32 LONG_CALL MaxSale_AddMoney(struct PlayerProfile *profile, u32 amount)
{
    PlayerProfile_AddMoney(profile, amount);
    if (GameRule_IsEnabled(RULE_MAX_SALE)) {
        PlayerProfile_SetMoney(profile, MAX_MONEY);
    }
    return PlayerProfile_GetMoney(profile);
}
