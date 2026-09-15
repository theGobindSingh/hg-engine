#ifndef GUARD_MR_PAINT_H
#define GUARD_MR_PAINT_H

#include "types.h"

// Mr. Paint (feature 3, slice 0.3.1 "learn") - see docs/mr-paint.md in the james-game repo for
// the full research record. This header is the single source of truth shared by src/bag.c
// (which sets the flag + stashes the item id on Bag_AddItem) and src/script_new_cmds.c (the new
// opcode-208 subcommand that turns that into the client's inspiration message), so the move
// order used for the reserved flags and for the archive-40 message indices can't drift apart.
//
// Var 0x4059 (VAR_UNK_4059, confirmed unused by any ROM script - see james-game
// docs/scripting-reference.md's safe-var-range table) stashes the item id of the HM/TM Mr. Paint
// just "learned" between Bag_AddItem setting it and the script command that reads it back.
#define MR_PAINT_PENDING_ITEM_VAR 0x4059

// One row per HM/TM-reachable field move Mr. Paint can learn, in the same order as the client's
// 12 reserved flags 0x8A0-0x8AB (Cut..Dig - the first 10). Headbutt (0x8AA) and Sweet Scent
// (0x8AB) have no machine that teaches them and are deliberately absent from this table; their
// flags stay reserved and unset by this slice.
typedef struct MrPaintMoveEntry {
    u16 move;
    u16 flag;
} MrPaintMoveEntry;

#define MR_PAINT_NUM_MACHINE_MOVES 10

extern const MrPaintMoveEntry gMrPaintMoveEntries[MR_PAINT_NUM_MACHINE_MOVES];

// 0 if move isn't one Mr. Paint tracks.
u16 MrPaintFlagForMove(u16 move);

// 1-based index into gMrPaintMoveEntries, matching archive-40 messages 121-130
// (data/text/040.txt) in the same order. 0 if move isn't one Mr. Paint tracks.
u16 MrPaintMessageIndexForMove(u16 move);

#endif // GUARD_MR_PAINT_H
