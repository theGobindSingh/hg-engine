#include "../include/mr_paint.h"

#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/constants/moves.h"
#include "../include/constants/species.h"
#include "../include/types.h"

#include "../include/bag.h"
#include "../include/message.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"

// PokeParty_GetPokeCount (rom.ld:282, 0x02074640) is linked but not declared in any header -
// follow the LONG_CALL convention of its neighbour Party_GetMonByIndex (include/pokemon.h:930).
extern int LONG_CALL PokeParty_GetPokeCount(struct Party *party);

// Not-found literal vanilla ScrCmd_GetPartySlotWithMove writes into *destVar before searching.
#define MR_PAINT_SLOT_NOT_FOUND 6

// Slice 0.3.8 "actor slot". Bug 3: 0.3.2 returned a hardcoded slot 0 for the stand-in actor.
// ROM script 146 then does `CompareVars 0x8004 0x8005` - the actor slot against the FOLLOWING
// Pokemon's slot (`GetFollowingPokePartySlot`) - and a player whose follower is party slot 0
// (the overwhelmingly common case) makes that EQUAL, sending the flow down the "the follower
// performs the move" branch. That branch never calls CutAnimation (opcode 183), so the 0.3.3
// Smeargle substitution never ran and the real lead animated the move, even though the name
// buffer - which runs earlier and unconditionally - already said "Mr. Paint".
//
// The fix returns a slot the follower can never have. It is deliberately NOT a real alternative
// party index: the follower's slot is unreachable from C here (opcode 727 has no named handler,
// no symbol and no gScriptCmdTable reference anywhere in hg-engine, and the FollowMon struct in
// include/pokemon.h names no party-index field), and a one-Pokemon party has no alternative slot
// to pick anyway. 7 is out of range for both a party slot (0-5) and the not-found value (6), and
// script 146 tests these vars with EQUAL/DIFFERENT only - there is not one ordered comparison in
// the whole 1025-line file - so 7 is never swept into the "nobody knows this move" path.
// See docs/mr-paint-polish.md "Bug 3 - design" for the full consumer audit.
//
// This is safe ONLY because the two commands that dereference the actor slot as a party index
// (opcode 183 CutAnimation and opcode 199 TextPokeNickname) are both hooked below, and both now
// skip the party lookup entirely while substituting. Nothing else on those script paths reads
// the actor slot; HiddenMachineEffect takes the follower's var, not this one.
#define MR_PAINT_ACTOR_SENTINEL_SLOT 7

