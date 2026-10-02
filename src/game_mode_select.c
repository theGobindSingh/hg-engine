/*
 * Game Mode Select: a screen inserted into the retail Oak speech (overlay 53) right after the player taps
 * NO INFO NEEDED, while both screens are black and before retail state 44 runs. See
 * ~/Programming/james-game/docs/game-mode-select.md for the design and every proof of an address below.
 *
 * The hook (hooks: `0053 GMS_MainTaskHook 021E5A5C 1`, asm/game_mode_hooks.s) replaces Oak's single call to
 * OakSpeech_DoMainTask. GMS_DoMainTask runs the retail state machine unchanged except at the first frame of
 * state 44, where it runs our own machine instead; when ours finishes it hands state 44 back untouched.
 *
 * The screen itself lives in overlay 152 (src/individual/GameModeSelectUI.c): ov129 only holds this thin
 * wrapper, the rule/preset tables (game_rules.c) and the flag helpers, because ov129 is capped at 0x023E0000.
 */
#include "../include/types.h"

#include "../include/constants/file.h"
#include "../include/game_mode_select_ui.h"
#include "../include/game_rules.h"
#include "../include/overlay.h"

extern BOOL LONG_CALL ov53_OakSpeech_DoMainTask(void *data);

_Static_assert(__builtin_offsetof(struct OakSpeechDataView, state) == 0x0C, "state offset");

#define OAK_STATE_FIRST                  0
#define OAK_STATE_NO_INFO_NEEDED_FADE_IN 44

static const struct GmsServices sGmsServices = { gGameRules, gGamePresets };

static u8 sGmsStarted; /* the UI overlay is loaded and running */
static u8 sGmsDone;

BOOL GMS_DoMainTask(void *data)
{
    struct OakSpeechDataView *d = data;

    if (d->state == OAK_STATE_FIRST) {
        sGmsStarted = 0; /* first state of every Oak run */
        sGmsDone = 0;
    }
    if (d->state == OAK_STATE_NO_INFO_NEEDED_FADE_IN && !sGmsDone) {
        GmsUiEntry entry = (GmsUiEntry)(GMS_UI_OVERLAY_BASE | 1);
        if (!sGmsStarted) {
            HandleLoadOverlay(OVERLAY_GMS_UI, 2);
            sGmsStarted = 1;
            entry(d, 0, &sGmsServices);
        }
        if (entry(d, 1, &sGmsServices)) {
            entry(d, 2, &sGmsServices);
            UnloadOverlayByID(OVERLAY_GMS_UI);
            sGmsStarted = 0;
            sGmsDone = 1; /* state is still 44: the retail machine takes over next frame */
        }
        return FALSE;
    }
    return ov53_OakSpeech_DoMainTask(data);
}
