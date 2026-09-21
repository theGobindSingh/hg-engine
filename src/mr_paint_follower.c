#include "../include/mr_paint.h"

#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/constants/species.h"
#include "../include/types.h"

#include "../include/bag.h"
#include "../include/item.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"

// PokeParty_GetPokeCount (rom.ld:282, 0x02074640) is linked but not declared in any header -
// follow the LONG_CALL convention of its neighbour Party_GetMonByIndex (include/pokemon.h:930),
// exactly as src/mr_paint.c:17 already does.
extern int LONG_CALL PokeParty_GetPokeCount(struct Party *party);

// Side feature 0.4.3 "Overworld Mr. Paint Companion Deployment".
//
// Using the Mr. Paint key item from the Bag flips FLAG_MR_PAINT_FOLLOWING; while it is set, the
// walking follower is DRAWN as Smeargle instead of the party lead, and it stays that way through
// doors, caves and warps. The party is never touched.
//
// ---------------------------------------------------------------------------------------------
// Where the substitution actually happens, and why it is only two places
// ---------------------------------------------------------------------------------------------
//
// Retail decides the follower's identity in three separate consumers, all handed the SAME local
// species value by FollowMon_ChangeMon / FollowMon_InitMapObject. None of the later two reads the
// earlier one back, so writing only the cache would change internal state and nothing on screen:
//
//   FollowPokeFsysParamSet        0x02069F3C   the cached identity (FieldSystem->followMon)
//   FollowPokeMapObjectSetParams  0x02069EE8   the map object's species/form/shiny params
//   FollowingPokemon_GetSpriteID  0x02069D70   ** what is actually drawn **
//
// The third is ALREADY OURS. Upstream hg-engine replaces it with get_mon_ow_tag (src/pokemon.c,
// `hooks`: "arm9 get_mon_ow_tag 08069D70 3") in every build this project has ever shipped - the
// hook line writes the address in the 0x08 form, which is why grepping the `hooks` file for
// "02069D70" finds nothing. So the visible half of this feature needs no new hook and no
// reproduced retail body; it is three lines inside get_mon_ow_tag.
//
// That leaves the cache. It is hooked here so the game's own live state agrees with the screen,
// which is also the only thing the headless harness can assert (the follower species is a single
// u32 at [gFieldSysPtr] + 0xF4). The middle consumer - the map object's species param - is
// deliberately NOT hooked: reproducing it would mean two more inferred call targets for a value
// only the follower-interaction task reads, and that task re-derives the party lead on its own
// anyway (a known, accepted limitation - talking to Mr. Paint cries and speaks as the real lead).
//
// ---------------------------------------------------------------------------------------------
// Why the gate is a species match and NOT followMon.active
// ---------------------------------------------------------------------------------------------
//
// get_mon_ow_tag is a plain species -> overworld-tag mapper. It is not follower-specific: it is
// also reached from grab_overworld_a081_index (src/field/overworld_table.c, "used for HoF/
// pokeathlon overworlds") and from one non-follower arm9 caller at 0x0204CF10. Substituting
// unconditionally would turn those into Smeargle too.
//
// The obvious narrowing - "only while a follower is active" - does NOT work, and this was checked
// against the retail disassembly rather than assumed. On the map-load path
// (FollowMon_InitMapObject, 0x020699F8) the order is:
//
//     CreateFollowingSpriteFieldObject  ->  GetSpriteID ... then SetObjectParams
//     FieldSystem+0xFA (active) = 1
//     FollowPokeFsysParamSet
//
// i.e. the sprite is chosen BEFORE `active` is set and before the cache is written. A gate reading
// either of those would fail on exactly the path that makes the feature persist across doors.
//
// Matching the requested species against the follower's REAL species is order-independent - it
// depends on nothing the follower chain has done yet - and it keeps the blast radius to "the one
// species that is currently the player's follower".
static u16 MrPaintFollowerBaseSpecies(FieldSystem *fieldSystem)
{
    struct Party *party = SaveData_GetPlayerPartyPtr(fieldSystem->savedata);
    int partyCount = PokeParty_GetPokeCount(party);
    int i;

    // The follower is the first ALIVE party member, not slot 0 - retail resolves it with
    // GetFirstAliveMonInParty_CrashIfNone. Mirror that, so a fainted lead does not make the
    // substitution miss the Pokemon that is really walking behind the player.
    for (i = 0; i < partyCount; i++) {
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);

        if (GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
            continue;
        }
        if (GetMonData(mon, MON_DATA_HP, NULL) == 0) {
            continue;
        }
        return (u16)GetMonData(mon, MON_DATA_SPECIES, NULL);
    }

    return SPECIES_NONE;
}

