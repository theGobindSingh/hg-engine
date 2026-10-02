#include "../include/message.h"

#include "../include/config.h"
#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/types.h"
#include "../include/game_rules.h"

/* james-game 0.5.2: with the Super Rare Candy rule ON the Rare Candy shows the appended "Super Rare Candy" lines
 * (archive 222 / 831 / 832 / 833, index 2686 / 537); 0.5.4 generalises it to the SUPER ITEMS table (src/game_rules.c). Rule OFF = byte-identical to before. */

void BufferOffsetItemLineFromFile(MessageFormat *msgFmt, u32 fieldno, u32 itemId, u32 fileId)
{
    MsgData *msgData = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, ARC_MSG_DATA, fileId, msgFmt->heapId);
    u32 offset = ITEM_MSG_OFFSET(itemId);
    const struct SuperItemDef *super = SuperItems_Active((u16)itemId);
    if (super != NULL) {
        offset = (fileId == MSG_DATA_ITEM_NAME_PLURAL_GEN4) ? super->msg832
               : (fileId == MSG_DATA_ITEM_GIVE_ITEM_GEN4)   ? super->msg833
                                                            : super->msg831;
    }
    if (msgData != NULL) {
        ReadMsgDataIntoString(msgData, offset, msgFmt->buffer);
        SetStringAsPlaceholder(msgFmt, fieldno, msgFmt->buffer, NULL);
        DestroyMsgData(msgData);
    }
}

void LONG_CALL BufferItemNameWithIndefArticle(MessageFormat *msgFmt, u32 fieldno, u32 itemId)
{
    enum ItemGeneration gen = ITEM_GENERATION(itemId);
    u32 fileId = (gen == CUSTOM)
        ? MSG_DATA_ITEM_NAME_ARTICLE_CUSTOM
        : MSG_DATA_ITEM_FILE(MSG_DATA_ITEM_NAME_ARTICLE_GEN4, gen);
    BufferOffsetItemLineFromFile(msgFmt, fieldno, itemId, fileId);
}

void LONG_CALL BufferItemNamePlural(MessageFormat *msgFmt, u32 fieldno, u32 itemId)
{
    enum ItemGeneration gen = ITEM_GENERATION(itemId);
    u32 fileId = (gen == CUSTOM)
        ? MSG_DATA_ITEM_NAME_PLURAL_CUSTOM
        : MSG_DATA_ITEM_FILE(MSG_DATA_ITEM_NAME_PLURAL_GEN4, gen);
    BufferOffsetItemLineFromFile(msgFmt, fieldno, itemId, fileId);
}

void LONG_CALL BufferItemNameGiveItem(MessageFormat *msgFmt, u32 fieldno, u32 itemId)
{
    enum ItemGeneration gen = ITEM_GENERATION(itemId);
    u32 fileId = (gen == CUSTOM)
        ? MSG_DATA_ITEM_GIVE_ITEM_CUSTOM
        : MSG_DATA_ITEM_FILE(MSG_DATA_ITEM_GIVE_ITEM_GEN4, gen);
    BufferOffsetItemLineFromFile(msgFmt, fieldno, itemId, fileId);
}

/* james-game 0.5.2: every reader of the item-name archive (222) funnels through retail ReadMsgDataIntoString /
 * NewString_ReadMsgData (arm9 0x0200BB6C / 0x0200BBA0) - the Bag (overlay 15), shops, party menu... load their own
 * MsgData for archive 222 and never touch BufferItemName. Both are re-implemented from retail (disassembled, pret
 * src/msgdata.c) with one addition: archive 222, a SUPER ITEMS row reads its replacement index while the rule is ON.
 * Retail layout: struct MsgData { u16 type; u16 heapId; u16 narcId; u16 fileId; union { void *direct; void *lazy; }; }. */
struct MsgDataLayout {
    u16 type;
    u16 heapId;
    u16 narcId;
    u16 fileId;
    void *data;
};
_Static_assert(__builtin_offsetof(struct MsgDataLayout, fileId) == 6, "MsgData.fileId");
_Static_assert(__builtin_offsetof(struct MsgDataLayout, data) == 8, "MsgData.data");

#define ITEM_NAME_FILE 222

static u32 MsgData_ApplyItemNameRule(const struct MsgDataLayout *msgData, u32 msgNo)
{
    if (msgData->fileId == ITEM_NAME_FILE && msgData->narcId == ARC_MSG_DATA) {
        const struct SuperItemDef *super = SuperItems_Active((u16)msgNo);
        if (super != NULL) {
            return super->msg222;
        }
    }
    return msgNo;
}

void LONG_CALL ReadMsgDataIntoString_hook(MsgData *msgData, s32 msgNo, String *dest)
{
    struct MsgDataLayout *m = (struct MsgDataLayout *)msgData;
    u32 no = MsgData_ApplyItemNameRule(m, (u32)msgNo);
    if (m->type == MSGDATA_LOAD_DIRECT) {
        ReadMsgData_ExistingTable_ExistingString(m->data, no, dest);
    } else if (m->type == MSGDATA_LOAD_LAZY) {
        ReadMsgData_ExistingNarc_ExistingString(m->data, m->fileId, no, m->heapId, dest);
    }
}

String *LONG_CALL NewString_ReadMsgData_hook(MsgData *msgData, s32 msgNo)
{
    struct MsgDataLayout *m = (struct MsgDataLayout *)msgData;
    u32 no = MsgData_ApplyItemNameRule(m, (u32)msgNo);
    if (m->type == MSGDATA_LOAD_DIRECT) {
        return ReadMsgData_ExistingTable_NewString(m->data, no, m->heapId);
    } else if (m->type == MSGDATA_LOAD_LAZY) {
        return ReadMsgData_ExistingNarc_NewString(m->data, m->fileId, no, m->heapId);
    }
    return NULL;
}
