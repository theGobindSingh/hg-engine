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

// Slice 0.3.8. The three obstacle moves whose ROM script 146 flow compares the actor slot
// against the follower's slot - Cut (Function 48), Rock Smash (Function 50), Strength
// (Function 65) - and which therefore need the sentinel to force the non-follower branch.
//
// Headbutt has the same shape at Function 60 but is unreachable: its flag 0x8AA is never set.
// Surf, Waterfall, Whirlpool and Rock Climb have no follower branch and no CompareVars at all
// (their flows are terminal: CheckMoveInParty / copy / TextPokeNickname / <Move>Animation /
// Jump Function#11), so they keep slot 0 and behave byte-for-byte as they shipped in 0.3.7 -
// which is what the client's note about those moves requires.
static BOOL MrPaintMoveHasFollowerBranch(u16 move)
{
    return move == MOVE_CUT || move == MOVE_ROCK_SMASH || move == MOVE_STRENGTH;
}

// Slice 0.3.3 "Smeargle actor". Set-or-cleared on EVERY call of the 0.3.2
// CheckMoveInParty hook, read (never cleared) by the three actor hooks, and
// force-cleared whenever any script ends - see ScrCmd_End below. Overlay-129
// BSS, proven zero at boot, so this fails safe to vanilla.
static u8 sMrPaintActorActive;

// Slice 0.4.14 "attribution". A SECOND, independent latch, set/cleared in exactly
// the same two places as sMrPaintActorActive above, and read only by
// ScrCmd_BufferPartyMonNick. It says "name this move's performer Mr. Paint",
// without saying anything about which mon ANIMATES it - the two concerns must stay
// decoupled, because while Mr. Paint is deployed the follower branch performs the
// move with no cut-in at all, so widening sMrPaintActorActive would change the
// actor on paths the client has already signed off.
//
// It MUST be a per-invocation latch and never a live CheckScriptFlag read inside
// BufferPartyMonNick: opcode 199 is used by 17 different script files, most with
// nothing to do with Mr. Paint (the Day Care among them), so a live check would
// print "Mr. Paint" in unrelated dialogue for as long as flag 2224 is set. Opcode
// 141 runs only on the seven obstacle flows, which is what scopes the override.
static u8 sMrPaintNameOverride;

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
    sMrPaintNameOverride = 0;

    // Slice 0.4.14: the gate is no longer conditional on the search having failed. The client
    // reported the one case it used to skip - Mr. Paint deployed AND a party Pokemon also knows
    // the move - which fell straight through to vanilla and named the wrong Pokemon.
    if (partyCount > 0) {
        // MrPaintLearnableFlagForMove, not MrPaintFlagForMove: observably identical (Fly, Flash
        // and Dig have flags that are never set, so CheckScriptFlag already rejected them) but it
        // makes it structurally impossible for the three cut moves to reach this code.
        u16 flag = MrPaintLearnableFlagForMove(move);

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
                    sMrPaintNameOverride = 1;

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
                        sMrPaintActorActive = 1;
                        if (*destVar == MR_PAINT_SLOT_NOT_FOUND) {
                            *destVar = 0;
                        }
                    }
                } else if (*destVar == MR_PAINT_SLOT_NOT_FOUND) {
                    // NOT deployed and nobody knows the move: exactly 0.4.6 behaviour - the static
                    // cut-in actor, reached through the 0.3.8 sentinel for the three moves whose
                    // flow would otherwise mistake slot 0 for the follower's.
                    *destVar = MrPaintMoveHasFollowerBranch(move) ? MR_PAINT_ACTOR_SENTINEL_SLOT : 0;
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
    // Slice 0.4.14 adds the second latch: sMrPaintActorActive means "Mr. Paint is the cut-in
    // actor", sMrPaintNameOverride means "Mr. Paint is deployed and performs this move himself".
    // Either one names him.
    if (sMrPaintActorActive || sMrPaintNameOverride) {
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
    sMrPaintNameOverride = 0;   // 0.4.14: the name latch has the same staleness risk, so the same cure
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
