/*
 * Game Mode Select: a screen inserted into the retail Oak speech (overlay 53) right after the player taps
 * NO INFO NEEDED, while both screens are black and before retail state 44 runs. See
 * ~/Programming/james-game/docs/game-mode-select.md for the design and every proof of an address below.
 *
 * The hook (hooks: `0053 GMS_MainTaskHook 021E5A5C 1`, asm/game_mode_hooks.s) replaces Oak's single call to
 * OakSpeech_DoMainTask. GMS_DoMainTask runs the retail state machine unchanged except at the first frame of
 * state 44, where it runs our own machine instead; when ours finishes it hands state 44 back untouched.
 */
#include "../include/types.h"

#include "../include/constants/sndseq.h"
#include "../include/game_mode_select.h"
#include "../include/game_rules.h"
#include "../include/message.h"
#include "../include/save.h"
#include "../include/sound.h"
#include "../include/system.h"
#include "../include/window.h"

/* ---------------------------------------------------------------------------------------------------------
 * Overlay 53 data and functions
 * ------------------------------------------------------------------------------------------------------ */

/* Minimal view of OakSpeechData (heap 0x50, size 0x180). Every offset is proven by an ldr/str immediate in
 * ov53.dis (see the design doc, section 1) and asserted below. */
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

_Static_assert(__builtin_offsetof(struct OakSpeechDataView, heapID) == 0x00, "heapID offset");
_Static_assert(__builtin_offsetof(struct OakSpeechDataView, saveData) == 0x04, "saveData offset");
_Static_assert(__builtin_offsetof(struct OakSpeechDataView, state) == 0x0C, "state offset");
_Static_assert(__builtin_offsetof(struct OakSpeechDataView, bgConfig) == 0x18, "bgConfig offset");
_Static_assert(__builtin_offsetof(struct OakSpeechDataView, msgData) == 0x100, "msgData offset");
_Static_assert(__builtin_offsetof(struct OakSpeechDataView, yesnoMenu) == 0x178, "yesnoMenu offset");

/* gSystem fields we read. The pret layout is asserted against hg-engine's own struct, and
 * simulatedInputs (+0x5C) is separately proven by the store at ov53 0x021E7ECC. */
_Static_assert(__builtin_offsetof(struct System, newKeys) == 0x48, "newKeys offset");
_Static_assert(__builtin_offsetof(struct System, simulatedInputs) == 0x5C, "simulatedInputs offset");
_Static_assert(__builtin_offsetof(struct System, touchX) == 0x60, "touchX offset");
_Static_assert(__builtin_offsetof(struct System, touchY) == 0x62, "touchY offset");
_Static_assert(__builtin_offsetof(struct System, touchNew) == 0x64, "touchNew offset");

extern BOOL LONG_CALL ov53_OakSpeech_DoMainTask(void *data);
extern BOOL LONG_CALL ov53_OakSpeech_PrintDialogMsg(struct OakSpeechDataView *data, int msgNum, int waitMode);
extern BOOL LONG_CALL ov53_OakSpeech_PrintAndFadeCenteredFullScreenText(struct OakSpeechDataView *data, int msgNum, int kind);
extern void LONG_CALL ov53_OakSpeech_TouchToAdvanceButtonAction(struct OakSpeechDataView *data, int action);
extern void LONG_CALL ov53_OakSpeech_SetButtonTutorialScreenLayout(struct OakSpeechDataView *data, int layout);
extern void LONG_CALL ov53_021E67C4(struct OakSpeechDataView *data, int idx);
extern void LONG_CALL ov53_021E6824(struct OakSpeechDataView *data, int idx);
extern void LONG_CALL ov53_OakSpeechYesNo_SetBackgroundPalette(void *yesnoMenu, int palette);
extern void LONG_CALL ov53_OakSpeechYesNo_Start(void *yesnoMenu, int msgBank, int msgIdYes, int msgIdNo);
extern int LONG_CALL ov53_OakSpeechYesNo_Main(void *yesnoMenu);
extern void LONG_CALL ToggleBgLayer(u32 layer, u32 on);
extern void LONG_CALL BeginNormalPaletteFade(u32 pattern, u32 typeTop, u32 typeBottom, u32 colour, u32 duration, u32 framesPer, u32 heapId);

