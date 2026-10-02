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
#define GMS_MSG_DESC_SUPER_ITEMS 152 /* existing placeholder: "Whoops! Looks like thereâs nothing here!" */

/* Script flags holding the chosen game mode (unused by retail and by every other feature of this project). */
#define FLAG_GMS_LEVEL_CAPS_OFF 2300 /* 0x8FC: set = level caps disabled */
#define FLAG_GMS_MODE_BIT0      2301 /* 0x8FD: mode id bit 0 */
#define FLAG_GMS_MODE_BIT1      2302 /* 0x8FE: mode id bit 1 */
#define FLAG_GMS_MODE_BIT2      2303 /* 0x8FF: mode id bit 2 */
#define FLAG_GMS_SUPER_ITEMS 2304 /* 0x900 (retail FLAG_UNK_900, unused): set = Super Items rule ON */

#endif // GAME_MODE_SELECT_H
