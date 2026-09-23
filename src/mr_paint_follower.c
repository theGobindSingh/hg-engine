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

// 0.4.12: the changed-tag guard now spans two functions that run ~24 frames apart, so the tag the
// follower was drawn with BEFORE FollowMon_ChangeMon has to survive the gap. A file-static is the
// whole mechanism: MrPaintRefreshFollower records it, MrPaintRebindFollowerModel (called from the
// swap script, between the recall and the hop-out) compares against it. It cannot go stale in a
// way that matters - the only writer is the toggle itself, the only reader is the script it
// queues, and a toggle that queues no script leaves a value nothing will ever read.
static u32 sMrPaintTagBeforeSwap;

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
//     0.4.12 CORRECTS TWO CLAIMS THIS COMMENT USED TO MAKE, both disproven by measurement; see
//     docs/mr-paint-ball-animation.md in the james-game repo for the full RCA.
//
//     (a) "EventSet_Script is the mechanism" - true on the Y path only. On the Bag USE path our
//         caller is a TaskFunc, and EventSet_Script ends in FieldSystem_CreateTask, whose
//         GF_ASSERT(taskman == NULL) is compiled into retail; it then clobbers
//         fieldSystem->taskman anyway, and because the field-task pump re-reads taskman AFTER the
//         task function returns, the freshly created script task was being freed the same frame.
//         The script never ran at all from the Bag. So the starter is now chosen per path:
//         StartScriptFromMenu (include/task.h) inside a task, EventSet_Script outside one. That
//         is not inconsistency - retail's own ItemFieldUseFunc_Bicycle has the EventSet_Script
//         shape exactly where we use it, and A2 proved in game that the Y path already worked.
//
//     (b) "the refresh does NOT need to sit between the two lines" - wrong, and doubly so. With
//         the two opcodes back-to-back the whole script ran in ~2 frames and nothing was drawn:
//         opcode 600 yields at most ONE frame and 606 returns FALSE, where vanilla separates them
//         by hundreds of frames. Once the script waits properly, re-binding the model beforehand
//         means the player watches MR. PAINT get sucked into the ball and Mr. Paint hop back out
//         - the opposite of what the client asked for. The re-bind therefore moved OUT of this
//         function and into MrPaintRebindFollowerModel below, which the script calls while the
//         follower is hidden. The IDENTITY (FollowMon_ChangeMon and its restore guard) stays
//         here, so the cache is correct the instant the flag flips and map-load persistence is
//         untouched.
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
//
//     0.4.12 MOVES THOSE TWO CALLS OUT of this function, unchanged, into
//     MrPaintRebindFollowerModel below - see (b) above for why. Everything this paragraph argues
//     still holds; only the moment it happens changed. The pre-swap tag travels to the new home
//     in sMrPaintTagBeforeSwap so the changed-tag guard survives the move intact.
//
// `taskman` is the TaskManager of the caller when there is one (the Bag USE path, where we run
// inside Task_MrPaintToggle) and NULL when there is not (the Y path, a plain field-use func). It
// exists ONLY to pick the script starter - see (a) above - and is threaded through rather than
// re-derived because a field-use func genuinely has no task to derive it from.
// 0.4.24 split: this is ONLY the identity half of the old MrPaintRefreshFollower - the tag record,
// FollowMon_ChangeMon, and the restore guard - with none of the "start the swap script" decision
// that follows it. The Poke Center nurse (src/script_new_cmds.c's MrPaintNurseRecall, below) needs
// exactly this half and nothing else: her own common script decides whether to run the swap
// script's animation body, through a plain `call`, once it knows a live follower survived.
//
// Returns TRUE when a live follower (non-NULL mapObject AND active != 0) exists after the swap,
// the same test MrPaintRefreshFollower used to inline before deciding whether to queue anything.
static BOOL MrPaintSwapFollowerIdentity(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;
    u8 active;

    if (fieldSystem == NULL || fieldSystem->mapObjectMan == NULL || fieldSystem->location == NULL) {
        return FALSE;
    }

    mapObject = fieldSystem->followMon.mapObject;
    active = fieldSystem->followMon.active;
    sMrPaintTagBeforeSwap = (mapObject != NULL) ? MapObject_GetGfxID(mapObject) : 0;

    FollowMon_ChangeMon(fieldSystem->mapObjectMan, fieldSystem->location->mapId);

    if (mapObject != NULL && active != 0) {
        if (fieldSystem->followMon.mapObject == NULL) {
            fieldSystem->followMon.mapObject = mapObject;
        }
        if (fieldSystem->followMon.active == 0) {
            fieldSystem->followMon.active = active;
        }
    }

    return fieldSystem->followMon.mapObject != NULL && fieldSystem->followMon.active != 0;
}