#define BG_MAIN_0 0
#define BG_MAIN_3 3
#define BG_SUB_0  4
#define BG_SUB_1  5
#define BG_SUB_2  6
#define BG_SUB_3  7

#define OAK_STATE_NO_INFO_NEEDED_FADE_IN 44

#define YESNO_RESPONSE_YES 1
#define YESNO_RESPONSE_NO  2

#define GMS_TEXT_SPEED_INSTANT 0
#define GMS_TEXT_COLOR(fg, sh, bg) ((((fg) & 0xFF) << 16) | (((sh) & 0xFF) << 8) | ((bg) & 0xFF))

/* ---------------------------------------------------------------------------------------------------------
 * Layout (tiles; the lower screen is 32x24 tiles = 256x192 px)
 * ------------------------------------------------------------------------------------------------------ */

#define GMS_PAL         14   /* sub BG palette holding retail's font colours */
#define GMS_FIRST_TILE  0xC0 /* above every retail SUB_0 window (<= 0xB0 incl. the yes/no pair) */
#define GMS_MAX_WIN     (RULE_COUNT * 2 + 2 > GMS_NUM_MODES ? RULE_COUNT * 2 + 2 : GMS_NUM_MODES)

#define GMS_NUM_MODES   4    /* Over Easy, Softboiled, Hardboiled, Scrambled = GMS_MODE_OVER_EASY..SCRAMBLED */
#define MODE_BTN_X      4
#define MODE_BTN_W      24
#define MODE_BTN_H      3
#define MODE_BTN_Y(i)   (2 + 5 * (i))

#define ROW_NAME_X      1
#define ROW_NAME_W      18
#define ROW_SWITCH_X    20
#define ROW_SWITCH_W    7
#define ROW_H           3
#define ROW_Y(i)        (2 + 4 * (i))
#define FOOTER_Y        18
#define FOOTER_CONFIRM_X 2
#define FOOTER_CANCEL_X  17
#define FOOTER_W        12

_Static_assert(RULE_COUNT >= 1 && RULE_COUNT <= 4, "the rule list draws at most 4 rows without paging");
_Static_assert(RULE_COUNT * 2 + 2 <= GMS_MAX_WIN, "window slots");
_Static_assert(GMS_MSG_CANCEL == GMS_MSG_CONFIRM + 1, "footer plates index their labels by position");
_Static_assert(FLAG_GMS_MODE_BIT1 == FLAG_GMS_MODE_BIT0 + 1 && FLAG_GMS_MODE_BIT2 == FLAG_GMS_MODE_BIT0 + 2, "mode bit flags are consecutive");

/* Palette 14 colour indices (retail font Pal0). */
#define COL_DARK       1
#define COL_LIGHT_GREY 2
#define COL_BLUE       8
#define COL_PINK       10
#define COL_WHITE      15

#define FILL_NORMAL    COL_WHITE
#define FILL_HIGHLIGHT COL_BLUE
#define FILL_ON        COL_PINK
/* Greyed (preset preview): grey plate, dark text. */
#define FILL_GREYED    COL_LIGHT_GREY

enum GmsScreen {
    SCR_MODES = 0, /* Screen A */
    SCR_RULES      /* Screens B (editable, Scrambled) and C (preview) */
};

enum GmsPhase {
    PH_ENTER = 0,
    PH_ENTER_WAIT,
    PH_BUILD,
    PH_PROMPT_IN,
    PH_INPUT,
    PH_PROMPT_OUT,
    PH_DLG_PRINT,
    PH_DLG_WAIT,
    PH_SURE_YESNO_START,
    PH_SURE_YESNO,
    PH_LEAVE_FADE,
    PH_LEAVE_WAIT
};

