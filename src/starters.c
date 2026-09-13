#include "../include/constants/moves.h"
#include "../include/constants/species.h"
#include "../include/map_events_internal.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"
#include "../include/types.h"

extern u32 space_for_setmondata;
extern u32 sStarterChoiceCries[];

void LONG_CALL GetMonSpriteCharAndPlttNarcIdsEx(MON_PIC *picdata, u16 mons_no, u8 dir, u8 col, u8 form_no, u8 a5, u32 personality);

// Use MON_WITH_FORM(SPECIES_NAME, form) to specify a starter
// with a form.
//
// Keep in mind that these choices will impact Rival team
// determination in the appropriate trigger-scripts.
//
// This will NOT update the text during the starter-selection
// sequence. To update that text, modify text archive 190 in
// DSPRE.
static const u16 sStarterChoices[3] = {
    SPECIES_CHIKORITA,
    SPECIES_CYNDAQUIL,
    SPECIES_TOTODILE,
};

/**
 *  @brief fills the given array with the species ids of the starter choices
 *
 *  @param species pointer to the species array
 */
void LONG_CALL CreateStarter_SetStarterSpecies(int *species)
{
    for (int i = 0; i < 3; i++) {
        // strip off form
        species[i] = sStarterChoices[i] & 0x7FF;
    }
}

/**
 *  @brief wrap the CreateMon call within CreateStarter to allow for starter forms
 *
 *  @param mon the PartyPokemon pointer
 *  @param species the species id
 *  @param slot starter choice (0-2)
 */
void LONG_CALL CreateStarter_CreateMon(struct PartyPokemon *mon, int species, int slot)
{
    u32 form = 0;

    if (slot >= 0 && slot < 3) {
        form = sStarterChoices[slot] >> 11;
    }

    space_for_setmondata = form;
    PokeParaSet(mon, species, 5, 32, FALSE, 0, 0, 0);
    space_for_setmondata = 0;

    if (form != 0) {
        SetMonData(mon, MON_DATA_FORM, &form);
    }
}

/**
 *  @brief wrap the GetMonSpriteCharAndPlttNarcIdsEx call within createMonSprites to handle starter forms
 *
 *  @param pic MON_PIC pointer
 *  @param species species id
 */
void LONG_CALL CreateMonSprites_HandleForm(MON_PIC *pic, u16 species, u8 gender, u8 shiny, int slot)
{
    u32 form = 0;

    if (slot >= 0 && slot < 3) {
        form = sStarterChoices[slot] >> 11;
        sStarterChoiceCries[slot] = (form == 0) ? species : PokeOtherFormMonsNoGet(species, form);
    }

    GetMonSpriteCharAndPlttNarcIdsEx(pic, species, gender, 2, shiny, 0, 0);

    if (form != 0) {
        GetOtherFormPic(pic, species, 2, shiny, form);
    }
}


// ===========================================================================
// Caretaker Retirement, phase 1: one Poke Ball in Elm's lab, holding a Happiny
// ===========================================================================
//
// Two vanilla script commands are replaced wholesale (registered in `hooks`).
// Both are Thumb BOOL(SCRIPTCONTEXT *) like every other scrcmd handler.

#define HAPPINY_PERFECT_IV_COUNT 4

void LONG_CALL MapPropManager_LoadOne(void *mapPropManager, u32 propId, const VecFx32 *pos, u32 a3, void *animManager);
int LONG_CALL PokeParty_GetPokeCount(void *party);

/**
 *  @brief force four distinct, randomly chosen IVs to 31, leaving the other two
 *         as they were rolled
 *
 *  The brief asks for a starter with four perfect IVs and two random ones: a
 *  guaranteed-good but not flawless partner. A partial Fisher-Yates shuffle over
 *  the six IV field ids keeps the four picks distinct - drawing four independent
 *  random indices would sometimes collide and leave fewer than four perfect.
 *
 *  @param mon the PartyPokemon to modify
 */
