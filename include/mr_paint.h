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

// Side feature 0.4.9 "instant follower swap". Same numbering rule: 2000 + the scrdef index of
// scr_seq_0003_075_mr_paint_follower_swap. Its whole body is retail's own send_follower_to_ball
// (opcode 600) / reset_follower_with_ball (opcode 606) pair - the follower returns to its Poke
// Ball and hops straight back out, which is the animation the client asked for by name. Both
// commands are attested in THIS ROM, not taken from an opcode table: the Poke Center nurse
// script (archive 4, Function#49 and Function#59/#78 - the exact moment he described) and
// script 146 Function#80, where retail itself brackets CutAnimation with the same two lines.
#define MR_PAINT_FOLLOWER_SWAP_SCRIPT 2075

// Queues the inspiration prompt as a script of its own when one is pending (var 0x4059 != 0),
// so it plays AFTER the giver's whole conversation instead of cutting into it. Called once per
// completed player step from PlayerStepEvent_RepelCounterDecrement; returns TRUE if it queued
// a script, matching that callback's "an event was set for this step" contract.
BOOL MrPaintTryQueueInspiration(FieldSystem *fieldSystem);

// Side feature 0.4.3 "companion deployment" - src/mr_paint_follower.c.
//
// Using Mr. Paint from the Bag toggles this flag; while it is set, the walking follower RENDERS as
// SMEARGLE instead of the party lead. Nothing is written to the party - the substitution lives
// entirely at the overworld sprite layer, which is what makes the client's "do not mutate the
// party struct" constraint hold by construction rather than by care.
//
// FLAG_UNK_8B0 in armips/include/flags.s. Verified unclaimed: zero hits across all 497 disassembled
// ROM scripts (against a positive control - flag 2218, the 0.4.2 Headbutt tutor flag, DOES hit),
// zero hits in armips/, src/ and include/, and FLAG_UNK_8A0..8B1 are all unnamed in
// pret/pokeheartgold. It sits clear of Mr. Paint's own reserved block 0x8A0-0x8AB. Flags are a
// fixed-size bit array of NUM_FLAGS = 2912 allocated at compile time, so using an id inside
// 0-2911 flips an already-allocated bit and the save layout CANNOT change.
#define FLAG_MR_PAINT_FOLLOWING 2224

// TRUE when the follower should be drawn as Mr. Paint INSTEAD of `species`.
//
// Both conditions are required: the flag is set AND the player still holds the item. The held-item
// half exists so a flag left set by a corrupt or hand-edited save can never strand a player with a
// Smeargle they have no way to dismiss.
//
// `species` is matched against the CURRENT FOLLOWER's real species - the first alive, non-egg party
// member, which is what retail's GetFirstAliveMonInParty_CrashIfNone resolves the follower to. That
// match is what keeps the substitution narrow: get_mon_ow_tag (the sprite lookup we piggyback on)
// is NOT follower-specific, so without it a Hall of Fame or Pokeathlon overworld would swap every
// species at once. See src/mr_paint_follower.c for why the gate cannot instead use followMon.active.
BOOL MrPaintFollowerSubstitutes(u16 species);

// Full-function hook (see hg-engine `hooks`) replacing retail FollowPokeFsysParamSet at
// 0x02069F3C - the writer of the cached follower identity in FieldSystem->followMon. Reproduces
// the retail body (a leaf: four stores, no calls) with the one substitution.
void MrPaintFollowPokeFsysParamSet(FieldSystem *fieldSystem, int species, u8 forme, BOOL shiny, u8 gender);

// Side feature 0.4.10 "shiny Mr. Paint" - src/mr_paint_follower.c. Full-function hook (see
// hg-engine `hooks`) replacing retail FollowMon_SetObjectShiny at 0x0206A080 - the ONLY code in
// the ROM that writes the walking follower's shiny bit. Both of its callers reach it (0x02069EE8,
// and the undocumented 5-argument sibling 0x02069F0C, which is called from overlay 1 and has no
// arm9 caller), so hooking this funnel covers the map load, 0.4.9's instant swap and script
// opcode 606 in one place. Forces the bit on while Mr. Paint is what is drawn, and otherwise
// passes the caller's own value straight through, so a genuinely shiny party lead keeps its
// sparkle and a normal one can never gain one.
//
// Only a pointer to LocalMapObject is needed here, so forward-declare the typedef rather than
// pulling map_events_internal.h into every consumer of this header. Repeating an identical
// typedef is fine - include/pokemon.h:568 already declares this exact line, and
// src/field/hidden_items.c includes both headers today.
typedef struct LocalMapObject LocalMapObject;
void MrPaintFollowMonSetObjectShiny(LocalMapObject *mapObject, BOOL enable);

// 0.4.7 "obstacle move from the follower" (client DoD 5) - src/mr_paint_follower.c. Returns the
// deployed follower's own party slot when, and only when, the object currently drawn behind the
// player really is Mr. Paint (flag set, item held, follower active, and - the load-bearing check
// - followMon.species == SPECIES_SMEARGLE, since the toggle is not immediate and the follower
// cache only updates on a map load). Returns -1 otherwise, which callers treat as "not deployed".
// Consumed by ScrCmd_GetPartySlotWithMove (src/mr_paint.c) so Cut/Rock Smash/Strength take
// script 146's overworld branch (no cut-in) instead of the 0.3.8 sentinel's cutscene branch.
int MrPaintDeployedFollowerSlot(FieldSystem *fieldSystem);