// Order matches the client's reserved flag block 0x8A0-0x8AB (docs/mr-paint.md) and
// data/text/040.txt indices 121-130. Headbutt/Sweet Scent have no machine and are omitted.
const MrPaintMoveEntry gMrPaintMoveEntries[MR_PAINT_NUM_MACHINE_MOVES] = {
    { MOVE_CUT, 0x8A0, 1 },
    { MOVE_FLY, 0x8A1, 0 },
    { MOVE_SURF, 0x8A2, 1 },
    { MOVE_STRENGTH, 0x8A3, 1 },
    { MOVE_FLASH, 0x8A4, 0 },
    { MOVE_WHIRLPOOL, 0x8A5, 1 },
    { MOVE_WATERFALL, 0x8A6, 1 },
    { MOVE_ROCK_SMASH, 0x8A7, 1 },
    { MOVE_ROCK_CLIMB, 0x8A8, 1 },
    { MOVE_DIG, 0x8A9, 0 },
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

u16 MrPaintLearnableFlagForMove(u16 move)
{
    u32 i;
    for (i = 0; i < MR_PAINT_NUM_MACHINE_MOVES; i++) {
        if (gMrPaintMoveEntries[i].move == move) {
            return gMrPaintMoveEntries[i].learnable ? gMrPaintMoveEntries[i].flag : 0;
        }
    }
    return 0;
}

// Slice 0.3.8. The four obstacle moves whose ROM script 146 flow compares the actor slot
// against the follower's slot - Cut (Function 48), Rock Smash (Function 50), Strength
// (Function 65), Headbutt (Function 60) - and which therefore need the sentinel to force the
// non-follower branch.
//
// 0.4.21: Headbutt's Function 60 is now REACHED, via the opcode-141 special case above that maps
// it straight to flag 0x8AA regardless of gMrPaintMoveEntries[] (which has no Headbutt row).
// Surf, Waterfall, Whirlpool and Rock Climb have no follower branch and no CompareVars at all
// (their flows are terminal: CheckMoveInParty / copy / TextPokeNickname / <Move>Animation /
// Jump Function#11), so they keep slot 0 and behave byte-for-byte as they shipped in 0.3.7 -
// which is what the client's note about those moves requires.
static BOOL MrPaintMoveHasFollowerBranch(u16 move)
{
    return move == MOVE_CUT || move == MOVE_ROCK_SMASH || move == MOVE_STRENGTH
        || move == MOVE_HEADBUTT;
}

// Side feature "water cut-in" (docs/mr-paint-water-attribution2.md, RCA B2). Surf/Waterfall/
// Whirlpool have no follower branch (MrPaintMoveHasFollowerBranch above is deliberately false for
// them - script 146 Functions 13/15/27 are terminal, no CompareVars at all), so the NOT-DEPLOYED
// gap is different from Cut/RockSmash/Strength's: there was never any visual at all when nobody
// in the party knew the move, only the correct "Mr. Paint used X!" text. This is what the client
// reported as (A) in the RCA - the walking lead's own put-away-at-the-water's-edge animation was
// the only thing on screen.
//
// 0.4.29 RCA (re-disassembled from the 0.4.28 ov001.bin, correcting the note below): a first cut
// of this build ALSO inserted an opcode-183 cut-in into script 146, on the belief that the retail
// Surf/Waterfall/Whirlpool tasks never draw one for a non-lead actor. In game that produced the
// shiny Smeargle cut-in TWICE for the not-deployed/no-knower arm - once from the retail task
// itself, once from our script insertion - so the retail task DOES draw a cut-in off a non-lead
// actor, and the earlier claim that it does not was wrong. The script-146 insertion was reverted;
// this function's only remaining job is to gate the sentinel-slot return (opcode 141, above),
// which is what actually makes the native cut-in happen for Mr. Paint.
//
// The real mechanism (re-checked against the 0.4.28 ov001.bin, one call site per move):
// `CallFieldTask_Surf` 0x021F2068 and `_Waterfall` 0x021F2590 each call a shared initialiser,
// ov01_021F3040, which unconditionally sets a stack-struct field (offset 0, call it showCutIn) to
// 1 before anything move-specific runs; that struct - actor slot included, from ov01_021F3100
// (MrPaintFieldMoveActorMon, hooked) - is then copied verbatim into the task the move actually
// runs as (Waterfall visibly branches its own task-type byte, 5 vs 6, on this same field at
// 0x021F2578-0x021F257C). Both have ONE path that can override it back to 0: a player-facing
// check (0x0205C700) that forces showCutIn=1 unconditionally for two of the four facings, and for
// the other two, `cmp r6,r0` against GetFirstAlivePokemonSlot(fieldSystem) at
// 0x021F20A4-0x021F20A8 / 0x021F25CC-0x021F25D0, gated behind ov01_02206268 (a position check
// against the fieldSystem+0xE4 follower object - i.e. "is the walking follower this same lead
// mon") - it only ever overrides to 0 when the actor slot EQUALS the first-alive slot, suppressing
// the splash because the walking follower already shows the move in place.
//
// Whirlpool is NOT at 0x021F2908 - that address's 3-argument, no-gate shape (it preserves all
// three incoming registers and calls a different pair of helpers, 0x021F28B8/0x2050530) matches
// Rock Climb instead (script 146's `RockClimbAnimation`, the fourth move this project already
// excludes from MrPaintMoveNeedsWaterCutIn - it needs the extra argument for its Partner-riding
// state, which Surf/Waterfall/Whirlpool have no use for). ov01_021F2DA4 has Surf/Waterfall's own
// 2-argument shape but, read in full, also skips the inline gate - it is some other terminal task,
// not Whirlpool either. The real Whirlpool gate was found the reliable way: `GetFirstAlivePokemonSlot`
// (0x022062CC) has exactly three callers in the whole overlay, and Surf/Waterfall account for two of
// them (above); the third, at 0x021FCF22 inside a multi-state task function starting 0x021FCE98, is
// Whirlpool's. Read in context: the state at 0x021FCF22 calls GetFirstAlivePokemonSlot, compares it
// (0x021FCF28) against a stored actor slot, and only when they're EQUAL *and* ov01_02069FB0 (follower
// active) is true does it hand off to the in-place follower branch (0x021FCF3C); every other case
// (including every non-match) falls through the task's states to a DIRECT, unconditional call to the
// cut-in drawer itself, `bl 0x02249458` at 0x021FCF5A - the same function Cut's DIFFERENT branch and
// Surf/Waterfall's cut-in path both use. So Whirlpool's route to the cut-in is if anything more
// direct than Surf/Waterfall's, not absent.
//
// Our sentinel slot (out of the party's real range, 0-5) can never equal the first-alive slot on any
// of the three moves, so none of these three overrides ever fires for Mr. Paint: showCutIn (or its
// Whirlpool-task equivalent) stays at "cut-in", and the native code draws it every time, reading the
// acting mon through the same hooked ov01_021F3100 slot each compare itself used - i.e. Mr. Paint's
// shiny Smeargle. All three moves are covered with zero script-146 changes. This also still clears
// risk (b) from the frozen design: every compare found here is a plain integer test, never an array
// index, so the sentinel is never dereferenced outside the party's real range on this path either.
static BOOL MrPaintMoveNeedsWaterCutIn(u16 move)
{
    return move == MOVE_SURF || move == MOVE_WATERFALL || move == MOVE_WHIRLPOOL;
}

// Slice 0.3.3 "Smeargle actor". Set-or-cleared on EVERY call of the 0.3.2
// CheckMoveInParty hook, read (never cleared) by the three actor hooks, and
// force-cleared whenever any script ends - see ScrCmd_End below. Overlay-129
// BSS, proven zero at boot, so this fails safe to vanilla.
static u8 sMrPaintActorActive;

// Side feature 0.4.17 "follower talk" (defined in full further down this file). Declared here,
// ahead of ScrCmd_End, purely so ScrCmd_End's backstop clear can reach them.
static u8 sMrPaintTalkActive;
static FieldSystem *sMrPaintTalkFieldSystem;

// Slice 0.4.14 "attribution". The 0.4.13 gate only ran when the party search FAILED, so the one
// case the client reported - Mr. Paint deployed AND a party Pokemon also knows the move - fell
// straight through to vanilla and named that Pokemon. The gate below is now entered regardless of
// the search result, and DEPLOYED is decided by the 0.4.7 follower gate itself.
//
// One latch, not two. Every deployed path wants Mr. Paint as the actor as well as the name - the
// client asked for both in one sentence - so sMrPaintActorActive is set on all of them, and the
// three readers stay in agreement by construction. It also preserves 0.4.13's failure mode: if the
// follower slot ever failed to match script 146's own CompareVars, the flow falls back to the
// cut-in, and the cut-in still draws Mr. Paint rather than somebody else under his name.
//
// It MUST stay a per-invocation latch and never become a live CheckScriptFlag read inside
// BufferPartyMonNick: opcode 199 is used by 17 different script files, most with nothing to do
// with Mr. Paint (the Day Care among them), so a live check would print "Mr. Paint" in unrelated
// dialogue for as long as flag 2224 is set. Opcode 141 runs only on the seven obstacle flows,
// which is what scopes the override.

// The stand-in actor. Rebuilt deterministically on every use, so nothing
// depends on lazy-init state. 0xEC (236) bytes of overlay-129 BSS.
static struct PartyPokemon sMrPaintActor;

// "Mr. Paint" in the HGSS charmap (charmap.txt: M=0137 r=0156 .=01AE
// space=01DE P=013A a=0145 i=014D n=0152 t=0158), 0xFFFF-terminated.
// 9 characters; the nickname field holds 11.
static const u16 sMrPaintActorNickname[] = {
    0x0137, 0x0156, 0x01AE, 0x01DE, 0x013A, 0x0145, 0x014D, 0x0152, 0x0158, 0xFFFF
};

// Fixed PID ('PaPa'): deterministic, and SHINY against the forced OT id 0 (0.4.10 - it was 'MrPa',
// 0x4D725061, and non-shiny until then).
//
// The cut-in provably reads its shininess off this mon, traced through this ROM's own bytes:
// ScrCmd_183 -> ov02_02249458 stashes the Pokemon * at work+0x5C -> overlay-2 thunk 0x0224A7A8 ->
// arm9 0x02070124 -> 0x0207013C, which calls BoxMonIsShiny (0x02070044, rom.ld:94) at 0x02070168
// and feeds the answer into the char/palette NARC-id selection. So the PID is the palette.
//
// SHINY_VALUE(0, 0x50615061) = 0x5061 ^ 0x5061 = 0, comfortably inside SHINY_ODDS (8,
// include/config.h:130). The whole expansion is integer & >> ^ <=, so it folds at compile time and
// can be asserted rather than trusted. GenerateShinyPIDKeepSubstructuresIntact is deliberately NOT
// used: it calls gf_rand(), so it is non-deterministic and cannot appear in a constant assertion.
//
// 'PaPa' is chosen over the equally shiny 'MrMr' (0x4D724D72) because it keeps the low byte at
// 0x61, byte-identical to the old PID - the sprite resolver reads MON_DATA_GENDER as well as
// shininess, and gender is derived from that byte, so the rendered gender cannot shift. The cry
// path reads only species and form, so the cry is unaffected either way.
//
// Scope limit worth stating plainly: since 0.4.7, Cut / Rock Smash / Strength with the follower
// DEPLOYED take vanilla's overworld branch and play no cut-in at all. So the shiny cut-in shows on
// Surf / Waterfall / Whirlpool / Rock Climb always, and on Cut / Rock Smash / Strength only when
// Mr. Paint is not deployed.
#define MR_PAINT_ACTOR_PID 0x50615061u
_Static_assert(SHINY_CHECK(0, MR_PAINT_ACTOR_PID), "the Mr. Paint cut-in actor must be shiny");

static struct PartyPokemon *MrPaintActorMon(void)
{
    ZeroMonData(&sMrPaintActor);
    PokeParaSet(&sMrPaintActor, SPECIES_SMEARGLE, 5, 31, TRUE, MR_PAINT_ACTOR_PID, TRUE, 0);
    SetMonData(&sMrPaintActor, MON_DATA_NICKNAME, (void *)sMrPaintActorNickname);
    return &sMrPaintActor;
}

// Side feature 0.4.33 "Teleport trick" (james-game docs/mr-paint-route29-handover.md section 3,
// docs/mr-paint-trick-menu.md). Minimal mirror of the two words retail's own
// FieldMove_CheckTeleport (rom.ld) actually reads out of its checkData argument - offset 0
// (mapId) and offset 4 (FieldSystem*) - disassembled from THIS ROM (see rom.ld's comment above
// the three new symbols this feature adds). Deliberately NOT the full retail struct: CheckTeleport
// never dereferences past offset 4 on the Teleport row, so nothing else needs modelling, and the
// offsets are asserted so a layout slip can never pass silently.
typedef struct MrPaintTeleportCheckData {
    u32 mapId;
    FieldSystem *fieldSystem;
} MrPaintTeleportCheckData;
_Static_assert(offsetof(MrPaintTeleportCheckData, mapId) == 0,
               "FieldMove_CheckTeleport reads mapId at checkData+0");
_Static_assert(offsetof(MrPaintTeleportCheckData, fieldSystem) == 4,
               "FieldMove_CheckTeleport reads fieldSystem at checkData+4");

extern u32 LONG_CALL FieldMove_CheckTeleport(MrPaintTeleportCheckData *checkData);
extern void *LONG_CALL FieldMoveTask_CreateTeleportEnvironment(FieldSystem *fieldSystem,
                                                                struct PartyPokemon *mon,
                                                                u32 partySlot, u32 heapId);
extern BOOL LONG_CALL Task_FieldTeleport(TaskManager *taskman);

// Retail's own FieldMove_UseTeleport passes 4 here (arm9 0x0206863E, `movs r3,#4`, immediately
// before its own call into FieldMoveTask_CreateTeleportEnvironment) - no named HEAP_ID_FIELD1
// constant exists yet anywhere in this tree, so the raw value is kept traceable with this comment
// instead of inventing one.
#define MR_PAINT_TELEPORT_HEAP_ID 4

// See include/mr_paint.h for the full contract. Calls retail's own FieldMove_CheckTeleport
// directly and forwards its return value unchanged (1 = NOT_HERE, 3 = HAVE_FOLLOWER, 0 = OK and
// the warp has started) rather than reimplementing any of its checks - the same "call real retail
// code" approach this file already uses for FieldMoveTask_CreateTeleportEnvironment/
// Task_FieldTeleport below and MrPaintNurseRecall's swap in src/mr_paint_follower.c.
u16 MrPaintTeleport(FieldSystem *fieldSystem)
{
    MrPaintTeleportCheckData checkData;
    u32 checkResult;
    void *env;

    checkData.mapId = (u32)fieldSystem->location->mapId;
    checkData.fieldSystem = fieldSystem;

    checkResult = FieldMove_CheckTeleport(&checkData);
    if (checkResult != 0) {
        return (u16)checkResult;
    }

    env = FieldMoveTask_CreateTeleportEnvironment(fieldSystem, MrPaintActorMon(),
                                                   MR_PAINT_ACTOR_SENTINEL_SLOT,
                                                   MR_PAINT_TELEPORT_HEAP_ID);
    TaskManager_Call((TaskManager *)fieldSystem->taskman, Task_FieldTeleport, env);
    return 0;
}

// Side feature "Surf gate" (james-0417-feedback.md sections 4/4b). Full-function hook (hg-engine
// `hooks`: "arm9 GetIdxOfFirstPartyMonWithMove 020542E8 2") replacing retail
// GetIdxOfFirstPartyMonWithMove at 0x020542E8. Disassembled from THIS ROM's control build
// (0.4.21) before writing this: 0x020542E8-0x02054355 (110 bytes) is
//   push {r3,r4,r5,r6,r7,lr}; bl PokeParty_GetPokeCount(0x2074640);
//   loop: bl Party_GetMonByIndex(0x2074644); GetMonData(mon,#76/*IS_EGG*/) skip-if-true;
//         GetMonData(mon,#54/*MOVE1*/)==move? / #55/*MOVE2*/ / #56/*MOVE3*/ / #57/*MOVE4*/ -> found;
//   not found: movs r0,#255; pop.
// Exactly the vanilla loop this function reproduces below - not-found is 0xFF, not the 6 the
// ScrCmd_GetPartySlotWithMove wrapper below writes into its own destVar (that 6 is that OTHER
// function's convention, unrelated to this one).
//
// ABI note - a DEVIATION from the frozen design's literal hooks line, caught by disassembling
// before hooking per the project's own hard rule: the design's draft line used register "1", copied
// from the ScrCmd_GetPartySlotWithMove line below without adjusting for a different signature. This
// function takes TWO arguments (struct Party *party in r0, u16 move in r1) - the register the
// hg-engine `hooks` mechanism clobbers as scratch for its "ldr rN,[pc,#0]; bx rN" trampoline (see
// scripts/make.py Hook(), the `register != 0xFF` branch) must be a register NOT carrying a live
// incoming argument, i.e. r2 (or r3), never r1: this function's convention already reserves r1 for
// the "move" argument, matching MonTryLearnMoveOnLevelUp's own hooks line ("...3", a 3-arg function
// hooked with the first free register r3) and ScrCmd_GetPartySlotWithMove's ("...1", a 1-arg
// ScrCmd_ function whose only argument is r0=ctx, so r1 is free). Using "1" here would have the
// trampoline overwrite r1 with the jump target BEFORE this function's own prologue ever reads
// "move" out of it, corrupting every call. The shipped `hooks` line for this function uses register
// 2, matching the convention exactly (2 live argument registers -> first free register is r2).
int GetIdxOfFirstPartyMonWithMove(struct Party *party, u16 move)
{
    int partyCount = PokeParty_GetPokeCount(party);
    int i;

    for (i = 0; i < partyCount; i++) {
        // Re-derive nothing extra here: unlike ScrCmd_GetPartySlotWithMove, retail's own
        // disassembly for THIS function re-reads Party_GetMonByIndex(party, i) fresh off the
        // caller-supplied `party` pointer every iteration too (there is no FieldSystem here to
        // re-derive it from) - reproduced identically.
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);

        if (GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }

        if (GetMonData(mon, MON_DATA_MOVE1, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE2, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE3, NULL) == move
            || GetMonData(mon, MON_DATA_MOVE4, NULL) == move) {
            return i;
        }
    }

    // Vanilla found nothing. Mr. Paint fallback: only for a move he can perform AND has already
    // "learned" (its reserved flag set) - reuses gMrPaintMoveEntries[]/MrPaintLearnableFlagForMove
    // untouched, no new flag, no new data. Deliberately narrower than the opcode-141 hook: no
    // Bag_HasItem check (ITEM_MR_PAINT is prevent_toss, so "learned" already implies "held" for the
    // life of the save - data/itemdata/itemdata.c) and no deployed/follower check (this hook only
    // answers "does the prompt exist at all"; who performs the move and what the box says is
    // decided downstream, entirely by the already-shipped opcode-141/183/199 hooks above).
    // CheckScriptFlag(u16) takes no FieldSystem/SaveData argument (include/save.h - it reads
    // SaveBlock2_get() internally), so it is reachable here with only the bare Party* this function
    // is handed.
    if (MrPaintLearnableFlagForMove(move) != 0 && CheckScriptFlag(MrPaintLearnableFlagForMove(move))) {
        // Any index != 0xFF works: both known callers (field_control.c's Surf/Waterfall checks,
        // confirmed by exhaustive BL/BLX scan - see james-0417-feedback.md section 4b) test the
        // result only as a found/not-found boolean, never dereference it as a party slot.
        return 0;
    }

    return 0xFF;
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

    sMrPaintActorActive = 0;

    // Slice 0.4.14: the gate is no longer conditional on the search having failed. The client
    // reported the one case it used to skip - Mr. Paint deployed AND a party Pokemon also knows
    // the move - which fell straight through to vanilla and named the wrong Pokemon.
    if (partyCount > 0) {
        // MrPaintLearnableFlagForMove, not MrPaintFlagForMove: observably identical (Fly, Flash
        // and Dig have flags that are never set, so CheckScriptFlag already rejected them) but it
        // makes it structurally impossible for the three cut moves to reach this code.
        u16 flag = MrPaintLearnableFlagForMove(move);

        if (move == MOVE_HEADBUTT) {
            // 0.4.21: 2218, set only by the Ilex Forest tutor (0.4.8). Deliberately no
            // gMrPaintMoveEntries[] row - Headbutt has no machine, so nothing must ever derive
            // a message index for it (MrPaintMessageIndexForMove is untouched and stays 10-row).
            flag = 0x8AA;
        }

        if (flag != 0 && CheckScriptFlag(flag)) {
            BAG_DATA *bag = Sav2_Bag_get(fieldSystem->savedata);

            if (Bag_HasItem(bag, ITEM_MR_PAINT, 1, HEAPID_WORLD)) {
                int followerSlot = MrPaintDeployedFollowerSlot(fieldSystem);

                if (followerSlot >= 0) {
                    // DEPLOYED. The Smeargle walking behind the player IS Mr. Paint, so he is the
                    // performer in name and in fact, whether or not a party Pokemon also knows the
                    // move. Requested verbatim by the client: "If Mr. Paint is active, the text
                    // should ALWAYS attribute the move to Mr. Paint, and Mr. Paint should ALWAYS
                    // perform the animation."
                    sMrPaintActorActive = 1;

                    if (MrPaintMoveHasFollowerBranch(move)) {
                        // Slice 0.4.7, now applied UNCONDITIONALLY rather than only when nothing
                        // was found. Matching the follower's own slot makes script 146's
                        // `CompareVars 0x8004 0x8005` come out EQUAL, which routes Cut / Rock
                        // Smash / Strength down vanilla's own overworld branch - the follower
                        // performs the move where it stands, with no cut-in. Doing it here as well
                        // as in the not-found case is what closes the hole: the client's "the
                        // animation is already correct" only held while the knower happened to be
                        // the party lead, i.e. the follower itself. A non-lead knower used to make
                        // the compare DIFFERENT and play a cut-in of the wrong Pokemon.
                        *destVar = (u16)followerSlot;
                    } else {
                        // Surf / Waterfall / Whirlpool / Rock Climb have no follower branch at all,
                        // so their performer is the cut-in actor - substitute it, or the box would
                        // say "Mr. Paint" over somebody else's animation. Both actor hooks skip the
                        // party lookup while substituting, so leaving *destVar on the real knower's
                        // slot is safe; it is never dereferenced.
                        if (*destVar == MR_PAINT_SLOT_NOT_FOUND) {
                            *destVar = 0;
                        }
                    }
                } else if (*destVar == MR_PAINT_SLOT_NOT_FOUND) {
                    // NOT deployed and nobody knows the move: exactly 0.4.6 behaviour - the static
                    // cut-in actor, reached through the 0.3.8 sentinel for the three moves whose
                    // follower-branch flow would otherwise mistake slot 0 for the follower's.
                    //
                    // 0.4.29 "water cut-in" widens this to Surf/Waterfall/Whirlpool as well, for a
                    // DIFFERENT reason: those three have no follower branch to mistake anything for
                    // (MrPaintMoveHasFollowerBranch is false), but until now they had no cut-in
                    // gate at all - script 146 Functions 13/15/27 ran straight to
                    // Surf/Waterfall/WhirlpoolAnimation with no branch, so nobody-knows-it played no
                    // Mr. Paint visual whatsoever. The same out-of-range sentinel is reused here
                    // purely as the signal those three functions' new `CompareVarValue 0x8004 7`
                    // line tests before calling the shared opcode-183 cut-in - it still relies on
                    // MrPaintFieldMoveActorMon (ov01_021F3100) never dereferencing it as a party
                    // index while sMrPaintActorActive is set, exactly like the follower-branch case.
                    // Rock Climb is deliberately excluded (not in MrPaintMoveNeedsWaterCutIn) - the
                    // frozen design scoped this to Surf/Waterfall/Whirlpool only.
                    *destVar = (MrPaintMoveHasFollowerBranch(move) || MrPaintMoveNeedsWaterCutIn(move))
                        ? MR_PAINT_ACTOR_SENTINEL_SLOT : 0;
                    sMrPaintActorActive = 1;
                }
            }
        }
    }

    return FALSE;
}