static void SetFourRandomPerfectIVs(struct PartyPokemon *mon)
{
    int ivFields[6] = {
        MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV,
        MON_DATA_SPEED_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV,
    };
    // SetMonData reads an IV through a 32-bit buffer (see src/pokemon.c:1406).
    u32 perfect = MAX_IVS;

    for (int i = 0; i < HAPPINY_PERFECT_IV_COUNT; i++) {
        int j = i + (gf_rand() % (6 - i));
        int tmp = ivFields[i];
        ivFields[i] = ivFields[j];
        ivFields[j] = tmp;

        SetMonData(mon, ivFields[i], &perfect);
    }

    // The HP IV feeds max HP, so the stats cached on the party mon are stale now.
    RecalcPartyPokemonStats(mon);
}

/**
 *  @brief force the starter's moveset to an exact, known-good four moves
 *
 *  The level-up learnset fill (species data -> level 5) was dropping the
 *  first move, leaving Happiny with only Charm and Copycat and no attack -
 *  a hard blocker. Aromatherapy is an egg move for the Chansey line in HGSS,
 *  so it can never come from the level-up learnset at all. Rather than chase
 *  the fill bug, the four slots are set explicitly and unconditionally here,
 *  in the client's requested order, so the starter is correct regardless of
 *  where that bug turns out to live.
 *
 *  @param mon the PartyPokemon to modify
 */
static void SetHappinyStarterMoves(struct PartyPokemon *mon)
{
    static const u16 moves[4] = {
        MOVE_POUND, MOVE_CHARM, MOVE_COPYCAT, MOVE_AROMATHERAPY,
    };

    for (int i = 0; i < 4; i++) {
        u8 ppUps = 0;
        SetMonData(mon, MON_DATA_MOVE1PPUP + i, &ppUps);

        u16 moveId = moves[i];
        SetMonData(mon, MON_DATA_MOVE1 + i, &moveId);

        u8 maxPP = GetMonData(mon, MON_DATA_MOVE1MAXPP + i, 0);
        SetMonData(mon, MON_DATA_MOVE1PP + i, &maxPP);
    }
}

// ---------------------------------------------------------------------------
// scrcmd 0xA7 (167), vanilla arm9 0x020430C4, pret's ScrCmd_ChooseStarter.
// ---------------------------------------------------------------------------
//
// Vanilla launches the touchscreen starter-selection scene, which hardcodes
// three ball models, three touch targets and three sprite slots in overlay 61 -
// it cannot be reduced to a single ball without rewriting that overlay. Since
// the lab now offers exactly one Pokemon, the scene has nothing to choose
// between, so script 0843 hands the mon over with the ordinary `GivePokemon`
// command instead (which does the OT / met-location / Poke Ball bookkeeping
// correctly) and this command is repurposed to do the one thing `GivePokemon`
// cannot: guarantee the IV spread.
//
// Safe to repurpose: 0xA7 is used exactly once in the whole ROM, at
// project/scripts/0843.script Script#13. It takes no operands, so scripts
// assemble unchanged and DSPRE / dspre-mcp still validate it.
BOOL LONG_CALL ScrCmd_ChooseStarter(SCRIPTCONTEXT *ctx)
{
    FieldSystem *fsys = ctx->fsys;
    struct Party *party = SaveData_GetPlayerPartyPtr(fsys->savedata);
    int count = PokeParty_GetPokeCount(party);

    // Act on the mon GivePokemon just appended, not on slot 0, so the command
    // stays correct if the script order is ever rearranged.
    if (count > 0) {
        SetFourRandomPerfectIVs(&party->members[count - 1]);
        SetHappinyStarterMoves(&party->members[count - 1]);
    }

    // Give the new starter its map object, so it walks behind the player
    // immediately instead of only after the next warp.
    //
    // Vanilla never needed this. Its starter scene ran as a field application,
    // and re-entering the field runs the warp-restore path, which is what
    // builds fieldSystem->followMon. Handing the mon over with a plain
    // GivePokemon never leaves the field, so script 0843's vanilla tail
    // (SetFollowingPokePosition / SendOutFollowingPoke) was acting on a
    // follower that had no map object, and nothing appeared. Opening and
    // closing the party menu appeared to "fix" it because that is a field
    // application too.
    //
    // FollowMon_InitMapObject (arm9 0x020699F8) is the exact function the warp
    // path uses - vanilla's only caller is 0x020531FC, pret's sub_0205316C in
    // field_warp_tasks.c. It does the whole job itself: the party lookup, the
    // FollowMon_GetPermissionBySpeciesAndMap gate (Elm's lab passes it, which
    // is why vanilla's own starter follows in here), CreateFollowingSpriteField-
    // Object, followMon.active and FieldSystem_SetFollowerPokeParam. Calling it
    // is exactly what a warp would have done, minus the warp.
    //
    // We pass the player's CURRENT coordinates via GetPlayerXCoord/
    // GetPlayerYCoord rather than vanilla's fieldSystem->location->x/y, because
    // mid-map the location record holds where the player ENTERED the map, not
    // where they are standing now.
    //
    // Guarded on .active so it can never run twice: FollowMon_Clear only nulls
    // the map object pointer, it does not delete the object, so a second call
    // would orphan the first sprite.
    if (!fsys->followMon.active) {
        FollowMon_InitMapObject(fsys->mapObjectMan,
            GetPlayerXCoord(fsys->playerAvatar),
            GetPlayerYCoord(fsys->playerAvatar),
            fsys->location->direction,
            fsys->location->mapId);
    }

    return FALSE;
}