// The toggle, reached from TWO entry points that must never drift apart - both funnel through the
// same static helper in src/mr_paint_follower.c:
//   - `field` column of row 6: Mr. Paint registered to SELECT, SELECT pressed on the overworld.
//   - `menu` column of row 6 (0.4.5): Bag -> USE, via the state-12 "close Bag, hand off to field"
//     idiom proven from this ROM's own ItemMenuUseFunc_EscapeRope/_Honey bytes - see
//     docs/mr-paint-follower.md Findings 21-22 in the james-game repo. Do NOT use sub_0203C8F0
//     (state 5, WAIT_APP) - it keeps the Bag alive and was REJECTED with evidence.
#include "task.h" // TaskManager, TaskFunc
struct ItemFieldUseData;
struct ItemMenuUseData;
struct ItemCheckUseData;
BOOL ItemFieldUseFunc_MrPaintToggle(struct ItemFieldUseData *data);
void ItemMenuUseFunc_MrPaintToggle(struct ItemMenuUseData *data, const struct ItemCheckUseData *dat2);
BOOL Task_MrPaintToggle(TaskManager *taskman);

// 0.4.12 "the ball animation": the second half of the old instant refresh, split out so it can run
// from INSIDE the swap script (MR_PAINT_FOLLOWER_SWAP_SCRIPT above) rather than before it - in the
// window where opcode 600 has already hidden the follower and 606 has not yet popped it back out.
// Re-binding the 3D model there is what makes the player see the LEAD go into the ball and
// Mr. Paint come out, instead of Mr. Paint doing both. Its only caller is src/script_new_cmds.c's
// SCRIPT_NEW_CMD_MR_PAINT_SWAP_FOLLOWER_MODEL, whose value must match
// NEW_COMMAND_MR_PAINT_SWAP_FOLLOWER_MODEL in armips/include/scriptmacros.s.
void MrPaintRebindFollowerModel(FieldSystem *fieldSystem);

// 0.4.13 "the follower comes back": clears the hidden state that opcode 606 LATCHES rather than
// lifts. 606 calls sub_02069DEC(object, TRUE), setting a persistent "keep hidden" bit in the map
// object's param 2, so after 0.4.12's recall the follower stayed invisible until the player took a
// step. sub_02069DC8(obj, FALSE) is the exact inverse - it clears both flag bits AND that latch,
// so the restore survives the next map load. Called from the swap script AFTER both `wait 24`s,
// because retail only ever un-hides from inside the task that owns the recall effect. Its only
// caller is src/script_new_cmds.c's SCRIPT_NEW_CMD_MR_PAINT_SHOW_FOLLOWER, whose value must match
// NEW_COMMAND_MR_PAINT_SHOW_FOLLOWER in armips/include/scriptmacros.s.
void MrPaintShowFollower(FieldSystem *fieldSystem);

// 0.4.23 "spawn tile fix" (James 0.4.17 item 3, docs/mr-paint-swap-polish.md design B). Together
// these two replace BOTH `reset_follower_with_ball` (606) and mr_paint_show_follower above at the
// tail of the swap script: instead of letting 606 park the incoming follower on the PLAYER's tile
// (what it always does - copies the player's own position, see mr_paint_show_follower's comment
// history), the outgoing follower's tile is recorded before the recall and the incoming one is
// placed there directly, with the native emerge effect played immediately instead of deferred to
// the player's next step. Falls back to exactly 606's own behaviour (design A) with no follower,
// no recorded tile, or the recorded tile already equal to the player's - see the .c file.
// Callers are src/script_new_cmds.c's SCRIPT_NEW_CMD_MR_PAINT_RECORD_FOLLOWER_TILE (4) and
// SCRIPT_NEW_CMD_MR_PAINT_EMERGE_AT_RECORDED_TILE (5), whose values must match
// NEW_COMMAND_MR_PAINT_RECORD_FOLLOWER_TILE / NEW_COMMAND_MR_PAINT_EMERGE_AT_RECORDED_TILE in
// armips/include/scriptmacros.s.
void MrPaintRecordFollowerTile(FieldSystem *fieldSystem);
void MrPaintEmergeAtRecordedTile(FieldSystem *fieldSystem);

// Side feature 0.4.17 "follower talk" - src/mr_paint.c. Full-function hook (see hg-engine
// `hooks`) replacing retail ScrCmd_FollowMonInteract (script opcode 711, arm9 0x02047414). Not
// deployed (MrPaintDeployedFollowerSlot < 0): byte-identical to vanilla
// (FieldSystem_FollowMonInteract(fsys); return TRUE;). Deployed: runs vanilla's own
// Task_FollowMonInteract (ov2 0x02250111) through TaskManager_Call exactly as retail does, with
// the talk latch below set for the duration so GetFirstAliveMonInParty_CrashIfNone (also hooked)
// substitutes the static Mr. Paint actor for every "the lead" read inside it. The real party is
// never read for this nor written.
BOOL ScrCmd_FollowMonInteract(SCRIPTCONTEXT *ctx);

// Full-function hook (see hg-engine `hooks`) replacing retail GetFirstAliveMonInParty_CrashIfNone
// (arm9 0x02054388, 14+ callers project-wide). Reproduces the vanilla search exactly (same
// PokeParty_GetPokeCount/Party_GetMonByIndex loop, same RetailPartyMonAliveTest call retail's own
// body makes, same crash-if-none fallback) UNLESS the 0.4.17 talk latch is active for the SAME
// FieldSystem's player party, in which case it returns the static shiny Smeargle actor
// (MrPaintActorMon(), friendship forced to 255) instead of searching the real party at all.
struct PartyPokemon *GetFirstAliveMonInParty_CrashIfNone(struct Party *party);

#endif // GUARD_MR_PAINT_H
