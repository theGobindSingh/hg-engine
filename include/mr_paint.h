#ifndef GUARD_MR_PAINT_H
#define GUARD_MR_PAINT_H

#include "types.h"
#include "script.h"

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

// Slice 0.3.2 "obstacles" - full-function hook (see hg-engine `hooks`) replacing retail
// ScrCmd_GetPartySlotWithMove (ROM script command 141, CheckMoveInParty) at 0x0204D3CC.
// Reproduces the vanilla search exactly, then falls back to party slot 0 (the lead mon, used
// as the visible stand-in actor) when nothing in the party knows the move but the player holds
// Mr. Paint, has already "learned" that move on it (the matching reserved flag), and the move
// is one of the ones Mr. Paint tracks. Badge gating happens later in the calling ROM script and
// is untouched by this.
BOOL ScrCmd_GetPartySlotWithMove(SCRIPTCONTEXT *ctx);

// Slice 0.3.3 "Smeargle actor" - full-function hooks (see hg-engine `hooks`) that make the
// cutscene actor and the "<name> used X!" message read Mr. Paint / SMEARGLE instead of the
// real lead mon, whenever the 0.3.2 hook above just fell back to the item. See src/mr_paint.c
// for the shared state contract (sMrPaintActorActive) these four coordinate through.

// Replaces retail ScrCmd_183 (0x02043724) - Cut/RockSmash/Headbutt/Strength/Flash actor setup.
BOOL ScrCmd_183(SCRIPTCONTEXT *ctx);

// Replaces retail ScrCmd_BufferPartyMonNick (0x020486F0) - the "<name> used X!" name buffer.
BOOL ScrCmd_BufferPartyMonNick(SCRIPTCONTEXT *ctx);

// Replaces overlay 1's ov01_021F3100 (0x021F3100) - actor resolver for
// Surf / Waterfall / Whirlpool / Rock Climb.
struct PartyPokemon *MrPaintFieldMoveActorMon(FieldSystem *fieldSystem, u32 partyIdx);

// Replaces ScrCmd_End (opcode 2, 0x02040898) - clears sMrPaintActorActive on every script end,
// so a cancelled prompt can never leave a stale actor flag set.
BOOL ScrCmd_End(SCRIPTCONTEXT *ctx);

#endif // GUARD_MR_PAINT_H
