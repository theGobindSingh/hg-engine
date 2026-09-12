#include "../include/repel.h"

#include "../include/bag.h"
#include "../include/constants/file.h"
#include "../include/constants/item.h"
#include "../include/egg_caretaker.h"
#include "../include/item.h"

void Repel_SetCurrentType();

#if defined(IMPLEMENT_REUSABLE_REPELS) || defined(IMPLEMENT_EGG_CARETAKER_CUE)
/**
 *  @brief the per-step field event hooked over PlayerStepEvent_RepelCounterDecrement
 *
 *  Vanilla only counts the repel down and shows "the repellent's effect wore
 *  off" (common script 2022).  IMPLEMENT_REUSABLE_REPELS replaces that message
 *  with the reuse prompt (2072), and IMPLEMENT_EGG_CARETAKER_CUE bolts the
 *  caretaker cue on afterwards.  Either flag alone is enough to install the
 *  hook, and with the repel flag off the vanilla repel behaviour is kept here
 *  verbatim so nothing is lost.
 *
 *  @param saveData the save
 *  @param fieldSystem the field system
 *  @return TRUE if a script was queued for this step
 */
bool32 PlayerStepEvent_RepelCounterDecrement(SaveData *saveData, FieldSystem *fieldSystem)
{
    void *roamerSaveData = EncDataSave_GetSaveDataPtr(saveData);
    u8 *repel_addr = SaveData_GetRepelPtr(roamerSaveData);

    if (*repel_addr != 0) {
        (*repel_addr)--;

        if (*repel_addr == 0) {
#ifdef IMPLEMENT_REUSABLE_REPELS
            BAG_DATA *bag = Sav2_Bag_get(saveData);
            u16 currentRepel = Repel_GetMostRecent();
            if (Bag_HasItem(bag, currentRepel, 1, HEAPID_WORLD)) {
                EventSet_Script(fieldSystem, 2072, NULL);
            } else {
                EventSet_Script(fieldSystem, 2022, NULL);
            }
#else
            EventSet_Script(fieldSystem, 2022, NULL);
#endif

            return TRUE;
        }
    }

#ifdef IMPLEMENT_EGG_CARETAKER_CUE
    if (EggCaretaker_TryLeadChangeCue(saveData, fieldSystem)) {
        return TRUE;
    }
#endif

    return FALSE;
}
#endif

#ifdef IMPLEMENT_REUSABLE_REPELS
u16 ALIGN4 CurrentRepelType = 0;

u16 Repel_GetMostRecent()
{
    Repel_SetCurrentType();
    return CurrentRepelType;
}

BOOL Repel_Use(u16 item_id, u32 heap_id)
{
    SaveData *saveData = SaveBlock2_get();
    void *roamerSaveData = EncDataSave_GetSaveDataPtr(saveData);
    u8 *repel_addr = SaveData_GetRepelPtr(roamerSaveData);

    BAG_DATA *bag = Sav2_Bag_get(saveData);

    item_id = Repel_GetMostRecent();

    if (Bag_TakeItem(bag, item_id, 1, heap_id)) {
        *repel_addr = Repel_GetSteps(item_id, heap_id);
        return TRUE;
    }

    return FALSE;
}

u8 Repel_GetSteps(u16 item_id, u32 heap_id)
{
    return GetItemData(item_id, ITEM_PARAM_HOLD_EFFECT_PARAM, heap_id);
}
#endif

void Repel_SetCurrentType()
{
#ifdef IMPLEMENT_REUSABLE_REPELS
    u16 item_id = 0;
    BAG_DATA *bag = Sav2_Bag_get(SaveBlock2_get());
    if (Bag_HasItem(bag, ITEM_MAX_REPEL, 1, HEAPID_MAIN_HEAP)) {
        item_id = ITEM_MAX_REPEL;
    } else if (Bag_HasItem(bag, ITEM_SUPER_REPEL, 1, HEAPID_MAIN_HEAP)) {
        item_id = ITEM_SUPER_REPEL;
    } else {
        item_id = ITEM_REPEL;
    }

    CurrentRepelType = item_id;
#endif
}