BOOL MrPaintFollowerSubstitutes(u16 species)
{
    FieldSystem *fieldSystem = gFieldSysPtr;
    BAG_DATA *bag;

    // get_mon_ow_tag is reached from contexts that have no field system at all. Fail safe to
    // vanilla rather than dereferencing.
    if (fieldSystem == NULL || fieldSystem->savedata == NULL) {
        return FALSE;
    }

    if (!CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        return FALSE;
    }

    bag = Sav2_Bag_get(fieldSystem->savedata);
    if (!Bag_HasItem(bag, ITEM_MR_PAINT, 1, HEAPID_WORLD)) {
        return FALSE;
    }

    // Nothing to do if the follower is already a Smeargle - and this also stops the substitution
    // recursing into itself conceptually.
    if (species == SPECIES_SMEARGLE) {
        return FALSE;
    }

    return species == MrPaintFollowerBaseSpecies(fieldSystem);
}

// Full-function hook replacing retail FollowPokeFsysParamSet (0x02069F3C).
//
// The retail body is a leaf - `push {r3,r4}`, four stores, `pop`, `bx lr`, 0x20 bytes, ending at
// 0x02069F5C, with no calls at all - so this reproduction cannot get a call target wrong. The
// store order below matches the shipped disassembly: species (+0xF4), shiny (+0xFB), forme
// (+0xFC), gender (+0xF8). Those four offsets are themselves the independent confirmation that
// followMon sits at FieldSystem+0xE4 with species at +0x10 (include/pokemon.h:578-613).
//
// Hooked WITHOUT a scratch-register column. That is required, not stylistic: with a register N the
// build writes `ldr rN,[pc,#0]; bx rN` over the entry, destroying rN before our C runs, and this
// function takes five arguments - r0-r3 are all live and r4-r7 are callee-saved, so no register is
// free. The register-free form preserves every register (it saves lr into a word at entry+0x18 and
// `bl`s here), needs 0x1C bytes of entry space - the body is 0x20 - and is what upstream already
// uses for 4-argument hooks such as `arm9 Bag_AddItem 02078398`.
// The four offsets above are a CONTRACT with retail, not a comment. A reproduction that stores the
// right values at the wrong addresses is worse than no hook at all: it silently corrupts whatever
// really lives there. This is not hypothetical - it is what shipped in 0.4.3, because FieldSystem
// carried an s64 member whose 8-byte alignment padding moved followMon from 0xE4 to 0xE8, so every
// store landed four bytes high and the species write zeroed retail's `active` byte at +0xFA. Assert
// the layout at build time so it can never drift silently again.
#define MR_PAINT_FOLLOWMON_OFS(field) \
    (__builtin_offsetof(FieldSystem, followMon) + __builtin_offsetof(FollowMon, field))
_Static_assert(MR_PAINT_FOLLOWMON_OFS(species) == 0xF4, "followMon.species must sit at FieldSystem+0xF4");
_Static_assert(MR_PAINT_FOLLOWMON_OFS(gender) == 0xF8, "followMon.gender must sit at FieldSystem+0xF8");
_Static_assert(MR_PAINT_FOLLOWMON_OFS(active) == 0xFA, "followMon.active must sit at FieldSystem+0xFA");
_Static_assert(MR_PAINT_FOLLOWMON_OFS(shiny) == 0xFB, "followMon.shiny must sit at FieldSystem+0xFB");
_Static_assert(MR_PAINT_FOLLOWMON_OFS(forme) == 0xFC, "followMon.forme must sit at FieldSystem+0xFC");

void MrPaintFollowPokeFsysParamSet(FieldSystem *fieldSystem, int species, u8 forme, BOOL shiny, u8 gender)
{
    if (MrPaintFollowerSubstitutes((u16)species)) {
        species = SPECIES_SMEARGLE;
        forme = 0;
        shiny = FALSE;
    }

    fieldSystem->followMon.species = species;
    fieldSystem->followMon.shiny = shiny;
    fieldSystem->followMon.forme = forme;
    fieldSystem->followMon.gender = gender;
}

// The one place the flag actually flips. BOTH entry points below - the SELECT/field path and the
// 0.4.5 Bag/USE path - call this and nothing else, so they cannot drift apart the way two copies
// of the same three lines eventually would.
static void MrPaintToggleFollowingFlag(void)
{
    if (CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        ClearScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    } else {
        SetScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    }
}