/* Scalars first and word-sized: Thumb loads with a small immediate offset are much shorter, and win[] is last. */
struct GmsCtx {
    MsgData *msg40;      /* archive 40 (Game Mode Select strings) */
    MsgData *retailMsg;  /* Oak's own archive 219 MsgData, restored on every return */
    u32 nWin;
    u32 phase;
    u32 afterOut;        /* phase to enter once the prompt has faded out */
    u32 screen;
    u32 mode;            /* GmsModeId shown on SCR_RULES */
    u32 cursor;          /* SCR_MODES: 0-3; SCR_RULES: row index, or RULE_COUNT for the footer */
    u32 footerSel;       /* SCR_RULES footer: 0 = Confirm, 1 = Cancel */
    u32 padMode;         /* highlight visible (d-pad driven) */
    u32 promptShown;     /* top prompt currently faded in (so the next call fades it out) */
    u32 dlgIsSure;
    u32 subState;
    u32 promptMsg;       /* archive 40 index of the current top prompt */
    u32 dlgMsg;
    u32 nextTile;
    u32 ruleValues;      /* bit i = rule i ON */
    struct Window win[GMS_MAX_WIN];
};

struct GmsIn {
    u32 keys;
    u8 touch;
    u16 x;
    u16 y;
};

static struct GmsCtx sGmsCtx;
static struct GmsCtx *sGms; /* NULL when idle; the overlay's .bss is not trusted, so everything is reset explicitly */
static u8 sGmsDone;

/* ---------------------------------------------------------------------------------------------------------
 * Windows and plates
 * ------------------------------------------------------------------------------------------------------ */

static void __attribute__((noinline)) GmsClear(struct OakSpeechDataView *d, u32 bg)
{
    BgClearTilemapBufferAndCommit(d->bgConfig, bg);
}

static void __attribute__((noinline)) GmsOn(u32 bg)
{
    ToggleBgLayer(bg, 1);
}

static void __attribute__((noinline)) GmsFade(struct OakSpeechDataView *d, u32 in)
{
    BeginNormalPaletteFade(0, in, in, 0, 6, 1, d->heapID);
}

static void GmsRemoveWindows(struct GmsCtx *c)
{
    for (u32 i = 0; i < c->nWin; i++) {
        RemoveWindow(&c->win[i]);
    }
    c->nWin = 0;
    c->nextTile = GMS_FIRST_TILE;
}

static void GmsAddWindow(struct GmsCtx *c, struct OakSpeechDataView *d, u32 x, u32 y, u32 w, u32 h)
{
    WindowTemplate t;
    t.bgId = BG_SUB_0;
    t.left = x;
    t.top = y;
    t.width = w;
    t.height = h;
    t.palette = GMS_PAL;
    t.baseTile = c->nextTile;
    AddWindow(d->bgConfig, &c->win[c->nWin], &t);
    c->nWin++;
    c->nextTile += w * h;
}

static void GmsPlate(struct GmsCtx *c, struct OakSpeechDataView *d, u32 widx, u32 msg, u32 fg, u32 sh, u32 fill)
{
    struct Window *w = &c->win[widx];
    String *s = String_New(0x80, d->heapID);
    ReadMsgDataIntoString(c->msg40, msg, s);
    FillWindowPixelBuffer(w, fill);
    u32 x = FontID_String_GetCenterAlignmentX(4, s, 0, w->width * 8);
    AddTextPrinterParameterizedWithColor(w, 4, s, x, 4, GMS_TEXT_SPEED_INSTANT, GMS_TEXT_COLOR(fg, sh, fill), NULL);
    CopyWindowToVram(w);
    String_Delete(s);
}

static BOOL GmsRulesEditable(const struct GmsCtx *c)
{
    return gGamePresets[c->mode].editable;
}

static u32 GmsTopMsg(const struct GmsCtx *c)
{
    return c->screen == SCR_MODES ? GMS_MSG_TOP_MAIN : gGamePresets[c->mode].topMsg;
}

/* Window slots on SCR_RULES: name 2*i, switch 2*i+1, then Confirm, Cancel. */
#define WIN_RULE_NAME(i)   (2 * (i))
#define WIN_RULE_SWITCH(i) (2 * (i) + 1)
#define WIN_CONFIRM        (2 * RULE_COUNT)
#define WIN_CANCEL         (2 * RULE_COUNT + 1)

