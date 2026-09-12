#ifndef EGG_CARETAKER_H
#define EGG_CARETAKER_H

#include "types.h"

#include "pokemon.h"
#include "save.h"
#include "script.h"

// Common script 3 entry ids, as passed to EventSet_Script.
#define EGG_CARETAKER_CUE_SCRIPT  2074
#define EGG_CARETAKER_TALK_SCRIPT 2075

bool32 EggCaretaker_TryLeadChangeCue(SaveData *saveData, FieldSystem *fieldSystem);

#endif
