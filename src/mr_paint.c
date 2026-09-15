#include "../include/mr_paint.h"

#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/constants/moves.h"
#include "../include/types.h"

#include "../include/bag.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"

// PokeParty_GetPokeCount (rom.ld:282, 0x02074640) is linked but not declared in any header -
// follow the LONG_CALL convention of its neighbour Party_GetMonByIndex (include/pokemon.h:930).
extern int LONG_CALL PokeParty_GetPokeCount(struct Party *party);

// Not-found literal vanilla ScrCmd_GetPartySlotWithMove writes into *destVar before searching.
#define MR_PAINT_SLOT_NOT_FOUND 6

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

// Slice 0.3.2 "obstacles". Full-function hook (see hg-engine `hooks`:
// "arm9 ScrCmd_GetPartySlotWithMove 0204D3CC 1") replacing retail ScrCmd_GetPartySlotWithMove
// (ROM script command 141, CheckMoveInParty) at 0x0204D3CC. Reads, in order, the destination
// var (as a pointer) then the move id (as a value) - matching the two operands of the script
// line `CheckMoveInParty 0x800C MOVE_X` - exactly as the retail handler does.
//
// Reproduces the vanilla search byte-for-byte (not-found literal written up front, egg mons
// skipped, MON_DATA_MOVE1..4 compared, party pointer re-derived every iteration to match the
// shipped bytes), then appends one fallback: if nothing was found, the party is non-empty (so
// slot 0 exists), the move is one Mr. Paint tracks and has already been "learned" (its reserved
// flag is set), and the player is holding Mr. Paint, slot 0 (the lead mon) is used as the
// visible stand-in actor. Badge gating lives later in the calling ROM script and is untouched.
// Always returns FALSE, on every path, exactly like vanilla.
BOOL ScrCmd_GetPartySlotWithMove(SCRIPTCONTEXT *ctx)
{
    u16 *destVar = ScriptGetVarPointer(ctx);
    u16 move = ScriptGetVar(ctx);

    FieldSystem *fieldSystem = ctx->fsys;
    struct Party *party = SaveData_GetPlayerPartyPtr(fieldSystem->savedata);
    int partyCount = PokeParty_GetPokeCount(party);
    int i;

    *destVar = MR_PAINT_SLOT_NOT_FOUND;

    for (i = 0; i < partyCount; i++) {
        // Re-derive the party pointer every iteration - matches the shipped retail bytes.
        struct Party *loopParty = SaveData_GetPlayerPartyPtr(fieldSystem->savedata);
        struct PartyPokemon *mon = Party_GetMonByIndex(loopParty, i);

        if (GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }

        if (GetMonData(mon, MON_DATA_MOVE1, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE2, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE3, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE4, NULL) == move) {
            *destVar = (u16)i;
            break;
        }
    }

    if (*destVar == MR_PAINT_SLOT_NOT_FOUND && partyCount > 0) {
        u16 flag = MrPaintFlagForMove(move);

        if (flag != 0 && CheckScriptFlag(flag)) {
            BAG_DATA *bag = Sav2_Bag_get(fieldSystem->savedata);

            if (Bag_HasItem(bag, ITEM_MR_PAINT, 1, HEAPID_WORLD)) {
                *destVar = 0;
            }
        }
    }

    return FALSE;
}