// The Bag/Y toggle's caller: identity swap, then (only with a live follower afterward) start the
// animation script. Logic is unchanged from before the 0.4.24 split - MrPaintSwapFollowerIdentity
// inlines exactly what used to sit here, so this remains byte-for-byte equivalent in behaviour.
static void MrPaintRefreshFollower(FieldSystem *fieldSystem, TaskManager *taskman)
{
    if (!MrPaintSwapFollowerIdentity(fieldSystem)) {
        return;
    }

    if (taskman != NULL) {
        StartScriptFromMenu(taskman, MR_PAINT_FOLLOWER_SWAP_SCRIPT, NULL);
    } else {
        EventSet_Script(fieldSystem, MR_PAINT_FOLLOWER_SWAP_SCRIPT, NULL);
    }
}

// The half of the old refresh that now runs from INSIDE the swap script, one line after opcode
// 600 has finished sucking the old follower into the ball and one line before 606 pops the new
// one out - i.e. entirely inside the window where the object is hidden, which is the whole point.
// Reached through src/script_new_cmds.c's SCRIPT_NEW_CMD_MR_PAINT_SWAP_FOLLOWER_MODEL (2), which
// must keep matching NEW_COMMAND_MR_PAINT_SWAP_FOLLOWER_MODEL in armips/include/scriptmacros.s.
//
// It re-reads followMon.mapObject and followMon.active rather than trusting anything captured
// earlier: opcode 606 restores the follower only if FollowMon_IsActive passes, so this must never
// be the thing that zeroes it. The restore guard in MrPaintRefreshFollower has already run by the
// time we get here, so both fields are live if they can be.
void MrPaintRebindFollowerModel(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;
    u32 tag;

    if (fieldSystem == NULL) {
        return;
    }

    mapObject = fieldSystem->followMon.mapObject;
    if (mapObject == NULL || fieldSystem->followMon.active == 0) {
        return;
    }

    tag = MapObject_GetGfxID(mapObject);
    if (tag != sMrPaintTagBeforeSwap) {
        ChangeMapObjSprite(mapObject, tag);
        MapObject_SetGfxID(mapObject, tag);
    }
}

// ---------------------------------------------------------------------------------------------
// 0.4.13: putting the follower BACK on screen, which 606 does not do
// ---------------------------------------------------------------------------------------------
//
// 0.4.12 made the native recall play - the follower shrinks and a Poke Ball is drawn on its tile,
// on both entry paths - and then the follower NEVER CAME BACK. It stayed invisible until the
// player took a step, measured at ~50 s of no input, with the follower object's flags word parked
// at 0x200CE621 against a baseline 0x2004E421 for the whole window. Two facts out of the
// disassembly explain that exactly, and both are counter-intuitive enough to be worth writing
// down rather than leaving to the next reader to re-derive:
//
// (1) THE FLAG'S NAME IS INVERTED. MapObject_SetVisible writes bit 9 and MapObject_SetFlag19
//     writes bit 19, but bit 9 SET means HIDDEN, not shown - pret's own FollowMon_IsVisible
//     returns TRUE when MAPOBJECTFLAG_VISIBLE is CLEAR. Reading the flags word with the name as a
//     guide gives the opposite answer to the truth, which is part of why 0.4.12's watch on that
//     word looked benign.
//
// (2) ScrCmd_606 IS NOT INERT - IT LATCHES THE FOLLOWER HIDDEN. It calls sub_02069DEC(object,
//     TRUE), which sets bit 1 of the object's PARAM 2, a persistent "keep hidden" latch that
//     FollowMon_ChangeMon itself re-hides on. Because that is a map-object PARAM and not the flags
//     word, a flags-word watch shows no trace of it at all - which is why 0.4.12's measurement
//     found the symptom and not the cause. So the thing we were relying on to pop the follower
//     back out is the very thing pinning it down.
//
// sub_02069DC8(obj, FALSE) is the complete inverse of both halves in one call: it performs
// sub_0206A040(obj, FALSE), which clears BOTH flag bits, and it clears the param-2 latch - so the
// restore also survives the next map load instead of being undone by the next ChangeMon. It is
// attested in retail at src/field_take_photo.c:809, it was already linked (rom.ld:385) and already
// prototyped (include/map_events_internal.h:241), and nothing in this tree used it before now, so
// this build adds nothing to rom.ld.
//
// POLARITY, spelled out because the parameter name reads backwards too: pass FALSE to make the
// follower DRAWN. TRUE hides it.
//
// WHY THE SCRIPT CALLS THIS AND NOT THE C TOGGLE: retail only ever un-hides a follower from inside
// the task that owns the recall effect, once that effect has finished. Ours has no such task, so
// the script stands in for one - this runs after BOTH `wait 24`s, i.e. after the recall and the
// hop-out have each had their frames, so it can never clear the latch out from under an animation
// that is still playing.
//
// The NULL / `active` guard is mandatory, not defensive padding: MapObject_SetBits dereferences
// its argument with no null check of its own, so a follower-less fire would fault rather than
// no-op. Both fields are re-read here rather than captured earlier, for the same reason
// MrPaintRebindFollowerModel re-reads them.
void MrPaintShowFollower(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;

    if (fieldSystem == NULL) {
        return;
    }

    mapObject = fieldSystem->followMon.mapObject;
    if (mapObject == NULL || fieldSystem->followMon.active == 0) {
        return;
    }

    sub_02069DC8(mapObject, FALSE);
}

