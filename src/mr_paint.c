#include "../include/mr_paint.h"

#include "../include/constants/moves.h"
#include "../include/types.h"

// Order matches the client's reserved flag block 0x8A0-0x8AB (docs/mr-paint.md) and
// data/text/040.txt indices 121-130. Headbutt/Sweet Scent have no machine and are omitted.
const MrPaintMoveEntry gMrPaintMoveEntries[MR_PAINT_NUM_MACHINE_MOVES] = {
    { MOVE_CUT, 0x8A0 },
    { MOVE_FLY, 0x8A1 },
    { MOVE_SURF, 0x8A2 },
    { MOVE_STRENGTH, 0x8A3 },
    { MOVE_FLASH, 0x8A4 },
    { MOVE_WHIRLPOOL, 0x8A5 },
    { MOVE_WATERFALL, 0x8A6 },
    { MOVE_ROCK_SMASH, 0x8A7 },
    { MOVE_ROCK_CLIMB, 0x8A8 },
    { MOVE_DIG, 0x8A9 },
};

u16 MrPaintFlagForMove(u16 move)
{
    u32 i;
    for (i = 0; i < MR_PAINT_NUM_MACHINE_MOVES; i++) {
        if (gMrPaintMoveEntries[i].move == move) {
            return gMrPaintMoveEntries[i].flag;
        }
    }
    return 0;
}

u16 MrPaintMessageIndexForMove(u16 move)
{
    u32 i;
    for (i = 0; i < MR_PAINT_NUM_MACHINE_MOVES; i++) {
        if (gMrPaintMoveEntries[i].move == move) {
            return (u16)(i + 1);
        }
    }
    return 0;
}
