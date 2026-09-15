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

// Slice 0.3.3 "Smeargle actor". Set-or-cleared on EVERY call of the 0.3.2
// CheckMoveInParty hook, read (never cleared) by the three actor hooks, and
// force-cleared whenever any script ends - see ScrCmd_End below. Overlay-129
// BSS, proven zero at boot, so this fails safe to vanilla.
static u8 sMrPaintActorActive;

// The stand-in actor. Rebuilt deterministically on every use, so nothing
// depends on lazy-init state. 0xEC (236) bytes of overlay-129 BSS.
static struct PartyPokemon sMrPaintActor;

// "Mr. Paint" in the HGSS charmap (charmap.txt: M=0137 r=0156 .=01AE
// space=01DE P=013A a=0145 i=014D n=0152 t=0158), 0xFFFF-terminated.
// 9 characters; the nickname field holds 11.
static const u16 sMrPaintActorNickname[] = {
    0x0137, 0x0156, 0x01AE, 0x01DE, 0x013A, 0x0145, 0x014D, 0x0152, 0x0158, 0xFFFF
};

// Fixed PID ('MrPa'): deterministic, and non-shiny against the forced OT id 0.
#define MR_PAINT_ACTOR_PID 0x4D725061u

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
    if (*destVar == MR_PAINT_SLOT_NOT_FOUND && partyCount > 0) {
        u16 flag = MrPaintFlagForMove(move);

        if (flag != 0 && CheckScriptFlag(flag)) {
            BAG_DATA *bag = Sav2_Bag_get(fieldSystem->savedata);

            if (Bag_HasItem(bag, ITEM_MR_PAINT, 1, HEAPID_WORLD)) {
                *destVar = 0;
                sMrPaintActorActive = 1;
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
    struct PartyPokemon *mon =
        Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(ctx->fsys->savedata), partyIdx);
    u32 gender;

    if (sMrPaintActorActive) {
        mon = MrPaintActorMon();
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
    struct PartyPokemon *mon =
        Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(fieldSystem->savedata), partyMonIdx);

    if (sMrPaintActorActive) {
        mon = MrPaintActorMon();
    }

    BufferBoxMonNickname((MessageFormat *)*msgFmt, idx, (struct BoxPokemon *)mon);
    return FALSE;
}

// Replaces overlay 1's ov01_021F3100 (0x021F3100) - the actor resolver the
// Surf / Waterfall / Whirlpool / Rock Climb field tasks all funnel through.
struct PartyPokemon *MrPaintFieldMoveActorMon(FieldSystem *fieldSystem, u32 partyIdx)
{
    struct PartyPokemon *mon =
        Party_GetMonByIndex(SaveData_GetPlayerPartyPtr(fieldSystem->savedata), partyIdx);

    if (sMrPaintActorActive) {
        mon = MrPaintActorMon();
    }
    return mon;
}

// Replaces ScrCmd_End (opcode 2, 0x02040898). The ONLY reason it exists is to
// make a stale actor flag impossible: if the player cancels the "Use CUT?"
// prompt, nothing else consumes the flag, so it is cleared here whenever the
// script terminates. Vanilla body is StopScript(ctx) + return FALSE, reproduced
// exactly plus the one clear.
BOOL ScrCmd_End(SCRIPTCONTEXT *ctx)
{
    sMrPaintActorActive = 0;
    StopScript(ctx);
    return FALSE;
}