// ---------------------------------------------------------------------------------------------
// 0.4.23: spawn tile fix (James 0.4.17 item 3, docs/mr-paint-swap-polish.md design B)
// ---------------------------------------------------------------------------------------------
//
// The client's report: toggling Mr. Paint ON or OFF drops the incoming Pokemon on the PLAYER'S
// tile instead of the outgoing follower's own tile. RCA in james-game's docs/mr-paint-swap-
// polish.md, replayed on a frozen 0.4.17 control: both retail's own recall task and ScrCmd_606
// call ov01_02205790(fieldSystem, dir), which copies the PLAYER's position vector onto the
// follower object and sets its facing - that is how vanilla ALWAYS places a follower coming out
// of its ball, so 0.4.13's un-hide was never the only culprit and neither is any future one.
//
// Design B records the outgoing follower's own tile immediately before the recall hides it, then
// places the incoming one back there directly instead of asking retail to park it on the player.
static struct {
    u32 x;
    u32 y;
    u32 z;
    u32 facing;
    BOOL valid;
} sMrPaintFollowerTile;

// Overlay-1, proven against this ROM's bytes (docs/mr-paint-swap-polish.md):
//   ov01_0220329C(mapObject, mode) - the native field-effect starter; mode 0 is the effect
//     retail's own follower step handler (asm/unk_020658D4.s) plays right before un-hiding a
//     freshly-recalled follower that was armed the way step 3 below arms this one.
//   ov01_02205790(fieldSystem, dir) - the function described above that puts a follower on the
//     PLAYER's tile. Used here ONLY in the fallback path, to reproduce ScrCmd_606 exactly when we
//     deliberately choose not to reposition (see MrPaintEmergeAtRecordedTile).
extern void LONG_CALL THUMB_FUNC ov01_0220329C(LocalMapObject *mapObject, int mode);
extern void LONG_CALL THUMB_FUNC ov01_02205790(FieldSystem *fieldSystem, int direction);

// Script cmd 4 (mr_paint_record_follower_tile): the first line of scr_seq_0003_075, before
// `send_follower_to_ball` (600) hides the outgoing follower. Records exactly what
// MapObject_SetPositionFromXYZAndDirection needs to put the incoming one back on the same tile.
// Leaves `valid` FALSE with no live follower - MrPaintEmergeAtRecordedTile's own fallback covers
// that, so there is nothing more to guard here than not dereferencing NULL.
void MrPaintRecordFollowerTile(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;

    sMrPaintFollowerTile.valid = FALSE;

    if (fieldSystem == NULL) {
        return;
    }

    mapObject = fieldSystem->followMon.mapObject;
    if (mapObject == NULL || fieldSystem->followMon.active == 0) {
        return;
    }

    sMrPaintFollowerTile.x = MapObject_GetCurrentX(mapObject);
    sMrPaintFollowerTile.y = MapObject_GetYCoord(mapObject);
    sMrPaintFollowerTile.z = MapObject_GetZCoord(mapObject);
    sMrPaintFollowerTile.facing = MapObject_GetFacingDirection(mapObject);
    sMrPaintFollowerTile.valid = TRUE;
}

