#ifndef GAME_RULES_H
#define GAME_RULES_H

#include "types.h"

/* Data-driven game rules. The Game Mode Select UI never names a rule: it draws RULE_COUNT rows from
 * gGameRules[] and shows/edits the values of gGamePresets[]. Adding a rule = one enum entry, one table
 * row, two text lines in archive 40 and one free script flag. */

enum GameRuleId {
    RULE_LEVEL_CAPS = 0,
    RULE_COUNT
};

enum GameModeId {
    GMS_MODE_NONE = 0, /* legacy / old save / mode never chosen */
    GMS_MODE_OVER_EASY,
    GMS_MODE_SOFTBOILED,
    GMS_MODE_HARDBOILED,
    GMS_MODE_SCRAMBLED,
    GMS_MODE_COUNT
};

struct GameRuleDef {
    u16 labelMsg;          /* archive 40 index of the row label */
    u16 descMsg;           /* archive 40 index of the description dialog */
    u16 storageFlag;       /* script flag holding the rule */
    u8 flagMeansOff;       /* 1: flag set = rule OFF (keeps old zeroed saves ON) */
    u8 scrambledDefault;   /* initial toggle value in Scrambled (0 = OFF) */
};

struct GamePresetDef {
    u16 nameMsg;           /* button label */
    u16 topMsg;            /* top-screen text for this mode */
    u32 ruleValues;        /* bit i = value of rule i (1 = ON) shown/locked */
    u8 editable;           /* Scrambled only */
    u8 selectable;         /* 0 = Confirm shows the "not ready" dialog and never progresses */
};

extern const struct GameRuleDef gGameRules[RULE_COUNT];
extern const struct GamePresetDef gGamePresets[GMS_MODE_COUNT]; /* index 0 unused */

/* Reads the flag through SaveBlock2_get(): safe in battle, on the field and during the Oak intro. */
BOOL LONG_CALL GameRule_IsEnabled(u32 rule);
/* Writes every rule flag plus the three mode-bit flags. ruleValues: bit i = rule i ON. */
void LONG_CALL GameRules_Commit(u32 mode, u32 ruleValues);
u32 LONG_CALL GameRules_GetMode(void);
/* Bitmask of every rule's scrambledDefault. */
u32 LONG_CALL GameRules_ScrambledDefaults(void);

#endif // GAME_RULES_H
