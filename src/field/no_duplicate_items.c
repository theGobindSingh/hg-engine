#include "config.h"

#include "constants/file.h"
#include "constants/item.h"
#include "game_mode_select.h"
#include "bag.h"
#include "game_rules.h"
#include "message.h"
#include "pokemon.h"
#include "save.h"
#include "types.h"

/*
 * NO DUPLICATE ITEMS (RULE_NO_DUPLICATE_ITEMS, flag 2315): no two Pokemon in the party may hold the same item.
 * Lives in overlay 131 (loaded together with field overlay 1, so resident whenever the party menu is opened from the
 * field and after every field battle; see gLinkedOverlayList).
 *
 *  1. Give refusal. PartyMenu_Subtask_GiveItemToMon (arm9 0x0207C400, pret src/party_menu.c:2456) is reached through two
 *     Thumb `bl`s at 0x02079120 (Bag GIVE, context 9) and 0x0207941C (party ITEM > GIVE, context 10). Both are retargeted
 *     (`bl` hooks) to NoDuplicateItems_GiveItemToMon. Its first instructions load PC-relative literals, so a tail-jump
 *     hook (which overwrites the first 8 bytes and cannot resume the original) would not be byte-safe; a call-site
 *     retarget leaves the retail function untouched. Rule OFF, or no clash: tail call into the retail function.
 *     Clash: print the refusal in the same message window the retail function uses and return state 11
 *     (PARTY_MENU_STATE_PRINT_ITEM_SWAP_MESSAGE) like the Mail refusal; nothing is mutated.
 *  2. Battle-end dedupe: NoDuplicateItems_BattleEnd in src/no_duplicate_items.c (ov129, resident during battle end).
 *     (runs sub_0205239C as retail, then returns to the Bag every held item that an earlier party slot already holds).
 *     Moved to ov129: ov131 is unmapped while the battle overlay is loaded.
 *
 * PartyMenu offsets (pret include/party_menu.h, checked against the arm9 disassembly): args 0x654, msgData 0x7C0,
 * formattedStrBuf 0x7C8, partyMonIndex 0xC65; PartyMenuArgs.party +0x00, .itemId +0x28.
 */
#define PM_ARGS               0x654
#define PM_FORMATTED_STR_BUF  0x7C8
#define PM_PARTY_MON_INDEX    0xC65
#define PMARGS_PARTY          0x00
#define PMARGS_ITEM_ID        0x28
#define HEAPID_PARTY_MENU     12
#define STATE_PRINT_ITEM_SWAP_MESSAGE 11

extern int LONG_CALL PartyMenu_Subtask_GiveItemToMon(void *partyMenu);
extern BOOL LONG_CALL Party_OtherMonHoldsItem(struct Party *party, u16 item, int excludeSlot);
extern int LONG_CALL PokeParty_GetPokeCount(struct Party *party);

int NoDuplicateItems_GiveItemToMon(void *partyMenu)
{
    u8 *pm = (u8 *)partyMenu;
    u8 *args = *(u8 **)(pm + PM_ARGS);

    if (GameRule_IsEnabled(RULE_NO_DUPLICATE_ITEMS)
        && Party_OtherMonHoldsItem(*(struct Party **)(args + PMARGS_PARTY), *(u16 *)(args + PMARGS_ITEM_ID), pm[PM_PARTY_MON_INDEX])) {
        MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, ARC_MSG_DATA, 40, HEAPID_PARTY_MENU);
        ReadMsgDataIntoString(msgData, GMS_MSG_NO_DUPLICATE_ITEMS_REFUSAL, *(void **)(pm + PM_FORMATTED_STR_BUF));
        DestroyMsgData(msgData);
        PartyMenu_PrintMessageOnWindow34(partyMenu, -1, TRUE);
        return STATE_PRINT_ITEM_SWAP_MESSAGE;
    }
    return PartyMenu_Subtask_GiveItemToMon(partyMenu);
}

