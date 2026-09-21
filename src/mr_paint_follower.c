#include "../include/mr_paint.h"

#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/constants/species.h"
#include "../include/types.h"

#include "../include/bag.h"
#include "../include/item.h"
#include "../include/map_events_internal.h"
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
//
// 0.4.7: the party walk below is also exactly what a caller needs when it wants the follower's
// SLOT rather than its species (DoD 5 - routing an obstacle move to the follower's own party
// index). Factored out so both callers share one walk instead of two copies drifting apart.
static int MrPaintFollowerBaseSlot(FieldSystem *fieldSystem)
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
        return i;
    }

    return -1;
}

static u16 MrPaintFollowerBaseSpecies(FieldSystem *fieldSystem)
{
    int slot = MrPaintFollowerBaseSlot(fieldSystem);
    struct Party *party;
    struct PartyPokemon *mon;

    if (slot < 0) {
        return SPECIES_NONE;
    }

    party = SaveData_GetPlayerPartyPtr(fieldSystem->savedata);
    mon = Party_GetMonByIndex(party, slot);
    return (u16)GetMonData(mon, MON_DATA_SPECIES, NULL);
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
_Static_assert(MR_PAINT_FOLLOWMON_OFS(mapObject) == 0xE4, "followMon.mapObject must sit at FieldSystem+0xE4");

void MrPaintFollowPokeFsysParamSet(FieldSystem *fieldSystem, int species, u8 forme, BOOL shiny, u8 gender)
{
    // 0.4.10: Mr. Paint is SHINY. This store is the CACHE at FieldSystem+0xFB - a second,
    // independent copy of the shiny bit, separate from the one the map object carries (written by
    // FollowMon_SetObjectShiny, hooked below). The two must agree: if the cache says FALSE while
    // the drawn object says TRUE, the game's own live state contradicts the screen, and the cache
    // is the only half the headless harness can read back.
    if (MrPaintFollowerSubstitutes((u16)species)) {
        species = SPECIES_SMEARGLE;
        forme = 0;
        shiny = TRUE;
    }

    fieldSystem->followMon.species = species;
    fieldSystem->followMon.shiny = shiny;
    fieldSystem->followMon.forme = forme;
    fieldSystem->followMon.gender = gender;
}

// ---------------------------------------------------------------------------------------------
// 0.4.10: the DRAWN shiny bit
// ---------------------------------------------------------------------------------------------
//
// "Is the follower currently being drawn as Mr. Paint?", answered WITHOUT a species argument -
// FollowMon_SetObjectShiny below is handed an object and a boolean and nothing else. Composed
// from the two predicates this file already has rather than duplicating their gates, so the
// flag/item/lead conditions can never drift between the two answers.
//
// THE `base == SPECIES_NONE` GUARD IS LOAD-BEARING, not defensive padding. MrPaintFollowerBaseSpecies
// returns SPECIES_NONE (0) when no party member is alive, and MrPaintFollowerSubstitutes(0) would
// then evaluate its final line as `0 == MrPaintFollowerBaseSpecies(...)`, i.e. `0 == 0`, and return
// TRUE - forcing shiny on with an all-fainted party. Without this line the gate inverts in exactly
// that edge case.
static BOOL MrPaintFollowerIsSubstitutedNow(void)
{
    FieldSystem *fieldSystem = gFieldSysPtr;
    u16 base;

    if (fieldSystem == NULL || fieldSystem->savedata == NULL) {
        return FALSE;
    }

    base = MrPaintFollowerBaseSpecies(fieldSystem);
    if (base == SPECIES_NONE) {
        return FALSE;
    }

    return MrPaintFollowerSubstitutes(base);
}

// Full-function hook replacing retail FollowMon_SetObjectShiny (0x0206A080).
//
// THE FUNNEL, AND WHY IT IS HOOKED INSTEAD OF FollowPokeMapObjectSetParams (0x02069EE8):
// 0x0206A080 is the ONLY code in the ROM that writes the follower's shiny bit. Its two callers
// are 0x02069EE8 and an undocumented 5-argument sibling at 0x02069F0C, which is called from
// overlay 1 (0x0220201E) and has no arm9 caller at all - so hooking 0x02069EE8 alone would miss
// a live path. Hooking the funnel covers all three rebuild routes at once: the map load
// (FollowMon_InitMapObject), 0.4.9's instant swap (FollowMon_ChangeMon), and script opcode 606.
//
// The retail body, read from this ROM's bytes: MapObject_GetParam(obj, 2), clear bit 0 only
// (retail does `asrs r0,r0,#1` then `lsls r1,r0,#1`; pret writes the same thing as
// `param = (u32)(param >> 1) << 1`), OR in bit 0 when enabled, MapObject_SetParam(obj, param, 2).
// Both accessors were already linked (rom.ld:357-358) and already declared
// (include/map_events_internal.h:221-222), so this build adds nothing to rom.ld.
//
// When the gate is FALSE the caller's own `enable` passes through untouched, so THE REAL LEAD'S
// OWN SHININESS IS PRESERVED IN BOTH DIRECTIONS - a shiny lead stays shiny, a normal lead stays
// normal. That is the client's "must not leak either way" requirement, met by construction rather
// than by a second code path that could disagree with the first.
void MrPaintFollowMonSetObjectShiny(LocalMapObject *mapObject, BOOL enable)
{
    int param;

    if (MrPaintFollowerIsSubstitutedNow()) {
        enable = TRUE;
    }

    param = MapObject_GetParam(mapObject, 2);
    param = (int)(((u32)param >> 1) << 1);
    if (enable) {
        param |= 1;
    }
    MapObject_SetParam(mapObject, param, 2);
}

// 0.4.7 "obstacle move from the follower" (client DoD 5). ScrCmd_GetPartySlotWithMove
// (src/mr_paint.c) wants the follower's OWN party slot, not the sentinel, whenever the follower
// currently on screen really is Mr. Paint - so ROM script 146's `CompareVars 0x8004 0x8005`
// comes out EQUAL and Cut/Rock Smash/Strength take vanilla's own no-cut-in, overworld branch
// instead of the cutscene one.
//
// Every condition here earns its place:
//
//   - fieldSystem / savedata NULL: same defensive shape as MrPaintFollowerSubstitutes above -
//     ScrCmd_GetPartySlotWithMove always has a live ctx->fsys in practice, but failing safe to
//     "not deployed" costs nothing and matches this file's existing style.
//   - FLAG_MR_PAINT_FOLLOWING / Bag_HasItem: the same two-part gate MrPaintFollowerSubstitutes
//     uses - a flag left set on a corrupt or hand-edited save, with the item since removed from
//     the Bag, must never claim the follower is Mr. Paint.
//   - followMon.active: cheap early-out once the flag/item gate has passed, but NOT sufficient
//     on its own - see the ordering note below.
//   - followMon.mapObject NULL: defensive against reading a follower record that has been
//     cleared (FsysFollowMonClear) but not yet reflected in `active`, if such a window exists;
//     costs nothing to check before touching followMon.species.
//   - followMon.species == SPECIES_SMEARGLE: the LOAD-BEARING check. The toggle is not
//     immediate - FLAG_MR_PAINT_FOLLOWING can be set while the on-screen follower is still
//     whatever it was before the last map load, because the follower cache
//     (FollowPokeFsysParamSet, hooked above) only runs again on a map load. This was proven in
//     game, not assumed: toggling Mr. Paint on mid-map leaves the previous species walking
//     behind the player until the next door/warp/cave transition. So "the flag is set" is NOT
//     the same fact as "Smeargle is what's actually drawn right now" - only followMon.species,
//     the game's own live record of what the follower object was built as (written by the very
//     hook above), tells us that. Everything above this line is a cheap gate; this line is the
//     one that is actually correct.
//
// A wrong answer here can only make script 146's compare come out DIFFERENT (the cut-in plays,
// exactly 0.4.6 behaviour) - never a crash and never a wrong Pokemon animating, since the actor
// substitution in src/mr_paint.c is driven by sMrPaintActorActive, not by this return value.
int MrPaintDeployedFollowerSlot(FieldSystem *fieldSystem)
{
    if (fieldSystem == NULL || fieldSystem->savedata == NULL) {
        return -1;
    }

    if (!CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        return -1;
    }

    if (!Bag_HasItem(Sav2_Bag_get(fieldSystem->savedata), ITEM_MR_PAINT, 1, HEAPID_WORLD)) {
        return -1;
    }

    if (fieldSystem->followMon.active == 0) {
        return -1;
    }

    if (fieldSystem->followMon.mapObject == NULL) {
        return -1;
    }

    if (fieldSystem->followMon.species != SPECIES_SMEARGLE) {
        return -1;
    }

    return MrPaintFollowerBaseSlot(fieldSystem);
}

// ---------------------------------------------------------------------------------------------
// 0.4.9: the INSTANT swap
// ---------------------------------------------------------------------------------------------
//
// Retail's own mid-session "the follower is now a different Pokemon" entry point. Disassembled
// from THIS ROM (0x02069B74, Thumb, 0x1F0 bytes): its sole vanilla call site 0x02053278 loads
// r0 = the map-object manager and r1 = the first word of Location, i.e. the map id - so from C
// it is FollowMon_ChangeMon(fieldSystem->mapObjectMan, fieldSystem->location->mapId).
//
// It MUTATES THE EXISTING OBJECT IN PLACE: zero calls to the map-object allocator 0x0205E294 or
// to CreateFollowingSpriteFieldObject, and no call into overlay 1 at all. That is why it, and
// not a despawn/respawn, is the right answer to the client's "no frozen duplicate" - an orphan
// is structurally impossible on this path rather than merely unlikely.
//
// It also re-runs BOTH hooks this feature already ships: FollowPokeFsysParamSet (the cache,
// MrPaintFollowPokeFsysParamSet above) and FollowingPokemon_GetSpriteID (upstream's
// get_mon_ow_tag, which decides what is drawn). We are not building a new swap mechanism; we are
// invoking the game's own one at a new moment. Added to rom.ld for this build - every function it
// calls was already there.
extern void LONG_CALL THUMB_FUNC FollowMon_ChangeMon(void *mapObjectMan, int mapId);

// rom.ld:387. Proven from this ROM's bytes rather than from the symbol name: 0x0205F5A4 is Thumb,
// takes the manager alone, reads the object count from [mgr+0x04] and the array base from
// [mgr+0x124], walks it at a 300-byte stride, and CLEARS 0x40 - bit 6,
// MAPOBJECTFLAG_MOVEMENT_PAUSED - on the flags word at each object's offset +0. Its Pause twin at
// 0x0205F574 is the identical function with an `orr` instead of a `bic`. Neither ever touches
// bit 9 (0x200, VISIBLE), which is why a paused object stays drawn while it stops being stepped.
//
// 0.4.10: the hand-written extern that used to sit here is gone. This file now includes
// map_events_internal.h for MapObject_Get/SetParam, and that header already declares this function
// - better typed, too (MapObjectMan * rather than void *). Two declarations of it is a hard error,
// so the header's wins; the research above is kept because the symbol name alone does not prove
// what the function does.

// We write through fieldSystem->mapObjectMan and fieldSystem->location, so pin both the way the
// followMon block above is pinned. 0.4.3 shipped a build where an alignment pad silently moved a
// member four bytes and every store landed on the wrong field; an offset comment is not an offset.
_Static_assert(__builtin_offsetof(FieldSystem, mapObjectMan) == 0x3C, "FieldSystem.mapObjectMan must sit at +0x3C");
_Static_assert(__builtin_offsetof(FieldSystem, location) == 0x20, "FieldSystem.location must sit at +0x20");
_Static_assert(__builtin_offsetof(Location, mapId) == 0x00, "Location.mapId must be Location's first word");

// Swap the walking follower's identity NOW, in place, and let the game play its own Poke Ball
// animation over the change.
//
// Two halves, and the split is deliberate.
//
// (1) The identity. FollowMon_ChangeMon re-derives what the follower is from the party and from
//     our flag, so the flag MUST already be flipped before this runs - toggle direction is
//     carried entirely by the flag, and MrPaintFollowerSubstitutes reads it. Toggling ON, retail
//     hands it the real lead's species and we substitute; toggling OFF, the flag is clear, we
//     return FALSE, and the real lead comes back. No direction argument is needed anywhere.
//
//     THE GUARD is not defensive padding. FsysFollowMonClear (0x0206A06C) runs unconditionally at
//     the top of ChangeMon and zeroes followMon.mapObject (+0xE4) and followMon.active (+0xFA).
//     Of its five exit paths only the two success paths write them back; the eligibility-gate
//     path at 0x02074640 does not, and that gate was NOT disassembled. If it ever returns 0 with
//     a follower still on screen we would silently leave both zeroed - and 0.4.7's obstacle gate
//     (MrPaintDeployedFollowerSlot above) requires BOTH non-zero, so the follower would quietly
//     stop being able to use Cut/Rock Smash/Strength. Snapshotting and restoring costs four lines
//     and removes the need to prove anything about 0x02074640 at all. followMon.species (+0xF4)
//     is NOT among the fields FsysFollowMonClear zeroes, so a restore returns the exact pre-call
//     state.
//
// (2) The animation, which is the client's explicit request: "the same animation that plays when
//     you select your starter, or any time you visit a Pokemon center and your lead hops back out
//     after being healed." It exists, as two ordinary script commands - see
//     MR_PAINT_FOLLOWER_SWAP_SCRIPT in include/mr_paint.h for where they are attested in this
//     ROM. EventSet_Script is the same mechanism MrPaintTryQueueInspiration (src/mr_paint.c) and
//     src/repel.c already use to start a script from C.
//
//     The identity change happens BEFORE the script is queued, so the Pokemon that hops out is
//     always the new one. That ordering is correct whether opcode 606 rebuilds the model or
//     merely un-hides it, because the show happens after the sprite id was written either way -
//     which is exactly why no custom script command is needed to sequence it.
//
//     Queued ONLY when a live follower object exists. A script-level reset toggles an
//     already-initialised follower; it cannot build one from nothing (docs/log.md,
//     2026-09-13T06:51:14+05:30, where NoBallResetFollowingPoke was tried for that and failed).
//     So on a bike, while surfing, with a fainted lead, or on a map that forbids followers, this
//     queues nothing at all and the flag simply flips - preserving 0.4.3's edge-case contract.
//
// (3) 0.4.11 - THE RE-BIND, and why the two halves above were not enough. 0.4.9 shipped (1) and
//     (2) and verified them IN RAM ONLY: the cached species became 235, the map object's sprite
//     id at +0x10 became 2735, and every assertion passed - while the screen kept drawing
//     Cyndaquil until the next map load. A RAM assertion is not proof of what is drawn.
//
//     The cause: MapObject_SetGfxID (0x0205F258) is a bare `str r1,[r0,#16]; bx lr`. The 3D model
//     is bound to the object when the object is created and is released and re-requested only
//     through a dirty-bit chain - sub_0205E420 (frees the stale resource when MAPOBJECTFLAG_UNK14
//     is set, then invalidates the sprite id) -> ov01_021FA108 (the swap worker that writes the
//     render substruct) -> sub_0205E38C (re-stamps the id and clears the bit). Nothing on the
//     toggle path ever entered that chain. Opcodes 600/606 do not either: pret's ScrCmd_606
//     re-checks permission using the species the object ALREADY has and then only un-hides it and
//     plays the ball effect - it carries no identity and rebuilds no model.
//
//     ChangeMapObjSprite (rom.ld:363, 0x021FA930, overlay 1) is the function that runs that chain,
//     and this is not a guess: retail's own Gracidea Land/Sky Forme swap calls it on a LIVE
//     FOLLOWER map object, mid-cutscene, with no warp - FollowMon_SetObjectParams(...) then
//     ov01_021FA930(followMon.mapObject, SPRITE_FOLLOWER_MON_SHAYMIN). Its second argument is an
//     overworld TAG, which is what upstream's own include/map_events_internal.h:245 has always
//     declared it to be and what this ROM's bytes confirm (the slow path stores it at node+0 as
//     the resource lookup key). 0.4.9 rejected this call partly because "its 2nd argument is not
//     a bare species id" - true, but the wrong objection: nobody wants to pass a species.
//
//     Overlay-1 residency is guaranteed at both entry points: the Bag path runs only after
//     start-menu state 12 has waited on runningFieldMap, and the Y path runs on the live field.
//
//     THE CHANGED-TAG GUARD is deliberate. ChangeMapObjSprite has a fast path that allocates
//     nothing and a slow path that allocates a 0x58-byte load-request node whose free is handed to
//     an asynchronous consumer and was NOT located in the disassembly. Firing only when the drawn
//     tag actually changed bounds that to real swaps, and the 20-toggle heap measurement in
//     docs/mr-paint-follower-refresh.md is the gate that says it does not drift.
//
//     THE RE-STAMP afterwards is eight bytes of belt and braces. pret has sub_0205E38C re-writing
//     the sprite id from its second argument; this ROM's disassembly reads that function as
//     ignoring its second argument, with sub_0205E420 invalidating the id on the way through. The
//     two readings disagree and nothing cheap settles which is right - so write the tag back
//     ourselves and the object provably holds it under either one. MapObject_SetGfxID cannot
//     allocate and cannot fail.
static void MrPaintRefreshFollower(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;
    u8 active;
    u32 tagBefore;
    u32 tagAfter;

    if (fieldSystem == NULL || fieldSystem->mapObjectMan == NULL || fieldSystem->location == NULL) {
        return;
    }

    mapObject = fieldSystem->followMon.mapObject;
    active = fieldSystem->followMon.active;
    tagBefore = (mapObject != NULL) ? MapObject_GetGfxID(mapObject) : 0;

    FollowMon_ChangeMon(fieldSystem->mapObjectMan, fieldSystem->location->mapId);

    if (mapObject != NULL && active != 0) {
        if (fieldSystem->followMon.mapObject == NULL) {
            fieldSystem->followMon.mapObject = mapObject;
        }
        if (fieldSystem->followMon.active == 0) {
            fieldSystem->followMon.active = active;
        }
    }

    if (fieldSystem->followMon.mapObject != NULL && fieldSystem->followMon.active != 0) {
        tagAfter = MapObject_GetGfxID(fieldSystem->followMon.mapObject);
        if (tagAfter != tagBefore) {
            ChangeMapObjSprite(fieldSystem->followMon.mapObject, tagAfter);
            MapObject_SetGfxID(fieldSystem->followMon.mapObject, tagAfter);
        }
    }

    if (fieldSystem->followMon.mapObject != NULL && fieldSystem->followMon.active != 0) {
        EventSet_Script(fieldSystem, MR_PAINT_FOLLOWER_SWAP_SCRIPT, NULL);
    }
}

// The one place the flag actually flips. BOTH entry points below - the SELECT/field path and the
// 0.4.5 Bag/USE path - call this and nothing else, so they cannot drift apart the way two copies
// of the same three lines eventually would. 0.4.9 puts the refresh here for the same reason: the
// client reported the deferred swap from the Bag, but the Y path deferred identically, and one
// shared body is the only way the two stay honest.
static void MrPaintToggleFollowingFlag(FieldSystem *fieldSystem)
{
    if (CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        ClearScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    } else {
        SetScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    }

    MrPaintRefreshFollower(fieldSystem);
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
// 0.4.9: `data` is no longer UNUSED - struct ItemFieldUseData's first member IS the FieldSystem
// (include/item.h:107-108), so the refresh needs nothing new here. This path runs on the live
// overworld, so it deliberately does NOT unpause: nothing ever paused for it. Confirmed in game
// on 0.4.7 - across a Y toggle the follower object's flags word is byte-identical to a control
// that did nothing at all, while the Bag arm gains 0x40 and never loses it.
BOOL ItemFieldUseFunc_MrPaintToggle(struct ItemFieldUseData *data)
{
    MrPaintToggleFollowingFlag(data == NULL ? NULL : data->fieldSystem);
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
// 0.4.9 fixes the client's "frozen, static duplicate of the lead Pokemon" here, and it is one
// call. Opening the Bag pauses EVERY active map object - MapObjectManager_PauseAllMovement sets
// MAPOBJECTFLAG_MOVEMENT_PAUSED, bit 6, on each one - and start-menu state 13 frees the menu and
// TaskManager_Jumps here WITHOUT unpausing, handing that obligation to the exit task. Every
// retail state=12 exit task that does not end in a warp discharges it itself:
// Task_MountOrDismountBicycle's state 2 is literally this one call. Ours was the odd one out, in
// every build since 0.4.6.
//
// Bit 6 is movement; VISIBLE is bit 9 and pause/unpause never touch it - so a paused object stays
// drawn, on its tile, and simply stops being stepped. That is exactly the client's sentence,
// "unlinks the lead Pokemon's movement logic without despawning its sprite". It was never an
// orphaned object: no second follower is ever created on this path (proven headlessly, three arms
// from one savestate), and the flags word gains exactly 0x40 the instant the Bag closes, in the
// Bag arm alone, and is never cleared.
//
// The unpause goes FIRST, before the toggle, so the field is already live when the swap script's
// own lockall/releaseall pair runs. That releaseall would unpause too - but the fix must not
// depend on a script that is only queued when a follower happens to exist, and the NPCs frozen
// alongside the follower need releasing whether one does or not.
//
// It stays HERE and not in MrPaintToggleFollowingFlag: the Y path never enters the start-menu
// state machine, so nothing paused for it, and unpausing there could clear a pause some other
// system legitimately set.
BOOL Task_MrPaintToggle(TaskManager *taskman)
{
    FieldSystem *fieldSystem = (taskman == NULL) ? NULL : taskman->fieldSystem;

    if (fieldSystem != NULL && fieldSystem->mapObjectMan != NULL) {
        MapObjectMan_UnpauseAllMovement(fieldSystem->mapObjectMan);
    }

    MrPaintToggleFollowingFlag(fieldSystem);
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