static void GmsDrawModes(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    for (u32 i = 0; i < GMS_NUM_MODES; i++) {
        u32 fill = (c->padMode && c->cursor == i) ? FILL_HIGHLIGHT : FILL_NORMAL;
        GmsPlate(c, d, i, gGamePresets[GMS_MODE_OVER_EASY + i].nameMsg, COL_DARK, COL_LIGHT_GREY, fill);
    }
}

static void GmsDrawRules(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    BOOL editable = GmsRulesEditable(c);
    u32 sh = editable ? COL_LIGHT_GREY : COL_WHITE;
    for (u32 i = 0; i < RULE_COUNT; i++) {
        BOOL on = (c->ruleValues >> i) & 1;
        u32 nameFill = FILL_GREYED;
        u32 swFill = FILL_GREYED;
        if (c->padMode && c->cursor == i) {
            nameFill = FILL_HIGHLIGHT;
        } else if (editable) {
            nameFill = FILL_NORMAL;
        }
        if (editable) {
            swFill = on ? FILL_ON : FILL_NORMAL;
        }
        GmsPlate(c, d, WIN_RULE_NAME(i), gGameRules[i].labelMsg, COL_DARK, sh, nameFill);
        GmsPlate(c, d, WIN_RULE_SWITCH(i), on ? GMS_MSG_ON : GMS_MSG_OFF, COL_DARK, sh, swFill);
    }
    for (u32 i = 0; i < 2; i++) {
        u32 fill = (c->padMode && c->cursor == RULE_COUNT && c->footerSel == i) ? FILL_HIGHLIGHT : FILL_NORMAL;
        GmsPlate(c, d, WIN_CONFIRM + i, GMS_MSG_CONFIRM + i, COL_DARK, COL_LIGHT_GREY, fill);
    }
}

static void GmsDrawScreen(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    if (c->screen == SCR_MODES) {
        GmsDrawModes(c, d);
    } else {
        GmsDrawRules(c, d);
    }
}

static void GmsBuildScreen(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsRemoveWindows(c);
    GmsClear(d, BG_SUB_0);
    if (c->screen == SCR_MODES) {
        for (u32 i = 0; i < GMS_NUM_MODES; i++) {
            GmsAddWindow(c, d, MODE_BTN_X, MODE_BTN_Y(i), MODE_BTN_W, MODE_BTN_H);
        }
    } else {
        for (u32 i = 0; i < RULE_COUNT; i++) {
            GmsAddWindow(c, d, ROW_NAME_X, ROW_Y(i), ROW_NAME_W, ROW_H);
            GmsAddWindow(c, d, ROW_SWITCH_X, ROW_Y(i), ROW_SWITCH_W, ROW_H);
        }
        GmsAddWindow(c, d, FOOTER_CONFIRM_X, FOOTER_Y, FOOTER_W, ROW_H);
        GmsAddWindow(c, d, FOOTER_CANCEL_X, FOOTER_Y, FOOTER_W, ROW_H);
    }
    GmsDrawScreen(c, d);
    GmsOn(BG_SUB_0);
    c->promptMsg = GmsTopMsg(c);
}

/* ---------------------------------------------------------------------------------------------------------
 * Input
 * ------------------------------------------------------------------------------------------------------ */

#define PAD_ANY_DIR (PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT)

static void GmsReadInput(struct GmsIn *in)
{
    in->keys = (u32)gSystem.newKeys;
    in->touch = gSystem.touchNew ? 1 : 0;
    in->x = gSystem.touchX;
    in->y = gSystem.touchY;
}

static BOOL GmsHitWin(const struct GmsIn *in, const struct Window *w)
{
    if (!in->touch) {
        return FALSE;
    }
    u32 left = w->tilemapLeft * 8;
    u32 top = w->tilemapTop * 8;
    return in->x >= left && in->x < left + w->width * 8 && in->y >= top && in->y < top + w->height * 8;
}

static void GmsSE(void)
{
    PlaySE(SEQ_SE_DP_SELECT);
}