// Slice 0.3.3 "Smeargle actor". Four hooks that make the cutscene actor and the
// "<name> used X!" message read Mr. Paint / SMEARGLE instead of the real lead
// mon, whenever sMrPaintActorActive says the 0.3.2 hook just fell back to the
// item. Faithful reimplementations of the vanilla bodies, each with one
// substitution, in the same call order as the shipped disassembly.

extern u32 LONG_CALL PlayerAvatar_GetGender(void *playerAvatar);
extern void *LONG_CALL ov02_02249458(FieldSystem *fsys, int a1, struct PartyPokemon *mon, int gender);
extern void LONG_CALL SetupNativeScript(SCRIPTCONTEXT *ctx, ScrCmdFunc ptr);
extern void LONG_CALL StopScript(SCRIPTCONTEXT *ctx);

#define MR_PAINT_SCRCMD183_CALLBACK ((ScrCmdFunc)0x0204378D)

// Replaces retail ScrCmd_183 (0x02043724) - the Cut/RockSmash/Headbutt/Strength/Flash
// cutscene actor setup. MUST return TRUE: the script yields on the native callback.
BOOL ScrCmd_183(SCRIPTCONTEXT *ctx)
{
    void **pWork = (void **)FieldSysGetAttrAddr(ctx->fsys, SCRIPTENV_GENERIC_WORK_PTR);
    u16 partyIdx = ScriptGetVar(ctx);
    struct PartyPokemon *mon;
    u32 gender;

    // Slice 0.3.8: look the slot up ONLY when we are not going to replace the result anyway.
    // 0.3.3 did the lookup first and then overwrote `mon`, which was harmless while the 0.3.2
    // hook returned slot 0 but would hand Party_GetMonByIndex the out-of-range sentinel now.
    // The script stream is still read in the same order - ScriptGetVar above is untouched.
    if (sMrPaintActorActive) {
        mon = MrPaintActorMon();
    } else {
        mon = Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(ctx->fsys->savedata), partyIdx);
    }

    gender = PlayerAvatar_GetGender(ctx->fsys->playerAvatar);
    *pWork = ov02_02249458(ctx->fsys, 0, mon, gender);
    SetupNativeScript(ctx, MR_PAINT_SCRCMD183_CALLBACK);
    return TRUE;
}