// Script cmd 5 (mr_paint_emerge_at_recorded_tile): replaces BOTH `reset_follower_with_ball` (606)
// and mr_paint_show_follower at the tail of scr_seq_0003_075, run once the model has already been
// rebound while the object is hidden (mr_paint_swap_follower_model, just before this).
//
//   1. No live follower, no recorded tile, or the recorded tile equals the player's own current
//      tile (the toggle happened somewhere the two coincide, or the record could not be trusted):
//      fall back to EXACTLY what ScrCmd_606 does - arm the deferred pop-out and let retail's own
//      step handler play it on the player's next step. Never worse than today's vanilla shape.
//   2. Otherwise, MapObject_SetPositionFromXYZAndDirection puts the object back on the tile the
//      outgoing follower stood on (current X/Y/Z, the render vector and facing; clears held
//      movement - pret map_object.c:1989, confirmed against this ROM's bytes).
//   3. Arm the same two bits ScrCmd_606 arms (sub_02069E84(obj,1), sub_02069DEC(obj,TRUE)), which
//      is what retail's own step handler checks before it treats an object as "about to emerge".
//   4. Play the emerge immediately instead of waiting for a step: ov01_0220329C(obj, 0) - the same
//      effect retail's step handler plays - then clear the "about to emerge" bit
//      (sub_02069E84(obj, FALSE)) and un-hide (sub_02069DC8(obj, FALSE), which also clears the
//      keep-hidden latch step 3 set, so nothing is left armed for a step that will never come).
//
// The record is consumed exactly once, whichever path runs, so a toggle that queues no swap
// script at all (no follower to begin with) can never leave a stale tile for the next one.
void MrPaintEmergeAtRecordedTile(FieldSystem *fieldSystem)
{
    LocalMapObject *mapObject;
    u32 x, y, z, facing;
    BOOL haveTile;

    haveTile = sMrPaintFollowerTile.valid;
    x = sMrPaintFollowerTile.x;
    y = sMrPaintFollowerTile.y;
    z = sMrPaintFollowerTile.z;
    facing = sMrPaintFollowerTile.facing;
    sMrPaintFollowerTile.valid = FALSE;

    if (fieldSystem == NULL) {
        return;
    }

    mapObject = fieldSystem->followMon.mapObject;
    if (mapObject == NULL || fieldSystem->followMon.active == 0) {
        return;
    }

    // BUGFIX (verified against this ROM's own arm9.bin): GetPlayerXCoord (0x0205c67c) forwards to
    // 0x0205f914, the SAME address rom.ld names MapObject_GetCurrentX (+0x64) - X vs X is correct.
    // But GetPlayerYCoord (0x0205c688) forwards to 0x0205f934, which rom.ld names
    // MapObject_GetZCoord (+0x6c) - NOT MapObject_GetYCoord (+0x68, height), which is where the
    // recorded `y` above comes from. Comparing GetPlayerYCoord() against `y` was comparing two
    // unrelated fields, so this "recorded tile coincides with the player's own tile" guard almost
    // never fired correctly. The right comparison is GetPlayerYCoord() against `z`.
    if (haveTile && fieldSystem->playerAvatar != NULL) {
        if ((u32)GetPlayerXCoord(fieldSystem->playerAvatar) == x
            && (u32)GetPlayerYCoord(fieldSystem->playerAvatar) == z) {
            haveTile = FALSE;
        }
    }

    if (!haveTile) {
        sub_02069E84(mapObject, TRUE);
        sub_02069DEC(mapObject, TRUE);
        ov01_02205790(fieldSystem, 1);
        return;
    }

    MapObject_SetPositionFromXYZAndDirection(mapObject, x, y, z, facing);
    sub_02069E84(mapObject, TRUE);
    sub_02069DEC(mapObject, TRUE);
    ov01_0220329C(mapObject, 0);
    sub_02069E84(mapObject, FALSE);
    sub_02069DC8(mapObject, FALSE);
}

