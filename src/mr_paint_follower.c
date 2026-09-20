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

// The toggle itself, reached as the `field` column of sNewItemFieldUseFuncs[] row 6 (src/item.c),
// i.e. when Mr. Paint is REGISTERED TO SELECT and SELECT is pressed on the overworld. The item
// already ships .selectable = TRUE, so this needs nothing else.
//
// Shape copied from retail ItemFieldUseFunc_Bicycle (0x02064C30), the one attested example of a
// key item whose effect happens on the field with no sub-application: read what is needed out of
// `data`, act, and RETURN FALSE. TRUE is what the app-opening use functions return - returning it
// without having started an app is the soft-lock risk, so FALSE is the only correct value here.
// We deliberately do NOT copy the Bicycle's `fieldSystem[0xD2] |= 0x80`: that bit accompanies a
// field task that later clears it, and setting it without one is how a field lock gets stuck.
//
// The `menu` column of our row is NULL on purpose - the Bag USE path needs an idiom this project
// has not yet established, and guessing it risks soft-locking the Bag. See
// docs/mr-paint-follower.md "Finding G" in the james-game repo; it is build 0.4.4's job.
//
// Edge cases are all the same case, by design: this only ever flips a save flag. If no follower
// can exist right now - lead fainted, on a bike, surfing, or a map that forbids followers - the
// flag still flips and nothing else happens, and Mr. Paint appears when a follower legitimately
// next can. We never force a follower where vanilla would show none: retail's own permission check
// runs on the REAL species upstream of every substitution point, so it is untouched.
BOOL ItemFieldUseFunc_MrPaintToggle(struct ItemFieldUseData *data UNUSED)
{
    if (CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        ClearScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    } else {
        SetScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    }

    return FALSE;
}
