#ifndef GAME_MODE_SELECT_UI_H
#define GAME_MODE_SELECT_UI_H

#include "types.h"
#include "message.h"

/* Minimal view of OakSpeechData (overlay 53, heap 0x50, size 0x180). Every offset is proven by an ldr/str
 * immediate in the retail overlay (see docs/game-mode-select.md section 1) and asserted in the UI overlay. */
struct OakSpeechDataView {
    u32 heapID;        /* +0x00 */
    void *saveData;    /* +0x04 */
    u8 pad08[4];       /* +0x08 options */
    u32 state;         /* +0x0C */
    u8 pad10[8];       /* +0x10 */
    void *bgConfig;    /* +0x18 */
    u8 pad1C[0x100 - 0x1C];
    MsgData *msgData;  /* +0x100 */
    u8 pad104[0x178 - 0x104];
    void *yesnoMenu;   /* +0x178 */
};

/* The Game Mode Select UI lives in its own overlay (id 152), loaded at the start of the screen and unloaded at
 * its end. Its entry function is first in the image. */
#define GMS_UI_OVERLAY_ID   152
#define GMS_UI_OVERLAY_BASE 0x023C4000

#include "game_rules.h"

/* ov129 data symbols cannot be linked from another overlay (scripts/generate_ld.py gives every symbol the Thumb bit,
 * so a data symbol comes out odd), so ov129 hands the tables over by pointer. */
struct GmsServices {
    const struct GameRuleDef *rules;
    const struct GamePresetDef *presets;
};

/* op 0: create the screen; op 1: run one frame (returns TRUE once the screen is finished); op 2: destroy. */
typedef BOOL (*GmsUiEntry)(struct OakSpeechDataView *d, u32 op, const struct GmsServices *svc);

#endif // GAME_MODE_SELECT_UI_H
