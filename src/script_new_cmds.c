#include "../include/config.h"
#include "../include/constants/file.h"
#include "../include/pokemon.h"
#include "../include/repel.h"
#include "../include/roamer.h"
#include "../include/save.h"
#include "../include/script.h"
#include "../include/types.h"

#define SCRIPT_NEW_CMD_REPEL_USE 0
#define SCRIPT_NEW_CMD_GET_PARTY_IVS 1

#define SCRIPT_NEW_CMD_MAX 256

int LONG_CALL PokeParty_GetPokeCount(void *party);

BOOL Script_RunNewCmd(SCRIPTCONTEXT *ctx)
{
    u8 sw = ScriptReadByte(ctx);
    u16 UNUSED arg0 = ScriptReadHalfword(ctx);

    switch (sw) {
    case SCRIPT_NEW_CMD_REPEL_USE:;
#ifdef IMPLEMENT_REUSABLE_REPELS
        u16 most_recent_repel = Repel_GetMostRecent();
        SetScriptVar(arg0, most_recent_repel);
        Repel_Use(most_recent_repel, HEAPID_MAIN_HEAP);
#endif
        break;

    case SCRIPT_NEW_CMD_GET_PARTY_IVS:;
        u16 basevar = ScriptReadHalfword(ctx);
        u16 slot;
        struct Party *party = SaveData_GetPlayerPartyPtr(ctx->fsys->savedata);
        int partyCount = PokeParty_GetPokeCount(party);
        struct PartyPokemon *mon = NULL;

        // The script side has no clean way to know the follower's party
        // index, so arg0 is interpreted three ways:
        //   - 0xFFFF: the sentinel, asking us to resolve it ourselves: the
        //     first party slot that is neither an Egg nor fainted, the same
        //     rule the game uses to pick the following Pokemon.
        //   - >= 0x4000: a script var id, whose value is the party slot.
        //   - anything else: a literal party index, used as-is.
        // After that, slot is either a valid party index (< partyCount),
        // used as-is, or still out of range, which falls back to the same
        // follower-rule resolution above.
        //
        // Output, eight consecutive vars from basevar:
        //   +0..+5  HP / Attack / Defense / Sp. Atk / Sp. Def / Speed IVs
        //   +6      species id
        //   +7      the party index actually used
        // If no usable Pokemon is found, all eight are set to 0.
        if (arg0 == 0xFFFF) {
            slot = 0xFFFF;
        } else if (arg0 >= 0x4000) {
            slot = GetScriptVar(arg0);
        } else {
            slot = arg0;
        }

        u16 resolvedSlot = 0;

        if (slot < partyCount) {
            mon = Party_GetMonByIndex(party, slot);
            resolvedSlot = slot;
        } else {
            int i;
            for (i = 0; i < partyCount; i++) {
                struct PartyPokemon *candidate = Party_GetMonByIndex(party, i);
                if (GetMonData(candidate, MON_DATA_IS_EGG, NULL) == 0 &&
                    GetMonData(candidate, MON_DATA_HP, NULL) > 0) {
                    mon = candidate;
                    resolvedSlot = (u16)i;
                    break;
                }
            }
        }

        if (mon == NULL) {
            for (int i = 0; i < 8; i++) {
                SetScriptVar(basevar + i, 0);
            }
            break;
        }

        SetScriptVar(basevar + 0, GetMonData(mon, MON_DATA_HP_IV, NULL));
        SetScriptVar(basevar + 1, GetMonData(mon, MON_DATA_ATK_IV, NULL));
        SetScriptVar(basevar + 2, GetMonData(mon, MON_DATA_DEF_IV, NULL));
        SetScriptVar(basevar + 3, GetMonData(mon, MON_DATA_SPATK_IV, NULL));
        SetScriptVar(basevar + 4, GetMonData(mon, MON_DATA_SPDEF_IV, NULL));
        SetScriptVar(basevar + 5, GetMonData(mon, MON_DATA_SPEED_IV, NULL));
        SetScriptVar(basevar + 6, GetMonData(mon, MON_DATA_SPECIES, NULL));
        // basevar+7 is the party index actually used.  It lets the script buffer
        // the name with buffer_mon_species_name (scrcmd 193, which takes a party
        // position), the one attested pairing for a {STRVAR_1 1, slot, 0} token.
        SetScriptVar(basevar + 7, resolvedSlot);
        break;

    default:
        break;
    }

    return FALSE;
}

#ifdef EXPAND_ROAMERS
BOOL LONG_CALL ScrCmd_CreateRoamer(SCRIPTCONTEXT *ctx)
{
    u8 roamerNo = ScriptReadByte(ctx);
    Save_CreateRoamerByID(ctx->fsys->savedata, roamerNo);
    return FALSE;
}
#endif // EXPAND_ROAMERS