// Replaces retail ScrCmd_BufferPartyMonNick (0x020486F0) - the "<name> used X!"
// name buffer. MUST return FALSE: synchronous, no wait state.
BOOL ScrCmd_BufferPartyMonNick(SCRIPTCONTEXT *ctx)
{
    FieldSystem *fieldSystem = ctx->fsys;
    void **msgFmt = (void **)FieldSysGetAttrAddr(fieldSystem, SCRIPTENV_MSGFMT);
    u8 idx = ScriptReadByte(ctx);          // must be read BEFORE the var - keep separate statements
    u16 partyMonIdx = ScriptGetVar(ctx);
    struct PartyPokemon *mon;

    // Slice 0.3.8 - same reordering as ScrCmd_183 above, and for the same reason. The byte and
    // the var are still read first, in that order, so the script stream is consumed identically.
    //
    // Slice 0.4.14 widens WHEN sMrPaintActorActive is set (see the opcode-141 hook), not what it
    // means here: Mr. Paint is this move's performer, so he is the name on the box.
    if (sMrPaintActorActive) {
        mon = MrPaintActorMon();
    } else {
        mon = Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(fieldSystem->savedata), partyMonIdx);
    }

    BufferBoxMonNickname((MessageFormat *)*msgFmt, idx, (struct BoxPokemon *)mon);
    return FALSE;
}

