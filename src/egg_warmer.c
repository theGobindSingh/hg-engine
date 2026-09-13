#include "../include/constants/ability.h"
#include "../include/constants/species.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/script.h"
#include "../include/types.h"

#ifdef IMPLEMENT_EGG_WARMER

/**
 *  @brief how many egg cycles a single step should burn off
 *
 *  Full replacement for the vanilla arm9 routine at 0206CB34, whose only caller
 *  is HandleDaycareStep (0206CD1C).  Vanilla returns 2 when any non-egg party
 *  member has Magma Armor or Flame Body, otherwise 1.
 *
 *  IMPLEMENT_EGG_WARMER adds the caretaker rule on top: a Chansey or Blissey
 *  *leading* the party (slot 0, not an egg) warms eggs exactly as Flame Body
 *  does.  Happiny deliberately does not count - it is the baby the player
 *  starts with, and the warming is the reward for raising it.  The result is
 *  still capped at 2, so nothing stacks.
 *
 *  @param party the player's party
 *  @return egg cycles to subtract this step, 1 or 2
 */
u8 GetEggCyclesToSubtract(struct Party *party)
{
    int i;

    for (i = 0; i < party->count; i++) {
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);

        if (GetMonData(mon, MON_DATA_SANITY_IS_EGG, NULL) != 0) {
            continue;
        }

        u32 ability = GetMonData(mon, MON_DATA_ABILITY, NULL);

        if (ability == ABILITY_MAGMA_ARMOR || ability == ABILITY_FLAME_BODY) {
            return 2;
        }
    }

    if (party->count > 0) {
        struct PartyPokemon *lead = Party_GetMonByIndex(party, 0);

        if (GetMonData(lead, MON_DATA_SANITY_IS_EGG, NULL) == 0) {
            u32 species = GetMonData(lead, MON_DATA_SPECIES, NULL);

            if (species == SPECIES_CHANSEY || species == SPECIES_BLISSEY) {
                return 2;
            }
        }
    }

    return 1;
}

#endif
