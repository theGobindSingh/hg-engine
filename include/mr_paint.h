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
//
// 2026-09-16 user decision: Mr. Paint's scope is the seven context-aware OBSTACLE moves - Cut,
// Surf, Rock Smash, Strength, Waterfall, Whirlpool, Rock Climb. Fly (0x8A1), Flash (0x8A4) and
// Dig (0x8A9) are CUT from this build: receiving HM02/TM70/TM28 while holding Mr. Paint behaves
// exactly like vanilla - item obtained, no flag set, no inspiration message. Their rows below
// stay in place (flag ids and the archive-40 message index mapping 1..10 are unchanged) so the
// message-index contract doesn't shift, but `learnable` is 0 for them. Along with Headbutt
// (0x8AA) and Sweet Scent (0x8AB), which have no machine at all, all five flags stay reserved
// and are never set by this build; their data/text/040.txt lines stay in place, unreferenced.
typedef struct MrPaintMoveEntry {
    u16 move;
    u16 flag;
    u16 learnable;
} MrPaintMoveEntry;

#define MR_PAINT_NUM_MACHINE_MOVES 10

extern const MrPaintMoveEntry gMrPaintMoveEntries[MR_PAINT_NUM_MACHINE_MOVES];

// 0 if move isn't one Mr. Paint tracks.
u16 MrPaintFlagForMove(u16 move);

// 1-based index into gMrPaintMoveEntries, matching archive-40 messages 121-130
// (data/text/040.txt) in the same order. 0 if move isn't one Mr. Paint tracks.
u16 MrPaintMessageIndexForMove(u16 move);

// Same lookup as MrPaintFlagForMove, but returns 0 unless the entry is also marked
// `learnable` (the seven obstacle moves). This is the learning path (Bag_AddItem) only -
// the obstacle path (ScrCmd_GetPartySlotWithMove) keeps using MrPaintFlagForMove, since a
// flag can be set on an already-learned move without this build ever setting it itself.
u16 MrPaintLearnableFlagForMove(u16 move);

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

// Slice 0.3.7 "deferred inspiration" - common-script 3 entry id as EventSet_Script takes it,
// i.e. 2000 + the scrdef index of scr_seq_0003_074_mr_paint_inspiration. Same numbering as
// src/repel.c's 2072/2022 and src/bag.c's 2073.
#define MR_PAINT_INSPIRATION_SCRIPT 2074

// Queues the inspiration prompt as a script of its own when one is pending (var 0x4059 != 0),
// so it plays AFTER the giver's whole conversation instead of cutting into it. Called once per
// completed player step from PlayerStepEvent_RepelCounterDecrement; returns TRUE if it queued
// a script, matching that callback's "an event was set for this step" contract.
BOOL MrPaintTryQueueInspiration(FieldSystem *fieldSystem);

#endif // GUARD_MR_PAINT_H