// ---------------------------------------------------------------------------
// scrcmd 0x26D (621), vanilla arm9 0x02047358,
// pret's ScrCmd_PlaceStarterBallsInElmsLab.
// ---------------------------------------------------------------------------
//
// Vanilla places the first `n` of three fixed props, with
//   n = 0 if FLAG_GOT_TM51_FROM_FALKNER (115), else
//   n = 1 if FLAG_MET_PASSERBY_BOY (153),      else
//   n = 2 if the party is non-empty,           else 3.
//
// Reproducing "one ball" by pre-setting flag 153 was rejected: that flag belongs
// to the passerby-boy event (script 0850) and to the "valuable Pokemon left"
// dialogue in script 0843, and it would still leave a ball on the machine after
// the player takes theirs. Replacing the command keeps those flags untouched and
// states the rule directly: one ball until it is taken, none afterwards.

// fsys->savedata is typed in FieldSystem, but 0x54 and 0x9C fall inside its
// unk50 filler. Offsets read straight off the vanilla routine's disassembly.
#define FSYS_MAP_PROP_ANIM_MANAGER(fsys) (*(void **)((u8 *)(fsys) + 0x54))
#define FSYS_MAP_PROP_MANAGER(fsys)      (*(void **)((u8 *)(fsys) + 0x9C))

#define MAP_PROP_POKE_BALL 0x8D  // prop model id, from the vanilla routine

BOOL LONG_CALL ScrCmd_PlaceStarterBallsInElmsLab(SCRIPTCONTEXT *ctx)
{
    FieldSystem *fsys = ctx->fsys;
    // ballCoords[0] of the vanilla table: (131, 0, 65) in fx32.
    static const VecFx32 ballPos = { 131 << FX32_SHIFT, 0, 65 << FX32_SHIFT };

    if (PokeParty_GetPokeCount(SaveData_GetPlayerPartyPtr(fsys->savedata)) == 0) {
        MapPropManager_LoadOne(FSYS_MAP_PROP_MANAGER(fsys), MAP_PROP_POKE_BALL, &ballPos, 0,
            FSYS_MAP_PROP_ANIM_MANAGER(fsys));
    }

    return FALSE;
}
