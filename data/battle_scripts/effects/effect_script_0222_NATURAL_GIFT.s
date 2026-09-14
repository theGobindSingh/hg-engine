#include "constants/battle_constants.h"
.include "battle_commands.inc"

.data

_000:
    CalcNaturalGiftParams _006
    CalcCrit 
    CalcDamage 
    RemoveItem BATTLER_CATEGORY_ATTACKER
    End 

_006:
    UpdateVar OPCODE_FLAG_ON, BSCRIPT_VAR_MOVE_STATUS_FLAGS, MOVE_STATUS_FAILED
    End 
