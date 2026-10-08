#include "config.h"

#include "constants/file.h"
#include "constants/item.h"
#include "bag.h"
#include "game_rules.h"
#include "pokemon.h"
#include "save.h"
#include "types.h"

/*
 * NO DUPLICATE ITEMS, always-resident half (overlay 129, loaded at boot; unlike overlay 131 it is still mapped during and
 * right after a battle, when the battle overlays occupy 0x023C4000+). The battle-end hook lives here because
 * sub_02050724 (arm9, called while the battle overlay is still loaded) tail-jumps to it. Every callee is arm9 or ov129.
 * The field-side give refusal (src/field/no_duplicate_items.c) shares Party_OtherMonHoldsItem.
 */
#define BATTLE_TYPE_DEBUG_BIT (1u << 31)

extern void LONG_CALL sub_0205239C(void *setup, void *fieldSystem);
extern int LONG_CALL PokeParty_GetPokeCount(struct Party *party);

/* TRUE when a party member other than slot excludeSlot (-1 for none) holds item. ITEM_NONE never clashes. */
BOOL LONG_CALL Party_OtherMonHoldsItem(struct Party *party, u16 item, int excludeSlot)
{
    int count = PokeParty_GetPokeCount(party);

    if (item == ITEM_NONE) {
        return FALSE;
    }
    for (int i = 0; i < count; i++) {
        if (i != excludeSlot) {
            struct PartyPokemon *mon = Party_GetMonByIndex(party, i);
            if ((u16)GetMonData(mon, MON_DATA_HELD_ITEM, NULL) == item) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

void NoDuplicateItems_BattleEnd(u32 *setup, void *fieldSystem)
{
    void *saveData;
    struct Party *party;
    int count;

    if (*setup & BATTLE_TYPE_DEBUG_BIT) {
        return;
    }
    sub_0205239C(setup, fieldSystem);
    if (!GameRule_IsEnabled(RULE_NO_DUPLICATE_ITEMS)) {
        return;
    }
    saveData = SaveBlock2_get();
    party = SaveData_GetPlayerPartyPtr(saveData);
    count = PokeParty_GetPokeCount(party);
    for (int i = 1; i < count; i++) {
        struct PartyPokemon *mon = Party_GetMonByIndex(party, i);
        u16 item = (u16)GetMonData(mon, MON_DATA_HELD_ITEM, NULL);
        u32 none = ITEM_NONE;

        if (item == ITEM_NONE) {
            continue;
        }
        for (int j = 0; j < i; j++) {
            if ((u16)GetMonData(Party_GetMonByIndex(party, j), MON_DATA_HELD_ITEM, NULL) == item) {
                if (Bag_AddItem(Sav2_Bag_get(saveData), item, 1, HEAPID_MAIN_HEAP)) {
                    SetMonData(mon, MON_DATA_HELD_ITEM, &none);
                }
                break;
            }
        }
    }
}