// Replaces overlay 1's ov01_021F3100 (0x021F3100) - the actor resolver the
// Surf / Waterfall / Whirlpool / Rock Climb field tasks all funnel through.
struct PartyPokemon *MrPaintFieldMoveActorMon(FieldSystem *fieldSystem, u32 partyIdx)
{
    // Slice 0.3.8 - reordered for consistency with the two hooks above. These four moves keep
    // slot 0 (MrPaintMoveHasFollowerBranch excludes them), so this one can never see the
    // sentinel; the lookup is skipped anyway rather than leaving one ordering different.
    if (sMrPaintActorActive) {
        return MrPaintActorMon();
    }
    return Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(fieldSystem->savedata), partyIdx);
}

// Replaces ScrCmd_End (opcode 2, 0x02040898). The ONLY reason it exists is to
// make a stale actor flag impossible: if the player cancels the "Use CUT?"
// prompt, nothing else consumes the flag, so it is cleared here whenever the
// script terminates. Vanilla body is StopScript(ctx) + return FALSE, reproduced
// exactly plus the one clear.
BOOL ScrCmd_End(SCRIPTCONTEXT *ctx)
{
    sMrPaintActorActive = 0;
    // 0.4.17 backstop: the 711 handler clears this itself when MrPaintTalkTask's wrapped
    // Task_FollowMonInteract returns TRUE, but a script that ends by some other path (e.g. the
    // player forcing a map change mid-conversation, if that is ever possible) must not leave it
    // stuck set, since it would otherwise misattribute a LATER, unrelated
    // GetFirstAliveMonInParty_CrashIfNone call for the same FieldSystem.
    sMrPaintTalkActive = 0;
    sMrPaintTalkFieldSystem = NULL;
    StopScript(ctx);
    return FALSE;
}