static void GmsSetPhase(struct GmsCtx *c, u32 phase)
{
    c->phase = phase;
    c->subState = 0;
}

/* Fades the current top prompt out, then continues at `after`. */
static void GmsLeavePrompt(struct GmsCtx *c, u32 after)
{
    c->afterOut = after;
    GmsSetPhase(c, PH_PROMPT_OUT);
}

static void GmsStartDialog(struct GmsCtx *c, u32 msg, BOOL isSure)
{
    c->dlgMsg = msg;
    c->dlgIsSure = isSure;
    GmsLeavePrompt(c, PH_DLG_PRINT);
}

/* Screen A */
static void GmsInputModes(struct GmsCtx *c, struct OakSpeechDataView *d, const struct GmsIn *in)
{
    int picked = -1;
    BOOL viaTouch = FALSE;

    for (u32 i = 0; i < GMS_NUM_MODES; i++) {
        if (GmsHitWin(in, &c->win[i])) {
            picked = i;
            viaTouch = TRUE;
            break;
        }
    }
    if (picked < 0) {
        if (in->keys & (PAD_ANY_DIR | PAD_BUTTON_A)) {
            if (!c->padMode) {
                c->padMode = 1; /* first press only shows the highlight */
                GmsDrawModes(c, d);
                GmsSE();
                return;
            }
            if (in->keys & PAD_KEY_UP) {
                c->cursor = (c->cursor + GMS_NUM_MODES - 1) % GMS_NUM_MODES;
                GmsDrawModes(c, d);
                GmsSE();
            } else if (in->keys & PAD_KEY_DOWN) {
                c->cursor = (c->cursor + 1) % GMS_NUM_MODES;
                GmsDrawModes(c, d);
                GmsSE();
            } else if (in->keys & PAD_BUTTON_A) {
                picked = c->cursor;
            }
        }
        /* B does nothing: there is nowhere to go back to. */
    }
    if (picked < 0) {
        return;
    }
    GmsSE();
    c->padMode = viaTouch ? 0 : 1;
    c->mode = GMS_MODE_OVER_EASY + picked;
    c->ruleValues = gGamePresets[c->mode].editable ? GameRules_ScrambledDefaults() : gGamePresets[c->mode].ruleValues;
    c->screen = SCR_RULES;
    c->cursor = 0;
    c->footerSel = 0;
    GmsLeavePrompt(c, PH_BUILD);
}

static void GmsToModes(struct GmsCtx *c)
{
    GmsSE();
    c->screen = SCR_MODES;
    c->cursor = c->mode - GMS_MODE_OVER_EASY;
    c->ruleValues = 0;
    GmsLeavePrompt(c, PH_BUILD);
}

static void GmsConfirm(struct GmsCtx *c)
{
    GmsSE();
    BOOL selectable = gGamePresets[c->mode].selectable;
    GmsStartDialog(c, selectable ? GMS_MSG_SURE : GMS_MSG_SORRY, selectable);
}

/* A rule without its own description shows the placeholder. */
static u32 GmsRuleDesc(u32 i)
{
    return gGameRules[i].descMsg ? gGameRules[i].descMsg : GMS_MSG_WHOOPS;
}