// ---------------------------------------------------------------------------------------------
// 0.4.24: the Poke Center nurse recall (James 0.4.17 item 4, docs/mr-paint-pokecenter.md)
// ---------------------------------------------------------------------------------------------
//
// Client's ask: healing at a Center must turn Mr. Paint off and put the real party's slot-0
// Pokemon on the counter, "identical to pressing USE to deactivate him". The frozen design's
// first cut assumed `clearflag 2224; call scr_seq_0003_mr_paint_swap_body` was the whole thing -
// it is NOT. The Bag/Y toggle's IDENTITY change (FollowMon_ChangeMon plus the restore guard,
// recording sMrPaintTagBeforeSwap above) happens here in C, in MrPaintSwapFollowerIdentity,
// strictly BEFORE script 2075/scr_seq_0003_mr_paint_follower_swap is ever queued or called -
// script 2075's mr_paint_swap_follower_model only re-binds the 3D model to whatever the identity
// ALREADY is. No script opcode reaches FollowMon_ChangeMon. So a `clearflag` followed by nothing
// but the swap script's body would rebind the model to the SAME identity it already had - visibly
// nothing would change, and slot 0 would never appear.
//
// The fix: give the nurse's own script the identity half as one new command, exactly what
// MrPaintRefreshFollower already runs for the Bag/Y path, and let her script `call` the shared
// swap-body subroutine (armips label _mr_paint_swap_body, scr_seq_00003_commonscript.s) for the
// animation half - but ONLY when a live follower survived the identity swap, the same gate
// MrPaintRefreshFollower already applies before queuing anything.
//
// Return value is the resultVar contract src/script_new_cmds.c's SCRIPT_NEW_CMD_MR_PAINT_NURSE_
// RECALL writes into a script var, which the nurse script then `compare`s against 1:
//   0 - flag was already clear. Vanilla by construction; nothing else runs.
//   1 - flag was set, now cleared, and a live follower exists after the swap. The caller `call`s
//       the shared swap-body subroutine (Mr. Paint's model goes into the ball, the real lead's
//       hops out at the recorded tile) before continuing to the vanilla heal.
//   2 - flag was set, now cleared, but no live follower survived the swap - the same edge cases
//       MrPaintRefreshFollower's own "queues nothing" comment documents (bike, surfing, fainted
//       lead, a follower-forbidding map). The item is off; there is nothing to animate, so the
//       caller must not call the swap body.
u16 MrPaintNurseRecall(FieldSystem *fieldSystem)
{
    if (!CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        return 0;
    }

    ClearScriptFlag(FLAG_MR_PAINT_FOLLOWING);

    return MrPaintSwapFollowerIdentity(fieldSystem) ? 1 : 2;
}

// The one place the flag actually flips. BOTH entry points below - the SELECT/field path and the
// 0.4.5 Bag/USE path - call this and nothing else, so they cannot drift apart the way two copies
// of the same three lines eventually would. 0.4.9 puts the refresh here for the same reason: the
// client reported the deferred swap from the Bag, but the Y path deferred identically, and one
// shared body is the only way the two stay honest.
//
// 0.4.12 threads `taskman` straight through for the same reason: one shared body, and the ONE
// thing the two paths legitimately differ on (which script starter is legal in their context)
// travels as an argument rather than as a second copy of the body.
static void MrPaintToggleFollowingFlag(FieldSystem *fieldSystem, TaskManager *taskman)
{
    if (CheckScriptFlag(FLAG_MR_PAINT_FOLLOWING)) {
        ClearScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    } else {
        SetScriptFlag(FLAG_MR_PAINT_FOLLOWING);
    }

    MrPaintRefreshFollower(fieldSystem, taskman);
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
    // NULL taskman: a field-use func runs in the idle context, with no task of its own, which is
    // exactly the context EventSet_Script's FieldSystem_CreateTask asserts for. A2 proved in game
    // that the swap script really does run on this path.
    MrPaintToggleFollowingFlag(data == NULL ? NULL : data->fieldSystem, NULL);
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
//     0.4.12 RETRACTS THAT LAST SENTENCE. It was true only while this task did nothing but flip a
//     flag. Now it starts a script through StartScriptFromMenu, which ends in TaskManager_Jump,
//     and TaskManager_Jump reuses the CALLING task struct in place rather than allocating a new
//     one - so returning TRUE pops and frees the very task just aimed at the script, and the
//     script silently never runs. Retail's ten Task_Use*InField field-move tasks are structurally
//     identical to ours and every one of them returns FALSE. See the comment at the return below.
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

    // 0.4.12: `taskman` goes down so the refresh can start the swap script with
    // StartScriptFromMenu instead of EventSet_Script - the only starter that is legal from inside
    // a task, and the fix for the script never having run on this path at all.
    MrPaintToggleFollowingFlag(fieldSystem, taskman);

    // MANDATORY FALSE, not a style choice. StartScriptFromMenu tail-calls TaskManager_Jump, which
    // re-aims THIS task struct at Task_RunScripts in place; FieldSystem_RunTaskFrame's
    // `while (taskman->func(taskman) == TRUE)` loop would then pop and free it on this very
    // frame, taking the script with it. The failure mode is silent - it looks exactly like the
    // feature not firing - which is why it is spelled out here rather than left to the reader.
    // Nothing leaks by returning FALSE: ItemMenuUseFunc_MrPaintToggle below sets
    // atexit_TaskEnv = NULL, so this task owns no environment to free.
    return FALSE;
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