// Slice 0.3.7 "deferred inspiration". Bug 2a: the 0.3.1 build called
// _MrPaintShowInspiration from inside the shared give-item routine, which
// returns to the giver BEFORE the giver's own closing dialogue - so the
// congratulation cut into the conversation. Both inline calls are gone; the
// prompt is now a common script of its own, started from here.
//
// The trigger point is the vanilla player-step event (hooked in src/repel.c),
// which by construction only runs when the player completes a step in the
// overworld under their own control - never during a battle, a cutscene, a map
// transition, or while a script holds the player. That structural guarantee is
// the whole safety argument; there is no state here to go stale. Vanilla itself
// starts a script from this callback ("REPEL's effect wore off", 2022), and
// hg-engine already ships scr_seq_0003_072_repels through it the same way.
//
// This only ever PEEKS at the var - opcode 208 subcommand 1 is what clears it
// (src/script_new_cmds.c), so the prompt fires at most once per HM learned.
BOOL MrPaintTryQueueInspiration(FieldSystem *fieldSystem)
{
    if (fieldSystem == NULL) {
        return FALSE;
    }

    if (GetScriptVar(MR_PAINT_PENDING_ITEM_VAR) == 0) {
        return FALSE;
    }

    EventSet_Script(fieldSystem, MR_PAINT_INSPIRATION_SCRIPT, NULL);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Side feature 0.4.17 "follower talk"
// ---------------------------------------------------------------------------------------------
//
// Talking to the deployed Mr. Paint follower (script opcode 711, ROM's TalkFollowingPoke /
// ScrCmd_FollowMonInteract) should run vanilla's own follower-talk task, but every read of "the
// lead" inside it must see the static shiny Smeargle actor (MrPaintActorMon() above) with
// friendship 255 - never the real party, which is neither read nor written for this.
//
// Disassembled from THIS ROM (arm9.bin / ov002.bin):
//   - ScrCmd_FollowMonInteract, the opcode-711 handler, is at arm9 0x02047414 - located by
//     finding gScriptCmdTable's opcode-141 entry (0204D3CD, the address ScrCmd_GetPartySlotWithMove
//     already hooks above) and reading forward to slot 711. Its whole body is
//     `FieldSystem_FollowMonInteract(ctx->fsys); return TRUE;` - a tail call into ov2 0x0224EF80,
//     itself `TaskManager_Call(fsys->taskman, Task_FollowMonInteract, NULL)`
//     (Task_FollowMonInteract = ov2 0x02250111).
//   - Task_FollowMonInteract (ov2 0x02250110-0x02250482, 882 bytes) was disassembled in full: it
//     is an ordinary state-machine TaskFunc returning BOOL(done), with NO call to
//     TaskManager_Call/_Jump anywhere in its body and no PC-relative load of its own address - so
//     it never reschedules or re-enters itself, and calling it directly from our own TaskFunc and
//     forwarding its return value is exactly what TaskManager_Call's normal dispatch does. Its one
//     call to SaveData_GetPlayerPartyPtr (0x02074904) is immediately followed by a call to
//     0x02054388 (ov2 0x022503BC/0x022503C0) - GetFirstAliveMonInParty_CrashIfNone, "the lead".
//   - GetFirstAliveMonInParty_CrashIfNone (arm9 0x02054388) itself calls PokeParty_GetPokeCount,
//     Party_GetMonByIndex and a single-argument aliveness test at 0x020541B0
//     (RetailPartyMonAliveTest, rom.ld) in a loop, crashing via GF_ASSERT_INTERNAL (0x0202551C,
//     already declared in include/types.h) if none qualifies - reproduced exactly below for the
//     not-latched path.

extern void LONG_CALL FieldSystem_FollowMonInteract(FieldSystem *fieldSystem);
extern u32 LONG_CALL RetailPartyMonAliveTest(struct PartyPokemon *mon);

// Task_FollowMonInteract, ov2 0x02250111 (thumb+1). Not a named retail symbol anywhere in this
// project; used purely as a TaskFunc pointer value passed to TaskManager_Call, so it needs no
// rom.ld entry of its own.
#define TASK_FOLLOW_MON_INTERACT ((TaskFunc)0x02250111)

// sMrPaintTalkActive / sMrPaintTalkFieldSystem (declared near sMrPaintActorActive above, so
// ScrCmd_End's backstop clear can reach them) are set for the duration of one MrPaintTalkTask run
// (opcode 711 -> ScrCmd_End at the latest), so GetFirstAliveMonInParty_CrashIfNone below knows to
// substitute. sMrPaintTalkFieldSystem scopes the substitution to the exact FieldSystem/party the
// 711 handler was invoked for, so an unrelated caller of the same retail function (14+ call sites
// project-wide) that happened to run while the latch is set can never be affected. Overlay-129
// BSS, proven zero at boot like sMrPaintActorActive.

static BOOL MrPaintTalkTask(TaskManager *taskman)
{
    BOOL done = TASK_FOLLOW_MON_INTERACT(taskman);

    if (done) {
        sMrPaintTalkActive = 0;
        sMrPaintTalkFieldSystem = NULL;
    }

    return done;
}

// Replaces retail ScrCmd_FollowMonInteract (opcode 711, 0x02047414). Not deployed: vanilla,
// byte-for-byte. Deployed: same TaskManager_Call retail makes, wrapped in MrPaintTalkTask so the
// talk latch tracks exactly one task's lifetime.
BOOL ScrCmd_FollowMonInteract(SCRIPTCONTEXT *ctx)
{
    FieldSystem *fieldSystem = ctx->fsys;

    if (MrPaintDeployedFollowerSlot(fieldSystem) < 0) {
        FieldSystem_FollowMonInteract(fieldSystem);
        return TRUE;
    }

    sMrPaintTalkActive = 1;
    sMrPaintTalkFieldSystem = fieldSystem;
    TaskManager_Call((TaskManager *)fieldSystem->taskman, MrPaintTalkTask, NULL);
    return TRUE;
}

// MrPaintActorMon() (above) plus friendship forced to 255 - the one extra field the follower-talk
// task reads that the cutscene actor never did (dialogue tier and the friendship bump both key off
// it). Rebuilt on every call like MrPaintActorMon() itself; nothing here is persisted.
static struct PartyPokemon *MrPaintTalkMon(void)
{
    struct PartyPokemon *mon = MrPaintActorMon();
    u8 friendship = 255;
    SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
    return mon;
}

// Replaces retail GetFirstAliveMonInParty_CrashIfNone (arm9 0x02054388). While the 0.4.17 talk
// latch is active for THIS party, returns the static Mr. Paint actor instead of searching; every
// other caller, and every path once the latch is clear, gets retail's own search reproduced
// exactly, including the crash-if-none-alive fallback (0x0202551C).
struct PartyPokemon *GetFirstAliveMonInParty_CrashIfNone(struct Party *party)
{
    int count;
    int i;

    if (sMrPaintTalkActive && sMrPaintTalkFieldSystem != NULL
        && party == SaveData_GetPlayerPartyPtr(sMrPaintTalkFieldSystem->savedata)) {
        return MrPaintTalkMon();
    }

    count = PokeParty_GetPokeCount(party);

    for (i = 0; i < count; i++) {
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);

        if (RetailPartyMonAliveTest(mon)) {
            return mon;
        }
    }

    GF_ASSERT_INTERNAL();
    return NULL;
}