// Entry point 1: the `field` column of sNewItemFieldUseFuncs[] row 6 (src/item.c), i.e. when
// Mr. Paint is REGISTERED TO SELECT and SELECT is pressed on the overworld. The item already
// ships .selectable = TRUE, so this needs nothing else.
//
// Shape copied from retail ItemFieldUseFunc_Bicycle (0x02064C30), the one attested example of a
// key item whose effect happens on the field with no sub-application: read what is needed out of
// `data`, act, and RETURN FALSE. TRUE is what the app-opening use functions return - returning it
// without having started an app is the soft-lock risk, so FALSE is the only correct value here.
// We deliberately do NOT copy the Bicycle's `fieldSystem[0xD2] |= 0x80`: that bit accompanies a
// field task that later clears it, and setting it without one is how a field lock gets stuck.
//
// Edge cases are all the same case, by design: this only ever flips a save flag. If no follower
// can exist right now - lead fainted, on a bike, surfing, or a map that forbids followers - the
// flag still flips and nothing else happens, and Mr. Paint appears when a follower legitimately
// next can. We never force a follower where vanilla would show none: retail's own permission check
// runs on the REAL species upstream of every substitution point, so it is untouched.
BOOL ItemFieldUseFunc_MrPaintToggle(struct ItemFieldUseData *data UNUSED)
{
    MrPaintToggleFollowingFlag();
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// Entry point 2 (0.4.5): the `menu` column - Bag -> USE
// ---------------------------------------------------------------------------------------------
//
// FROZEN by docs/mr-paint-follower.md Findings 21-22 in the james-game repo, transcribed from
// THIS ROM's own arm9 bytes, not inferred:
//
//   - ItemMenuUseFunc_Bicycle (0x02064BFC) and retail's ItemMenuUseFunc_EscapeRope/_Honey (pret
//     src/field_use_item.c) all write atexit_TaskFunc + atexit_TaskEnv=NULL + state=12. State 12
//     is retail's GENERAL "close the Bag, hand off to the field" mechanism - not a Bicycle-only
//     trick - and it fades the screen back IN before Task 13 does
//     `TaskManager_Jump(taskManager, exitTaskFunc, exitTaskEnvironment); Heap_Free(startMenu);`.
//     The Bag is destroyed before our TaskFunc runs, so a Bag wedge is structurally impossible.
//   - sub_0203C8F0 (state 5, WAIT_APP - what hg-engine's six existing ItemMenuUseFunc_* use) is
//     REJECTED: it keeps the Bag allocated and calls exitTaskFunc every frame until THAT func
//     drives state to RETURN itself, a shape with zero shipped precedent for a plain toggle. Do
//     not "simplify" this back to sub_0203C8F0 - it was tried, and disproven.
//   - A TaskManager_Jump target MAY return TRUE on its very first call: proven from pret
//     src/task.c - FieldSystem_RunTaskFrame's `while (taskman->func(taskman) == TRUE)` loop pops
//     and frees that taskman the same frame and continues on prevTask (the field task
//     underneath). Retail's own Task_JumpToFieldEscapeRope is exactly this shape: a one-line
//     relay that itself calls TaskManager_Jump again and returns FALSE - but our case needs no
//     further jump, so TRUE on the first call is correct and matches the one-shot pattern.
//
// atexit_TaskEnv MUST be explicitly zeroed: case 13 forwards it straight into TaskManager_Jump as
// the new task's environment, and unlike state 5, state 12's retail users always zero it.
//
// P7 Finding 26 (docs/mr-paint-follower.md): every retail state=12 writer calls
// FieldSystem_LoadFieldOverlay(fieldSystem) immediately before setting state. The case-12 handler
// (arm9 START_MENU_STATE_12) waits on sub_020505C8(fieldSystem), a bool wrapper around
// sub_0203DF8C ("processManager->parent != NULL && runningFieldMap") that only ever becomes true
// once FieldSystem_LoadFieldOverlay's async overlay load (ov01_02206378) finishes. Without the
// call, that predicate is never true and the state machine stalls in case 12 forever - a
// permanent black-screen stall indistinguishable from a crash, which is exactly what 0.4.5 showed
// byte-for-byte. rom.ld already names this address (FieldSystem_LoadFieldOverlay = 0x020505C0|1)
// but no C header had declared it; declared here, matching the existing LONG_CALL/THUMB_FUNC
// convention (include/item.h's ItemMenuUseFunc_* prototypes) rather than a raw-address call.
extern void LONG_CALL THUMB_FUNC FieldSystem_LoadFieldOverlay(FieldSystem *fieldSystem);

struct BagViewAppWork;
#define MR_PAINT_BAGVIEW_OFS(field) __builtin_offsetof(struct BagViewAppWork, field)
_Static_assert(MR_PAINT_BAGVIEW_OFS(state) == 0x26, "BagViewAppWork.state must sit at +0x26");
_Static_assert(MR_PAINT_BAGVIEW_OFS(atexit_TaskFunc) == 0x354, "BagViewAppWork.atexit_TaskFunc must sit at +0x354");
_Static_assert(MR_PAINT_BAGVIEW_OFS(atexit_TaskEnv) == 0x380, "BagViewAppWork.atexit_TaskEnv must sit at +0x380");

// The TaskFunc TaskManager_Jump hands control to. Flips the exact same flag as the field path,
// through the shared helper above, and completes in one frame.
BOOL Task_MrPaintToggle(TaskManager *taskman UNUSED)
{
    MrPaintToggleFollowingFlag();
    return TRUE;
}

void ItemMenuUseFunc_MrPaintToggle(struct ItemMenuUseData *data, const struct ItemCheckUseData *dat2 UNUSED)
{
    FieldSystem *fieldSystem = data->taskManager->fieldSystem; // TaskManager_GetFieldSystem(data->taskManager);
    struct BagViewAppWork *env = data->taskManager->env;

    env->atexit_TaskFunc = Task_MrPaintToggle;
    env->atexit_TaskEnv = NULL;
    FieldSystem_LoadFieldOverlay(fieldSystem);
    env->state = 12;
}
