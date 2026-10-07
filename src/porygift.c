#include "../include/constants/item.h"
#include "../include/constants/moves.h"
#include "../include/constants/species.h"
#include "../include/game_rules.h"
#include "../include/npc_trade.h"
#include "../include/pokemon.h"
#include "../include/save.h"
#include "../include/types.h"

/* PORYGIFT debug rule: builds a Lv. 100 Porygon (31 IVs, 252 Speed / 252 Sp. Atk EVs, Tri Attack / Ice Beam /
 * Thunderbolt / Recover at full PP) and adds it to the party. Lives in ov129 (ov131 cannot be linked from ov129; see game-mode-select docs).
 * Returns TRUE when added, FALSE when the party was full. saveData = SaveBlock2_get(). Met data as retail ScrCmd_GiveMon (pret pokeheartgold src/scrcmd_party.c:20,32): the current map's mapsec and encounter type 24. */
BOOL LONG_CALL Porygift_Give(FieldSystem *fsys, void *saveData, int heapId)
{
    static const u16 moves[4] = { MOVE_TRI_ATTACK, MOVE_ICE_BEAM, MOVE_THUNDERBOLT, MOVE_RECOVER };
    struct Party *party = SaveData_GetPlayerPartyPtr(saveData);
    struct PartyPokemon *mon = AllocMonZeroed(heapId);
    u32 value;
    BOOL result;

    ZeroMonData(mon);
    PokeParaSet(mon, SPECIES_PORYGON, 100, MAX_IVS, FALSE, 0, 0, 0);
    sub_020720FC(mon, Sav2_PlayerData_GetProfileAddr(saveData), ITEM_POKE_BALL, MapHeader_GetMapSec(fsys->location->mapId), 24, heapId);

    value = 252;
    SetMonData(mon, MON_DATA_SPEED_EV, &value);
    SetMonData(mon, MON_DATA_SPATK_EV, &value);

    for (u32 i = 0; i < 4; i++) {
        value = moves[i];
        SetMonData(mon, MON_DATA_MOVE1 + i, &value);
        value = GetMoveMaxPP(moves[i], 0);
        SetMonData(mon, MON_DATA_MOVE1PP + i, &value);
        value = 0;
        SetMonData(mon, MON_DATA_MOVE1PPUP + i, &value);
    }

    RecalcPartyPokemonStats(mon);
    value = GetMonData(mon, MON_DATA_MAXHP, NULL);
    SetMonData(mon, MON_DATA_HP, &value);

    result = PokeParty_Add(party, mon);
    if (result) {
        UpdatePokedexWithReceivedSpecies(saveData, mon);
    }
    sys_FreeMemoryEz(mon);
    return result;
}
