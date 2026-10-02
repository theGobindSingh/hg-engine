#include "../include/game_rules.h"

#include "../include/game_mode_select.h"
#include "../include/save.h"
#include "../include/types.h"

const struct GameRuleDef gGameRules[RULE_COUNT] = {
    [RULE_LEVEL_CAPS] = { GMS_MSG_LEVEL_CAPS, GMS_MSG_DESC_LEVEL_CAPS, FLAG_GMS_LEVEL_CAPS_OFF, 1, 0 },
};

const struct GamePresetDef gGamePresets[GMS_MODE_COUNT] = {
    [GMS_MODE_OVER_EASY]  = { GMS_MSG_OVER_EASY,  GMS_MSG_TOP_NOT_READY, 0, 0, 0 },
    [GMS_MODE_SOFTBOILED] = { GMS_MSG_SOFTBOILED, GMS_MSG_TOP_NOT_READY, 1u << RULE_LEVEL_CAPS, 0, 0 },
    [GMS_MODE_HARDBOILED] = { GMS_MSG_HARDBOILED, GMS_MSG_TOP_NOT_READY, 1u << RULE_LEVEL_CAPS, 0, 0 },
    [GMS_MODE_SCRAMBLED]  = { GMS_MSG_SCRAMBLED,  GMS_MSG_TOP_SCRAMBLED, 0, 1, 1 },
};

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

void LONG_CALL GameRules_Commit(u32 mode, u32 ruleValues)
{
    for (u32 i = 0; i < RULE_COUNT; i++) {
        BOOL on = (ruleValues >> i) & 1;
        GameRules_WriteFlag(gGameRules[i].storageFlag, gGameRules[i].flagMeansOff ? !on : on);
    }
    for (u32 i = 0; i < 3; i++) {
        GameRules_WriteFlag(FLAG_GMS_MODE_BIT0 + i, (mode >> i) & 1);
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
