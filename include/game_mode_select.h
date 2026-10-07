#ifndef GAME_MODE_SELECT_H
#define GAME_MODE_SELECT_H

/* Archive 40 (hg-engine-owned, data/text/040.txt) message indices for the Game Mode Select screens.
 * Indices 0-148 belong to Mr. Paint; Game Mode Select appends from 149. */
#define GMS_MSG_TOP_MAIN      149
#define GMS_MSG_TOP_SCRAMBLED 150
#define GMS_MSG_TOP_NOT_READY 151
#define GMS_MSG_WHOOPS        152
#define GMS_MSG_SURE          153
#define GMS_MSG_SORRY         154
#define GMS_MSG_OVER_EASY     155
#define GMS_MSG_SOFTBOILED    156
#define GMS_MSG_HARDBOILED    157
#define GMS_MSG_SCRAMBLED     158
#define GMS_MSG_LEVEL_CAPS    159
#define GMS_MSG_ON            160
#define GMS_MSG_OFF           161
#define GMS_MSG_CONFIRM       162
#define GMS_MSG_CANCEL        163
#define GMS_MSG_DESC_LEVEL_CAPS 164
#define GMS_MSG_GAME_RULES      165
#define GMS_MSG_DEBUG_RULES     166
/* 167/168 (old SUPER RARE CANDY label/description) are now dead text, kept so no archive-40 index moves. */
#define GMS_MSG_SUPER_ITEMS 169
#define GMS_MSG_DESC_SUPER_ITEMS 170 /* "A handful of helpful items\nfor rapid testing" (client verbatim, break ours) */
#define GMS_MSG_MAX_MR_PAINT 171
#define GMS_MSG_DESC_MAX_MR_PAINT 172
#define GMS_MSG_MAX_FLY 173
#define GMS_MSG_DESC_MAX_FLY 174
#define GMS_MSG_ALL_KEY_ITEMS 175
#define GMS_MSG_DESC_ALL_KEY_ITEMS 176
#define GMS_MSG_ALL_KEY_ITEMS_RECEIPT 177 /* commonscript 2080 (scr_seq_00003_commonscript.s) uses the literal 177 */
#define GMS_MSG_MAX_CASH 178
#define GMS_MSG_DESC_MAX_CASH 179
#define GMS_MSG_MAX_SALE 180
#define GMS_MSG_DESC_MAX_SALE 181
#define GMS_MSG_PORYGIFT 182
#define GMS_MSG_DESC_PORYGIFT 183
#define GMS_MSG_PORYGIFT_RECEIPT 184 /* commonscript 2081 (scr_seq_00003_commonscript.s) uses the literal 184 */

/* Script flags holding the chosen game mode (unused by retail and by every other feature of this project). */
#define FLAG_GMS_LEVEL_CAPS_OFF 2300 /* 0x8FC: set = level caps disabled */
#define FLAG_GMS_MODE_BIT0      2301 /* 0x8FD: mode id bit 0 */
#define FLAG_GMS_MODE_BIT1      2302 /* 0x8FE: mode id bit 1 */
#define FLAG_GMS_MODE_BIT2      2303 /* 0x8FF: mode id bit 2 */
#define FLAG_GMS_SUPER_ITEMS 2304 /* 0x900 (retail FLAG_UNK_900, unused): set = Super Items rule ON */
#define FLAG_GMS_MAX_MR_PAINT 2305 /* 0x901 (retail FLAG_UNK_901): set = MAX MR. PAINT ON */
#define FLAG_GMS_MAX_FLY      2306 /* 0x902 (retail FLAG_UNK_902): set = MAX FLY ON */
#define FLAG_GMS_ALL_KEY_ITEMS 2307 /* 0x903 (retail FLAG_UNK_903): set = ALL KEY ITEMS ON */
#define FLAG_GMS_MAX_CASH     2308 /* 0x904: set = MAX CASH ON */
#define FLAG_GMS_MAX_SALE     2309 /* 0x905: set = MAX SALE ON */
#define FLAG_GMS_PORYGIFT     2310 /* 0x906: set = PORYGIFT ON (commonscript 2081 reads it via the ROM script, no Commit side effect) */

#endif // GAME_MODE_SELECT_H
