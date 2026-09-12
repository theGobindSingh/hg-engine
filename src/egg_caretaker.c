#include "../include/egg_caretaker.h"

#include "../include/constants/species.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"
#include "../include/types.h"

#ifdef IMPLEMENT_EGG_CARETAKER_CUE

/**
 *  @brief does the party hold at least one Egg?
 *
 *  @param party the player's party
 *  @return TRUE if any slot holds an Egg
 */
static bool32 PartyHasEgg(struct Party *party)
{
    int i;

    for (i = 0; i < party->count; i++) {
        if (GetMonData(Party_GetMonByIndex(party, i), MON_DATA_IS_EGG, NULL) != 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/**
 *  @brief fire the caretaker cue on the first step after the follower changed
 *
 *  Called once per player step from PlayerStepEvent_RepelCounterDecrement.
 *
 *  The follower species is the thing being watched rather than the party lead,
 *  because the game itself only swaps the follower in FollowMon_ChangeMon /
 *  FollowMon_InitMapObject, which run on a map load or a "restore overworld"
 *  transition (battle end, blackout, egg hatch).  Reordering the party in the
 *  menu does not change the sprite, so watching the party lead would play the
 *  cue over the wrong Pokemon.  Watching followMon.species means the cue lands
 *  on the first step after the Chansey or Blissey is actually walking behind
 *  the player, which is what the player sees.
 *
 *  The previous species is kept in EGG_CARETAKER_CUE_VAR and is never cleared
 *  to 0, so walking into a building (or any state with no follower) does not
 *  re-arm the cue.  A plain map load with an unchanged follower does not fire
 *  it either, because the species has not changed.
 *
 *  @param saveData the save
 *  @param fieldSystem the field system
 *  @return TRUE if a script was queued, which stops the caller doing more
 */
bool32 EggCaretaker_TryLeadChangeCue(SaveData *saveData, FieldSystem *fieldSystem)
{
    u16 cur;
    u16 prev;

    if (fieldSystem->followMon.active == 0) {
        return FALSE;
    }

    cur = (u16)(fieldSystem->followMon.species & 0x7FF);

    if (cur == 0) {
        return FALSE;
    }

    prev = VarGet(fieldSystem, EGG_CARETAKER_CUE_VAR);

    if (prev == cur) {
        return FALSE;
    }

    VarSet(fieldSystem, EGG_CARETAKER_CUE_VAR, cur);

    // First time we have ever seen a follower on this save: just remember it.
    if (prev == 0) {
        return FALSE;
    }

    if (cur != SPECIES_CHANSEY && cur != SPECIES_BLISSEY) {
        return FALSE;
    }

    if (!PartyHasEgg((struct Party *)SaveData_GetPlayerPartyPtr(saveData))) {
        return FALSE;
    }

    EventSet_Script(fieldSystem, EGG_CARETAKER_CUE_SCRIPT, NULL);

    return TRUE;
}

#endif