/* Screens B and C */
static void GmsInputRules(struct GmsCtx *c, struct OakSpeechDataView *d, const struct GmsIn *in)
{
    BOOL editable = GmsRulesEditable(c);

    /* touch */
    if (in->touch) {
        for (u32 i = 0; i < 2; i++) {
            if (GmsHitWin(in, &c->win[WIN_CONFIRM + i])) {
                c->cursor = RULE_COUNT;
                c->footerSel = i;
                c->padMode = 0;
                if (i == 0) {
                    GmsConfirm(c);
                } else {
                    GmsToModes(c);
                }
                return;
            }
        }
        {
            for (u32 i = 0; i < RULE_COUNT; i++) {
                if (editable && GmsHitWin(in, &c->win[WIN_RULE_SWITCH(i)])) {
                    GmsSE();
                    c->ruleValues ^= 1u << i;
                    c->cursor = i;
                    c->padMode = 0;
                    GmsDrawRules(c, d);
                    return;
                }
                if (GmsHitWin(in, &c->win[WIN_RULE_NAME(i)])) {
                    GmsSE();
                    c->cursor = i;
                    c->padMode = 0;
                    GmsStartDialog(c, GmsRuleDesc(i), FALSE);
                    return;
                }
            }
        }
        return; /* the greyed rows and empty space ignore touches */
    }

    if (in->keys & PAD_BUTTON_B) {
        GmsToModes(c);
        return;
    }
    if (!(in->keys & (PAD_ANY_DIR | PAD_BUTTON_A))) {
        return;
    }
    if (!c->padMode) {
        c->padMode = 1; /* first press only shows the highlight */
        GmsDrawRules(c, d);
        GmsSE();
        return;
    }

    if (in->keys & PAD_KEY_UP) {
        c->cursor = c->cursor == 0 ? RULE_COUNT : c->cursor - 1;
        GmsDrawRules(c, d);
        GmsSE();
    } else if (in->keys & PAD_KEY_DOWN) {
        c->cursor = c->cursor == RULE_COUNT ? 0 : c->cursor + 1;
        GmsDrawRules(c, d);
        GmsSE();
    } else if (in->keys & (PAD_KEY_LEFT | PAD_KEY_RIGHT)) {
        if (c->cursor == RULE_COUNT) {
            c->footerSel ^= 1;
        } else if (editable) {
            c->ruleValues ^= 1u << c->cursor;
        } else {
            return; /* preset: values are locked */
        }
        GmsDrawRules(c, d);
        GmsSE();
    } else { /* A */
        if (c->cursor == RULE_COUNT) {
            if (c->footerSel == 0) {
                GmsConfirm(c);
            } else {
                GmsToModes(c);
            }
        } else {
            GmsSE();
            GmsStartDialog(c, GmsRuleDesc(c->cursor), FALSE);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The machine
 * ------------------------------------------------------------------------------------------------------ */

/* Returns TRUE once Game Mode Select has finished and Oak's speech may resume at state 44. */
static BOOL GmsStep(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    struct GmsIn in;

    switch (c->phase) {
    case PH_ENTER:
        /* Mirrors retail state 7 (FADE_IN_TUTORIAL_MENU), minus the SUB_2 button art. */
        ov53_OakSpeech_TouchToAdvanceButtonAction(d, 0); /* hidden: its area would turn touches into a simulated A */
        GmsClear(d, BG_SUB_0);
        ov53_OakSpeech_SetButtonTutorialScreenLayout(d, 1);
        ov53_021E67C4(d, 0);
        GmsOn(BG_MAIN_3);
        GmsOn(BG_MAIN_0);
        GmsOn(BG_SUB_0);
        GmsOn(BG_SUB_3);
        GmsFade(d, 1);
        GmsSetPhase(c, PH_ENTER_WAIT);
        break;
    case PH_ENTER_WAIT:
        if (WIPE_SYS_EndCheck() == 1) {
            GmsSetPhase(c, PH_BUILD);
        }
        break;
    case PH_BUILD:
        GmsBuildScreen(c, d);
        GmsSetPhase(c, PH_PROMPT_IN);
        break;
    case PH_PROMPT_IN:
        if (ov53_OakSpeech_PrintAndFadeCenteredFullScreenText(d, c->promptMsg, 2) == TRUE) {
            c->promptShown = 1;
            GmsSetPhase(c, PH_INPUT);
        }
        break;
    case PH_INPUT:
        GmsReadInput(&in);
        if (c->screen == SCR_MODES) {
            GmsInputModes(c, d, &in);
        } else {
            GmsInputRules(c, d, &in);
        }
        break;
    case PH_PROMPT_OUT:
        if (!c->promptShown || ov53_OakSpeech_PrintAndFadeCenteredFullScreenText(d, c->promptMsg, 2) == TRUE) {
            c->promptShown = 0;
            GmsSetPhase(c, c->afterOut);
        }
        break;
    case PH_DLG_PRINT:
        if (c->subState == 0) {
            GmsOn(BG_MAIN_0); /* the prompt fade-out left it off */
            c->subState = 1;
        }
        if (ov53_OakSpeech_PrintDialogMsg(d, c->dlgMsg, 1) == TRUE) {
            GmsSetPhase(c, c->dlgIsSure ? PH_SURE_YESNO_START : PH_DLG_WAIT);
        }
        break;
    case PH_DLG_WAIT:
        GmsReadInput(&in);
        if ((in.keys & (PAD_BUTTON_A | PAD_BUTTON_B)) || in.touch) {
            GmsSE();
            GmsClear(d, BG_MAIN_0);
            GmsSetPhase(c, PH_PROMPT_IN);
        }
        break;
    case PH_SURE_YESNO_START:
        /* Retail's "understood?" sequence (oaks_speech.c:1719-1722). The plates are hidden by clearing the tilemap. */
        GmsClear(d, BG_SUB_0);
        ov53_OakSpeechYesNo_SetBackgroundPalette(d->yesnoMenu, 7);
        ov53_OakSpeechYesNo_Start(d->yesnoMenu, 219, 61, 62);
        GmsSetPhase(c, PH_SURE_YESNO);
        break;
    case PH_SURE_YESNO: {
        int r = ov53_OakSpeechYesNo_Main(d->yesnoMenu);
        if (r == YESNO_RESPONSE_YES) {
            GameRules_Commit(c->mode, c->ruleValues);
            GmsSetPhase(c, PH_LEAVE_FADE);
        } else if (r == YESNO_RESPONSE_NO) {
            /* back to the toggle list with the same values */
            GmsClear(d, BG_MAIN_0);
            GmsClear(d, BG_SUB_0);
            GmsDrawScreen(c, d);
            GmsOn(BG_SUB_0);
            GmsSetPhase(c, PH_PROMPT_IN);
        }
        break;
    }
    case PH_LEAVE_FADE:
        GmsFade(d, 0);
        GmsSetPhase(c, PH_LEAVE_WAIT);
        break;
    case PH_LEAVE_WAIT:
        if (WIPE_SYS_EndCheck() == 1) {
            GmsRemoveWindows(c);
            GmsClear(d, BG_MAIN_0);
            GmsClear(d, BG_SUB_0);
            /* SUB_0 and SUB_2 were switched off by the yes/no, SUB_1 never came on. */
            ov53_021E6824(d, 0); /* the yes/no overwrote SUB_2 and palette 7: put retail's layout back */
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static BOOL GmsRun(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    /* Retail reads [data+0x100] at the instant it prints, so swap it in for this frame only. */
    d->msgData = c->msg40;
    BOOL done = GmsStep(c, d);
    d->msgData = c->retailMsg;
    return done;
}

static struct GmsCtx *GmsCreate(struct OakSpeechDataView *d)
{
    struct GmsCtx *c = &sGmsCtx;
    memset(c, 0, sizeof(*c));
    c->retailMsg = d->msgData;
    c->msg40 = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, 27, 40, d->heapID);
    c->nextTile = GMS_FIRST_TILE;
    c->screen = SCR_MODES;
    c->phase = PH_ENTER;
    return c;
}

static void GmsDestroy(struct GmsCtx *c)
{
    DestroyMsgData(c->msg40);
    c->msg40 = NULL;
}

BOOL GMS_DoMainTask(void *data)
{
    struct OakSpeechDataView *d = data;

    if (d->state == 0) {
        sGms = NULL; /* first state of every Oak run */
        sGmsDone = 0;
    }
    if (d->state == OAK_STATE_NO_INFO_NEEDED_FADE_IN && !sGmsDone) {
        if (sGms == NULL) {
            sGms = GmsCreate(d);
        }
        if (GmsRun(sGms, d)) {
            GmsDestroy(sGms);
            sGms = NULL;
            sGmsDone = 1; /* state is still 44: the retail machine takes over next frame */
        }
        return FALSE;
    }
    return ov53_OakSpeech_DoMainTask(data);
}
