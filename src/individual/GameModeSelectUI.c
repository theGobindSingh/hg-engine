/*
 * Game Mode Select UI, overlay 152 (loaded by GMS_DoMainTask in ov129 for the duration of the screen only).
 * See ~/Programming/james-game/docs/game-mode-select.md for the design and every proof of an address below.
 *
 * Look: the lower screen of the mode select is retail's intro info menu (the three-button art, retrofitted to
 * four buttons, with the one-button SUB_1 frame as cursor); the toggle screen is retail's Options menu
 * (backdrop, red row frame, chosen white box with arrow, CONFIRM/QUIT buttons), built from the Options app's
 * own graphics (NARC 72 = a/0/7/2) and drawn on the Oak app's four sub-screen BG layers, so no sprite system is
 * needed. The description of the selected rule is shown in the framed pane at the bottom of the top screen.
 *
 * Sub screen layers (all priority 0 except SUB_3, so SUB_0 is in front):
 *   SUB_0  text windows (font palette 14)
 *   SUB_1  cursor frame (info menu frame / Options red row frame), moved by BG scroll
 *   SUB_2  info menu button art / the Options chosen-box and footer-button pictures (window pixel canvases)
 *   SUB_3  backdrop (Poke Ball motif / Options backdrop)
 * Sub BG palettes: 5-6 Options backdrop, 10 Options box/arrow, 11 Options footer buttons (retail Oak uses 0-4,
 * 7-9 and 14 only).
 */
#include "../../include/types.h"

#include "../../include/constants/sndseq.h"
#include "../../include/game_mode_select.h"
#include "../../include/game_mode_select_ui.h"
#include "../../include/game_rules.h"
#include "../../include/message.h"
#include "../../include/save.h"
#include "../../include/sound.h"
#include "../../include/sprite.h"
#include "../../include/system.h"
#include "../../include/window.h"

/* ---------------------------------------------------------------------------------------------------------
 * Overlay 53 data and functions
 * ------------------------------------------------------------------------------------------------------ */

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

/* The window pixel canvas is written directly: 4bpp, 8x8 tiles of 32 bytes in row-major window order. */
_Static_assert(__builtin_offsetof(struct Window, bgId) == 0x04, "Window.bgId offset");
_Static_assert(__builtin_offsetof(struct Window, width) == 0x07, "Window.width offset");
_Static_assert(__builtin_offsetof(struct Window, height) == 0x08, "Window.height offset");
_Static_assert(__builtin_offsetof(struct Window, pixelBuffer) == 0x0C, "Window.pixelBuffer offset");
_Static_assert(sizeof(struct Window) == 0x10, "Window size");
_Static_assert(__builtin_offsetof(String, size) == 0x02, "String.size offset");
_Static_assert(__builtin_offsetof(String, data) == 0x08, "String.data offset");

extern BOOL LONG_CALL ov53_OakSpeech_PrintAndFadeFullScreenText(struct OakSpeechDataView *data, int msgNum, int kind, int y, int height);
extern void LONG_CALL ov53_OakSpeech_TouchToAdvanceButtonAction(struct OakSpeechDataView *data, int action);
extern void LONG_CALL ov53_OakSpeech_SetButtonTutorialScreenLayout(struct OakSpeechDataView *data, int layout);
extern void LONG_CALL ov53_OakSpeech_FillBgLayerWithPalette(struct OakSpeechDataView *data, int layer, int palette);
extern void LONG_CALL ov53_021E67C4(struct OakSpeechDataView *data, int idx);
extern void LONG_CALL ov53_021E6824(struct OakSpeechDataView *data, int idx);
extern void LONG_CALL ov53_OakSpeechYesNo_SetBackgroundPalette(void *yesnoMenu, int palette);
extern void LONG_CALL ov53_OakSpeechYesNo_Start(void *yesnoMenu, int msgBank, int msgIdYes, int msgIdNo);
extern int LONG_CALL ov53_OakSpeechYesNo_Main(void *yesnoMenu);
extern void LONG_CALL ToggleBgLayer(u32 layer, u32 on);
extern void LONG_CALL BeginNormalPaletteFade(u32 pattern, u32 typeTop, u32 typeBottom, u32 colour, u32 duration, u32 framesPer, u32 heapId);
extern void LONG_CALL GfGfxLoader_LoadScrnData(u32 narcId, s32 memberNo, void *bgConfig, u32 layer, u32 tileStart, u32 szByte, BOOL isCompressed, u32 heapId);
extern void LONG_CALL ScheduleSetBgPosText(void *bgConfig, u32 layer, u32 op, int value);
extern void LONG_CALL BgCommitTilemapBufferToVram(void *bgConfig, u32 layer);
extern void LONG_CALL Heap_FreeExplicit(u32 heapId, void *ptr);
extern void LONG_CALL ClearFrameAndWindow2(void *window, BOOL copyToVram);

#define BG_MAIN_0 0
#define BG_MAIN_3 3
#define BG_SUB_0  4
#define BG_SUB_1  5
#define BG_SUB_2  6
#define BG_SUB_3  7

#define BG_POS_SET_Y   3 /* ScheduleSetBgPosText op, as at ov53 0x21E68A4 */
#define PAL_LOC_SUB_BG 4 /* GfGfxLoader_GXLoadPal location, as at ov53 0x21E6872 */

#define YESNO_RESPONSE_YES 1
#define YESNO_RESPONSE_NO  2

#define GMS_TEXT_SPEED_INSTANT 0
#define GMS_TEXT_COLOR(fg, sh, bg) ((((fg) & 0xFF) << 16) | (((sh) & 0xFF) << 8) | ((bg) & 0xFF))

/* ---------------------------------------------------------------------------------------------------------
 * Graphics resources
 * ------------------------------------------------------------------------------------------------------ */

#define NARC_OPTIONS 72  /* a/0/7/2, pret NARC_a_0_7_2: the Options app's graphics */
#define NARC_INTRO   120 /* a/1/2/0, the Oak intro's graphics (ov53 passes 0x78) */

#define OPT_NCLR_BOX   0  /* sprite palette 0: grey/white boxes, arrow */
#define OPT_NCLR_FOOT  1  /* sprite palette 1: blue footer buttons */
#define OPT_NCLR_BG    2  /* lower-screen BG palettes (2 x 16 colours) */
#define OPT_NCGR_BOX   4  /* sprite tiles: chosen value box and arrow */
#define OPT_NCGR_FOOT  6  /* sprite tiles: CONFIRM / QUIT buttons */
#define OPT_NCGR_BG    7  /* lower-screen BG tiles (128) */
#define OPT_NSCR_BG    17 /* lower-screen backdrop (32x32 entries, six baked rows) */
#define OPT_NSCR_FRAME 18 /* red row cursor frame (32x7 entries) */

#define INTRO_NCGR_BACKDROP 32 /* SUB_3 tiles, loaded by retail at OakSpeech_LoadButtonTutorialGfx */
#define INTRO_NCGR_FRAME    42 /* SUB_1 cursor frame tiles */
#define INTRO_NSCR_BUTTONS  47 /* three-button art, SUB_2 */
#define INTRO_NSCR_FRAME    49 /* one-button cursor frame, SUB_1 */

#define SUB_PAL_BUTTONS  7  /* retail button palette (3 palettes at 7-9) */
#define SUB_PAL_OPT_BG   5  /* palettes 5 and 6 */
#define SUB_PAL_OPT_BOX  10
#define SUB_PAL_OPT_FOOT 11
#define SUB_PAL_FONT     14 /* retail font palette 0 */

/* File layout of the raw members, read with AllocAndReadWholeNarcMemberByIdPair. */
#define NSCR_MAGIC          0x5343524Eu /* 'NRCS' section tag at +0x10, entries at +0x24 */
#define NCGR_MAGIC          0x43484152u /* 'RAHC' section tag at +0x10, tiles at +0x30 */
#define SECTION_TAG_OFFSET  0x10
#define NSCR_ENTRIES_OFFSET 0x24
#define NCGR_TILES_OFFSET   0x30
#define SCREEN_W 32
#define SCREEN_H 24

/* Sprite cells of the Options app (NCER members 9 and 12 of NARC 72), decoded from this ROM. Tile numbers are in
 * 1D mapping: an object of w x h tiles reads w*h consecutive tiles, row by row. */
struct GmsOam {
    s8 x;
    s8 y;
    u8 w;
    u8 h;
    u8 tile;
    u8 flipH;
};

/* NCER 9 cell 0: the small chosen value box with its arrow (ON/OFF boxes are this size). */
static const struct GmsOam sBoxOam[3] = {
    { -8, 4, 2, 2, 0, 0 },
    { 0, 0, 4, 4, 4, 0 },
    { 32, 0, 1, 4, 20, 0 },
};

/* NCER 12 cells 0-3: 64x24 footer button; 0 normal, 1 selected (red outline), 2 and 3 press flash. */
static const struct GmsOam sFootOam[4][4] = {
    { { 32, 16, 4, 1, 0, 1 }, { 32, 0, 4, 2, 4, 1 }, { 0, 16, 4, 1, 0, 0 }, { 0, 0, 4, 2, 4, 0 } },
    { { 32, 16, 4, 1, 12, 1 }, { 32, 0, 4, 2, 16, 1 }, { 0, 16, 4, 1, 12, 0 }, { 0, 0, 4, 2, 16, 0 } },
    { { 32, 0, 4, 2, 24, 1 }, { 32, 16, 4, 1, 32, 1 }, { 0, 0, 4, 2, 24, 0 }, { 0, 16, 4, 1, 32, 0 } },
    { { 32, 0, 4, 2, 36, 1 }, { 32, 16, 4, 1, 44, 1 }, { 0, 0, 4, 2, 36, 0 }, { 0, 16, 4, 1, 44, 0 } },
};

#define FOOT_NORMAL   0
#define FOOT_SELECTED 1
#define FOOT_PRESS_A  2

/* ---------------------------------------------------------------------------------------------------------
 * Layout (tiles; the lower screen is 32x24 tiles = 256x192 px)
 * ------------------------------------------------------------------------------------------------------ */

#define GMS_FIRST_TILE  0xC0 /* SUB_0 text windows: above every retail SUB_0 window (<= 0xB0 incl. the yes/no pair) */
#define GMS_CANVAS_TILE 0x40 /* SUB_2 canvas windows (the retail button art needs tiles 0-33 only on screen A) */

#define GMS_NUM_MODES 4 /* Over Easy, Softboiled, Hardboiled, Scrambled = GMS_MODE_OVER_EASY..SCRAMBLED */

/* Screen A: retail info menu geometry, retrofitted to four buttons (pitch 6 tiles). The button art is 4 tile rows
 * tall and is copied from rows 3-6 of retail's NSCR 47. */
#define MODE_BTN_SRC_ROW 3
#define MODE_BTN_ROWS    4
#define MODE_BTN_ROW(i)  (1 + 6 * (i))
#define MODE_TXT_X       7
#define MODE_TXT_W       18
#define MODE_TXT_H       3
#define MODE_TXT_Y       8 /* retail's text y for a 3-button menu */
#define MODE_HIT_X0      50
#define MODE_HIT_X1      213
#define MODE_HIT_Y0(i)   (4 + 48 * (i))
#define MODE_HIT_Y1(i)   (42 + 48 * (i))
#define MODE_FRAME_Y(i)  (((MODE_BTN_SRC_ROW - MODE_BTN_ROW(i)) * 8) & 0xFF) /* retail's frame sits at rows 3-6 */

/* Screens B and C: Options geometry. Rule i occupies tile rows 3+3i .. 5+3i. */
#define OPT_HEADER_ROWS      3
#define OPT_ROW_TILES        3
#define OPT_ROW_TOP(i)       (OPT_HEADER_ROWS + OPT_ROW_TILES * (i))
#define OPT_ROW_Y(i)         (8 * OPT_ROW_TOP(i)) /* retail sprite y is 24 + 24 i */
#define OPT_FRAME_Y(i)       (-8 - 24 * (i))      /* retail sMenuEntryBorderYCoords */
#define OPT_BOX_ON_X         112                  /* sprite x of the first (ON) box position */
#define OPT_BOX_OFF_X        160
#define OPT_BOX_W            40
#define OPT_CANVAS_COL       13 /* chosen-box canvas: tile columns 13 .. 25 */
#define OPT_CANVAS_W         13
#define OPT_LABEL_COL        1
#define OPT_LABEL_W          13
#define OPT_VALUE_COL        14 /* value text window: tile columns 14 .. 24 */
#define OPT_VALUE_W          11
#define OPT_FOOT_ROW         21
#define OPT_FOOT_CONFIRM_COL 14 /* canvas/label windows, 9 tiles wide; the sprite sits 4 px inside (x 116 / 188) */
#define OPT_FOOT_CANCEL_COL  23
#define OPT_FOOT_W           9
#define OPT_FOOT_H           4
#define OPT_FOOT_SPRITE_X    4 /* px inside the 72 px canvas */
#define OPT_FOOT_SPRITE_Y    2 /* retail sprite y is 170; the canvas starts at row 21 = 168 */
#define OPT_HDR_W            17 /* the tab is 19 tiles wide */
/* source rows/columns of the retail backdrop NSCR 17 */
#define OPT_SRC_LIGHT_ROW    3  /* rows 3-5: light group (three boxes); its third box is erased for ON/OFF rows */
#define OPT_SRC_DARK_ROW     6  /* rows 6-8: dark group (BATTLE SCENE: ON and OFF boxes) */
#define OPT_SRC_EDGE_ROW     21 /* panel bottom edge */
#define OPT_SRC_FILL_ROW     22 /* plain grey */
#define OPT_SRC_THIRD_BOX_X0 26
#define OPT_SRC_THIRD_BOX_X1 30
#define OPT_SRC_GAP_COL      25
/* touch rectangles (retail Options sOptionsAppTouchscreenHitboxes) */
#define OPT_HIT_BOX_Y0(i)    (26 + 24 * (i))
#define OPT_HIT_BOX_Y1(i)    (46 + 24 * (i))
#define OPT_HIT_ON_X0        112
#define OPT_HIT_ON_X1        151
#define OPT_HIT_OFF_X0       160
#define OPT_HIT_OFF_X1       200
#define OPT_HIT_CONFIRM_X0   128
#define OPT_HIT_CONFIRM_X1   180
#define OPT_HIT_CANCEL_X0    183
#define OPT_HIT_CANCEL_X1    255
#define OPT_HIT_FOOT_Y0      172
#define OPT_HIT_FOOT_Y1      191

/* Top pane: the retail Oak dialog window and frame (sWindowTemplate_DialogMsg, DrawFrameAndWindow2 0x3E2 / 4). */
#define PANE_X          2
#define PANE_Y          19
#define PANE_W          27
#define PANE_H          4
#define PANE_PAL        6
#define PANE_BASE       0x36D
#define PANE_FRAME_TILE 0x3E2
#define PANE_FRAME_PAL  4
#define PROMPT_LAST_ROW 17 /* the prompt must end above the pane's frame row (18) */

#define GMS_MAX_WIN    (3 + 2 * RULE_COUNT > GMS_NUM_MODES ? 3 + 2 * RULE_COUNT : GMS_NUM_MODES)
#define GMS_MAX_CANVAS (RULE_COUNT + 2)

_Static_assert(RULE_COUNT >= 1 && RULE_COUNT <= 4, "the rule list draws at most 4 rows without paging");
_Static_assert(GMS_MSG_CANCEL == GMS_MSG_CONFIRM + 1, "footer labels are indexed by position");
_Static_assert(GMS_MSG_OFF == GMS_MSG_ON + 1, "ON/OFF labels are consecutive");
_Static_assert(FLAG_GMS_MODE_BIT1 == FLAG_GMS_MODE_BIT0 + 1 && FLAG_GMS_MODE_BIT2 == FLAG_GMS_MODE_BIT0 + 2, "mode bit flags are consecutive");
_Static_assert(OPT_ROW_TOP(RULE_COUNT) + 1 < OPT_FOOT_ROW, "the rule list must end above the footer buttons");

/* Text colours (font palette 0): 1 dark, 2 light grey shadow, 15 white. */
#define TXT_WHITE  GMS_TEXT_COLOR(15, 2, 0) /* Options names and unchosen values */
#define TXT_DARK   GMS_TEXT_COLOR(1, 2, 0)  /* Options chosen value */
#define TXT_BUTTON GMS_TEXT_COLOR(15, 1, 0) /* info menu buttons */

/* Sprite palette 0 colours: 1 = white box fill; a locked (preset) box is drawn with the grey 3. */
#define BOX_FILL_IDX   1
#define BOX_LOCKED_IDX 3

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
    PH_FOOT_PRESS,
    PH_SORRY,
    PH_SURE_YESNO_START,
    PH_SURE_YESNO,
    PH_LEAVE_FADE,
    PH_LEAVE_WAIT
};

#define FOOT_PRESS_FRAMES 10

/* Scalars first and word-sized: Thumb loads with a small immediate offset are much shorter. */
struct GmsCtx {
    const struct GameRuleDef *rules;     /* passed in by ov129: data symbols of ov129 are not linkable from here (the symbol generator adds the Thumb bit to them) */
    const struct GamePresetDef *presets;
    MsgData *msg40;     /* archive 40 (Game Mode Select strings) */
    MsgData *retailMsg; /* Oak's own archive 219 MsgData, restored on every return */
    u8 *boxTiles;       /* raw NCGR 4 (screen B only) */
    u8 *footTiles;      /* raw NCGR 6 (screen B only) */
    u32 nWin;
    u32 nCanvas;
    u32 phase;
    u32 afterOut;      /* phase to enter once the prompt has faded out */
    u32 screen;
    u32 mode;          /* GmsModeId shown on SCR_RULES */
    u32 cursor;        /* SCR_MODES: 0-3; SCR_RULES: rule row, RULE_COUNT = CONFIRM, RULE_COUNT + 1 = CANCEL */
    u32 footerSel;     /* last footer button the cursor was on (0 = Confirm, 1 = Cancel) */
    u32 padMode;       /* SCR_MODES: highlight frame visible */
    u32 promptShown;   /* top prompt currently faded in (so the next call fades it out) */
    u32 paneUp;
    u32 descRule;      /* rule whose description the pane shows */
    u32 pressDelay;    /* SCR_MODES: frames since a press (0 = none) */
    u32 flashDelay;
    u32 flashState;
    u32 footPress;     /* PH_FOOT_PRESS: frames elapsed */
    u32 footAction;    /* the footer button being pressed: 0 confirm, 1 cancel */
    u32 sub3IsOptions; /* SUB_3 holds the Options backdrop instead of retail's */
    u32 promptMsg;     /* archive 40 index of the current top prompt */
    u32 promptY;
    u32 promptH;
    u32 ruleValues; /* bit i = rule i ON */
    struct Window pane;
    struct Window win[GMS_MAX_WIN];       /* SUB_0 text */
    struct Window canvas[GMS_MAX_CANVAS]; /* SUB_2 pixel canvases */
    u16 map[SCREEN_W * SCREEN_H];
};

struct GmsIn {
    u32 keys;
    u8 touch;
    u16 x;
    u16 y;
};

static struct GmsCtx sGmsCtx;
static struct GmsCtx *sGms; /* NULL when idle */

/* ---------------------------------------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------------------------------------ */

static void __attribute__((noinline)) GmsClear(struct OakSpeechDataView *d, u32 bg)
{
    BgClearTilemapBufferAndCommit(d->bgConfig, bg);
}

static void __attribute__((noinline)) GmsLayer(u32 bg, u32 on)
{
    ToggleBgLayer(bg, on);
}

static void __attribute__((noinline)) GmsFade(struct OakSpeechDataView *d, u32 in)
{
    BeginNormalPaletteFade(0, in, in, 0, 6, 1, d->heapID);
}

static void GmsSE(void)
{
    PlaySE(SEQ_SE_DP_SELECT);
}

static void GmsSetPhase(struct GmsCtx *c, u32 phase)
{
    c->phase = phase;
}

static u8 *GmsReadMember(struct OakSpeechDataView *d, u32 narc, u32 member, u32 magic)
{
    u8 *raw = AllocAndReadWholeNarcMemberByIdPair(narc, member, d->heapID);
    if (raw != NULL && *(u32 *)(raw + SECTION_TAG_OFFSET) != magic) {
        Heap_FreeExplicit(d->heapID, raw);
        raw = NULL;
    }
    return raw;
}

static void GmsFreeMember(struct OakSpeechDataView *d, u8 *raw)
{
    if (raw != NULL) {
        Heap_FreeExplicit(d->heapID, raw);
    }
}

static void GmsLoadPalette(struct OakSpeechDataView *d, u32 narc, u32 member, u32 slot, u32 numPalettes)
{
    ArcUtil_PalSet(narc, member, PAL_LOC_SUB_BG, slot * 0x20, numPalettes * 0x20, d->heapID);
}

static void GmsRemoveWindows(struct GmsCtx *c)
{
    for (u32 i = 0; i < c->nWin; i++) {
        RemoveWindow(&c->win[i]);
    }
    c->nWin = 0;
    for (u32 i = 0; i < c->nCanvas; i++) {
        RemoveWindow(&c->canvas[i]);
    }
    c->nCanvas = 0;
}

static void GmsFreeTiles(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsFreeMember(d, c->boxTiles);
    GmsFreeMember(d, c->footTiles);
    c->boxTiles = NULL;
    c->footTiles = NULL;
}

static struct Window *GmsAddWindowAt(struct OakSpeechDataView *d, u32 bg, struct Window *w, u32 x, u32 y, u32 ww, u32 hh,
    u32 pal, u32 tile)
{
    WindowTemplate t;
    t.bgId = bg;
    t.left = x;
    t.top = y;
    t.width = ww;
    t.height = hh;
    t.palette = pal;
    t.baseTile = tile;
    AddWindow(d->bgConfig, w, &t);
    return w;
}

/* SUB_0 text window; tiles are handed out above retail's. */
static struct Window *GmsAddText(struct GmsCtx *c, struct OakSpeechDataView *d, u32 x, u32 y, u32 w, u32 h)
{
    u32 tile = GMS_FIRST_TILE;
    for (u32 i = 0; i < c->nWin; i++) {
        tile += c->win[i].width * c->win[i].height;
    }
    struct Window *win = GmsAddWindowAt(d, BG_SUB_0, &c->win[c->nWin], x, y, w, h, SUB_PAL_FONT, tile);
    c->nWin++;
    FillWindowPixelBuffer(win, 0);
    return win;
}

/* SUB_2 pixel canvas window. */
static struct Window *GmsAddCanvas(struct GmsCtx *c, struct OakSpeechDataView *d, u32 x, u32 y, u32 w, u32 h, u32 pal)
{
    u32 tile = GMS_CANVAS_TILE;
    for (u32 i = 0; i < c->nCanvas; i++) {
        tile += c->canvas[i].width * c->canvas[i].height;
    }
    struct Window *win = GmsAddWindowAt(d, BG_SUB_2, &c->canvas[c->nCanvas], x, y, w, h, pal, tile);
    c->nCanvas++;
    FillWindowPixelBuffer(win, 0);
    return win;
}

/* Prints one string into a window: left edge at x, or centred in the span [x, x + span) when span != 0. */
static void GmsPrint(struct GmsCtx *c, struct OakSpeechDataView *d, struct Window *w, u32 msg, u32 font, u32 x, u32 span,
    u32 y, u32 color)
{
    String *s = String_New(0x80, d->heapID);
    ReadMsgDataIntoString(c->msg40, msg, s);
    if (span != 0) {
        x += FontID_String_GetCenterAlignmentX(font, s, 0, span);
    }
    AddTextPrinterParameterizedWithColor(w, font, s, x, y, GMS_TEXT_SPEED_INSTANT, color, NULL);
    String_Delete(s);
}

/* Counts the lines of a message (CHAR_LF = 0xE000, as pret String_CountLines). */
static u32 GmsCountLines(struct GmsCtx *c, struct OakSpeechDataView *d, u32 msg)
{
    String *s = String_New(0x400, d->heapID);
    ReadMsgDataIntoString(c->msg40, msg, s);
    u32 lines = 1;
    for (u32 i = 0; i < s->size; i++) {
        if (s->data[i] == 0xE000) {
            lines++;
        }
    }
    String_Delete(s);
    return lines;
}

/* ---------------------------------------------------------------------------------------------------------
 * Top screen: prompt and pane
 * ------------------------------------------------------------------------------------------------------ */

static BOOL GmsRulesEditable(const struct GmsCtx *c)
{
    return c->presets[c->mode].editable;
}

static u32 GmsTopMsg(const struct GmsCtx *c)
{
    return c->screen == SCR_MODES ? GMS_MSG_TOP_MAIN : c->presets[c->mode].topMsg;
}

/* A rule without its own description shows the placeholder. */
static u32 GmsRuleDesc(const struct GmsCtx *c, u32 i)
{
    return c->rules[i].descMsg ? c->rules[i].descMsg : GMS_MSG_WHOOPS;
}

static void GmsPaneText(struct GmsCtx *c, struct OakSpeechDataView *d, u32 msg)
{
    String *s = String_New(0x400, d->heapID);
    ReadMsgDataIntoString(c->msg40, msg, s);
    FillWindowPixelBuffer(&c->pane, 15);
    AddTextPrinterParameterized(&c->pane, 1, s, 0, 0, GMS_TEXT_SPEED_INSTANT, NULL);
    String_Delete(s);
    CopyWindowToVram(&c->pane);
}

static void GmsPaneShow(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    if (!c->paneUp) {
        GmsLayer(BG_MAIN_0, 1); /* the prompt fade ended with the layer on; make sure */
        GmsAddWindowAt(d, BG_MAIN_0, &c->pane, PANE_X, PANE_Y, PANE_W, PANE_H, PANE_PAL, PANE_BASE);
        DrawFrameAndWindow2(&c->pane, FALSE, PANE_FRAME_TILE, PANE_FRAME_PAL);
        c->paneUp = 1;
    }
}

static void GmsPaneHide(struct GmsCtx *c)
{
    if (c->paneUp) {
        ClearFrameAndWindow2(&c->pane, FALSE); /* FALSE = commit now */
        RemoveWindow(&c->pane);
        c->paneUp = 0;
    }
}

/* Fades the current top prompt out (the pane goes first), then continues at `after`. */
static void GmsLeavePrompt(struct GmsCtx *c, u32 after)
{
    GmsPaneHide(c);
    c->afterOut = after;
    GmsSetPhase(c, PH_PROMPT_OUT);
}

/* ---------------------------------------------------------------------------------------------------------
 * Screen A: mode select (retail info menu art)
 * ------------------------------------------------------------------------------------------------------ */

static void GmsRestoreRetailBackdrop(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    if (c->sub3IsOptions) {
        GfGfxLoader_LoadCharData(NARC_INTRO, INTRO_NCGR_BACKDROP, d->bgConfig, BG_SUB_3, 0, 0, FALSE, d->heapID);
        ov53_021E67C4(d, 0);
        c->sub3IsOptions = 0;
    }
}

static void GmsModeFrame(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    ScheduleSetBgPosText(d->bgConfig, BG_SUB_1, BG_POS_SET_Y, MODE_FRAME_Y(c->cursor));
}

static void GmsBuildModes(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsRemoveWindows(c);
    GmsFreeTiles(c, d);
    GmsClear(d, BG_SUB_0);
    GmsClear(d, BG_SUB_2);
    GmsLayer(BG_SUB_1, 0);
    GmsRestoreRetailBackdrop(c, d);

    /* Retail's three-button art, then a four-button tilemap composed from its first button's four tile rows. */
    ov53_021E6824(d, 0);
    u8 *raw = GmsReadMember(d, NARC_INTRO, INTRO_NSCR_BUTTONS, NSCR_MAGIC);
    if (raw != NULL) {
        const u16 *src = (const u16 *)(raw + NSCR_ENTRIES_OFFSET);
        for (u32 i = 0; i < SCREEN_W * SCREEN_H; i++) {
            c->map[i] = 0;
        }
        for (u32 k = 0; k < GMS_NUM_MODES; k++) {
            for (u32 r = 0; r < MODE_BTN_ROWS; r++) {
                for (u32 x = 0; x < SCREEN_W; x++) {
                    u16 v = src[(MODE_BTN_SRC_ROW + r) * SCREEN_W + x];
                    if (v != 0) {
                        v = (v & 0x0FFF) | (SUB_PAL_BUTTONS << 12);
                    }
                    c->map[(MODE_BTN_ROW(k) + r) * SCREEN_W + x] = v;
                }
            }
        }
        GmsFreeMember(d, raw);
        LoadRectToBgTilemapRect(d->bgConfig, BG_SUB_2, c->map, 0, 0, SCREEN_W, SCREEN_H);
        BgCommitTilemapBufferToVram(d->bgConfig, BG_SUB_2);
    }

    /* Retail's one-button cursor frame, shown only once the pad or touch has been used. */
    GfGfxLoader_LoadCharData(NARC_INTRO, INTRO_NCGR_FRAME, d->bgConfig, BG_SUB_1, 0, 0, FALSE, d->heapID);
    GfGfxLoader_LoadScrnData(NARC_INTRO, INTRO_NSCR_FRAME, d->bgConfig, BG_SUB_1, 0, 0, FALSE, d->heapID);
    GmsModeFrame(c, d);

    for (u32 i = 0; i < GMS_NUM_MODES; i++) {
        struct Window *w = GmsAddText(c, d, MODE_TXT_X, MODE_BTN_ROW(i), MODE_TXT_W, MODE_TXT_H);
        GmsPrint(c, d, w, c->presets[GMS_MODE_OVER_EASY + i].nameMsg, 4, 0, w->width * 8, MODE_TXT_Y, TXT_BUTTON);
        CopyWindowToVram(w);
    }

    GmsLayer(BG_SUB_3, 1);
    GmsLayer(BG_SUB_2, 1);
    GmsLayer(BG_SUB_0, 1);
    GmsLayer(BG_SUB_1, c->padMode ? 1 : 0);
    c->pressDelay = 0;
    c->flashDelay = 0;
    c->flashState = 0;
}

#define PAD_ANY_DIR (PAD_KEY_UP | PAD_KEY_DOWN | PAD_KEY_LEFT | PAD_KEY_RIGHT)

static void GmsReadInput(struct GmsIn *in)
{
    in->keys = (u32)gSystem.newKeys;
    in->touch = gSystem.touchNew ? 1 : 0;
    in->x = gSystem.touchX;
    in->y = gSystem.touchY;
}

/* Retail OakSpeech_MultichoiceMenuHandleInputVertical, with four buttons and no B. */
static void GmsInputModes(struct GmsCtx *c, struct OakSpeechDataView *d, const struct GmsIn *in)
{
    int picked = -1;

    if (c->pressDelay != 0) {
        c->pressDelay++;
        if (c->pressDelay > 20) {
            c->pressDelay = 0;
            picked = c->cursor;
        }
    } else if (in->touch) {
        for (u32 i = 0; i < GMS_NUM_MODES; i++) {
            if (in->x >= MODE_HIT_X0 && in->x <= MODE_HIT_X1 && in->y >= MODE_HIT_Y0(i) && in->y <= MODE_HIT_Y1(i)) {
                c->cursor = i;
                GmsModeFrame(c, d);
                GmsLayer(BG_SUB_1, 1);
                c->pressDelay = 1;
                c->flashDelay = 0;
                c->flashState = 0;
                c->padMode = 0;
                GmsSE();
                break;
            }
        }
    } else if (!c->padMode) {
        if (in->keys & (PAD_BUTTON_A | PAD_KEY_UP | PAD_KEY_DOWN)) {
            GmsSE();
            GmsLayer(BG_SUB_1, 1);
            c->padMode = 1;
        }
    } else if (in->keys & PAD_KEY_UP) {
        if (c->cursor != 0) {
            c->cursor--;
            GmsModeFrame(c, d);
            GmsSE();
        }
    } else if (in->keys & PAD_KEY_DOWN) {
        if (c->cursor != GMS_NUM_MODES - 1) {
            c->cursor++;
            GmsModeFrame(c, d);
            GmsSE();
        }
    } else if (in->keys & PAD_BUTTON_A) {
        c->pressDelay = 1;
        c->flashDelay = 0;
        c->flashState = 0;
        GmsSE();
    }
    if (c->pressDelay != 0) { /* the frame flashes while the press is shown */
        c->flashDelay++;
        if (c->flashDelay > 2) {
            c->flashState ^= 1;
            c->flashDelay = 0;
            GmsLayer(BG_SUB_1, c->flashState ? 0 : 1);
        }
    }
    if (picked < 0) {
        return;
    }
    c->mode = GMS_MODE_OVER_EASY + picked;
    c->ruleValues = c->presets[c->mode].editable ? GameRules_ScrambledDefaults() : c->presets[c->mode].ruleValues;
    c->screen = SCR_RULES;
    c->cursor = 0;
    c->footerSel = 0;
    c->descRule = 0;
    GmsLayer(BG_SUB_1, 1);
    GmsLeavePrompt(c, PH_BUILD);
}

/* ---------------------------------------------------------------------------------------------------------
 * Screens B and C: the Options look
 * ------------------------------------------------------------------------------------------------------ */

static void GmsPutPixel(struct Window *w, int x, int y, u32 v)
{
    if (x < 0 || y < 0 || x >= (int)(w->width * 8) || y >= (int)(w->height * 8)) {
        return;
    }
    u8 *p = (u8 *)w->pixelBuffer + (((y >> 3) * w->width + (x >> 3)) * 32) + ((y & 7) * 4) + ((x & 7) >> 1);
    if (x & 1) {
        *p = (*p & 0x0F) | (v << 4);
    } else {
        *p = (*p & 0xF0) | v;
    }
}

/* Draws the OAMs of one sprite cell into a window canvas at (ox, oy). The first OAM is on top, so draw last-first.
 * lockedFill: replace the box fill colour with grey (a locked preset's chosen box). */
static void GmsBlitCell(struct Window *w, const u8 *raw, const struct GmsOam *oam, u32 count, int ox, int oy, BOOL lockedFill)
{
    if (raw == NULL) {
        return;
    }
    const u8 *tiles = raw + NCGR_TILES_OFFSET;
    for (u32 n = count; n > 0; n--) {
        const struct GmsOam *o = &oam[n - 1];
        for (u32 ty = 0; ty < o->h; ty++) {
            for (u32 tx = 0; tx < o->w; tx++) {
                const u8 *t = tiles + (o->tile + ty * o->w + tx) * 32;
                u32 dtx = o->flipH ? (u32)(o->w - 1 - tx) : tx;
                for (u32 y = 0; y < 8; y++) {
                    for (u32 x = 0; x < 8; x++) {
                        u32 v = (t[y * 4 + (x >> 1)] >> ((x & 1) * 4)) & 15;
                        if (v == 0) {
                            continue;
                        }
                        if (lockedFill && v == BOX_FILL_IDX) {
                            v = BOX_LOCKED_IDX;
                        }
                        u32 px = dtx * 8 + (o->flipH ? 7 - x : x);
                        GmsPutPixel(w, ox + o->x + (int)px, oy + o->y + (int)(ty * 8 + y), v);
                    }
                }
            }
        }
    }
}

/* Window slots on SCR_RULES. */
#define WIN_HEADER     0
#define WIN_LABEL(i)   (1 + 2 * (i))
#define WIN_VALUE(i)   (2 + 2 * (i))
#define CANVAS_FOOT(k) (RULE_COUNT + (k))

static BOOL GmsRuleIsOn(const struct GmsCtx *c, u32 i)
{
    return (c->ruleValues >> i) & 1;
}

static void GmsDrawRuleRow(struct GmsCtx *c, struct OakSpeechDataView *d, u32 i)
{
    BOOL on = GmsRuleIsOn(c, i);
    BOOL locked = !GmsRulesEditable(c);

    /* the chosen box and arrow: first position = ON, second = OFF */
    struct Window *cv = &c->canvas[i];
    FillWindowPixelBuffer(cv, 0);
    GmsBlitCell(cv, c->boxTiles, sBoxOam, 3, (on ? OPT_BOX_ON_X : OPT_BOX_OFF_X) - OPT_CANVAS_COL * 8, 0, locked);
    CopyWindowToVram(cv);

    /* the two value labels */
    struct Window *w = &c->win[WIN_VALUE(i)];
    FillWindowPixelBuffer(w, 0);
    GmsPrint(c, d, w, GMS_MSG_ON, 0, OPT_BOX_ON_X - OPT_VALUE_COL * 8, OPT_BOX_W, 5, on ? TXT_DARK : TXT_WHITE);
    GmsPrint(c, d, w, GMS_MSG_OFF, 0, OPT_BOX_OFF_X - OPT_VALUE_COL * 8, OPT_BOX_W, 5, on ? TXT_WHITE : TXT_DARK);
    CopyWindowToVram(w);
}

static void GmsDrawFooter(struct GmsCtx *c, u32 k, u32 cell)
{
    struct Window *cv = &c->canvas[CANVAS_FOOT(k)];
    FillWindowPixelBuffer(cv, 0);
    GmsBlitCell(cv, c->footTiles, sFootOam[cell], 4, OPT_FOOT_SPRITE_X, OPT_FOOT_SPRITE_Y, FALSE);
    CopyWindowToVram(cv);
}

/* Red row frame and footer buttons for the current cursor. */
static void GmsShowCursor(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    BOOL onFooter = c->cursor >= RULE_COUNT;
    for (u32 k = 0; k < 2; k++) {
        GmsDrawFooter(c, k, (onFooter && c->cursor == RULE_COUNT + k) ? FOOT_SELECTED : FOOT_NORMAL);
    }
    if (!onFooter) {
        ScheduleSetBgPosText(d->bgConfig, BG_SUB_1, BG_POS_SET_Y, OPT_FRAME_Y(c->cursor));
        c->descRule = c->cursor; /* on a footer button the pane keeps the last rule's description */
    }
    GmsLayer(BG_SUB_1, onFooter ? 0 : 1);
}

static void GmsUpdateCursor(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsShowCursor(c, d);
    GmsPaneText(c, d, GmsRuleDesc(c, c->descRule));
}

/* Options backdrop: retail's NSCR 17 rows copied into the layout of this rule list (light and dark rows alternate, as in
 * retail; the light row has its third box erased so every row is an ON/OFF row). */
static void GmsBuildBackdrop(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    u8 *raw = GmsReadMember(d, NARC_OPTIONS, OPT_NSCR_BG, NSCR_MAGIC);
    if (raw == NULL) {
        return;
    }
    const u16 *src = (const u16 *)(raw + NSCR_ENTRIES_OFFSET);
    u16 *map = c->map;
    for (u32 y = 0; y < SCREEN_H; y++) {
        for (u32 x = 0; x < SCREEN_W; x++) {
            u32 sy;
            u32 sx = x;
            if (y < OPT_HEADER_ROWS) {
                sy = y;
            } else if (y < OPT_ROW_TOP(RULE_COUNT)) {
                u32 row = (y - OPT_HEADER_ROWS) / OPT_ROW_TILES;
                u32 r = (y - OPT_HEADER_ROWS) % OPT_ROW_TILES;
                if ((row & 1) == 0) {
                    sy = OPT_SRC_LIGHT_ROW + r;
                    if (x >= OPT_SRC_THIRD_BOX_X0 && x <= OPT_SRC_THIRD_BOX_X1) {
                        sx = OPT_SRC_GAP_COL;
                    }
                } else {
                    sy = OPT_SRC_DARK_ROW + r;
                }
            } else if (y == OPT_ROW_TOP(RULE_COUNT)) {
                sy = OPT_SRC_EDGE_ROW;
            } else {
                sy = OPT_SRC_FILL_ROW;
            }
            u16 v = src[sy * SCREEN_W + sx];
            map[y * SCREEN_W + x] = (v & 0x0FFF) | ((((v >> 12) & 0xF) + SUB_PAL_OPT_BG) << 12);
        }
    }
    GmsFreeMember(d, raw);
    LoadRectToBgTilemapRect(d->bgConfig, BG_SUB_3, map, 0, 0, SCREEN_W, SCREEN_H);
    BgCommitTilemapBufferToVram(d->bgConfig, BG_SUB_3);
}

static void GmsBuildRules(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsRemoveWindows(c);
    GmsFreeTiles(c, d);
    c->boxTiles = GmsReadMember(d, NARC_OPTIONS, OPT_NCGR_BOX, NCGR_MAGIC);
    c->footTiles = GmsReadMember(d, NARC_OPTIONS, OPT_NCGR_FOOT, NCGR_MAGIC);
    GmsClear(d, BG_SUB_0);
    GmsClear(d, BG_SUB_2);

    /* palettes, then the backdrop on SUB_3 */
    GmsLoadPalette(d, NARC_OPTIONS, OPT_NCLR_BG, SUB_PAL_OPT_BG, 2);
    GmsLoadPalette(d, NARC_OPTIONS, OPT_NCLR_BOX, SUB_PAL_OPT_BOX, 1);
    GmsLoadPalette(d, NARC_OPTIONS, OPT_NCLR_FOOT, SUB_PAL_OPT_FOOT, 1);
    GfGfxLoader_LoadCharData(NARC_OPTIONS, OPT_NCGR_BG, d->bgConfig, BG_SUB_3, 0, 0, FALSE, d->heapID);
    c->sub3IsOptions = 1;
    GmsBuildBackdrop(c, d);

    /* red row frame on SUB_1 */
    GfGfxLoader_LoadCharData(NARC_OPTIONS, OPT_NCGR_BG, d->bgConfig, BG_SUB_1, 0, 0, FALSE, d->heapID);
    GfGfxLoader_LoadScrnData(NARC_OPTIONS, OPT_NSCR_FRAME, d->bgConfig, BG_SUB_1, 0, 0, FALSE, d->heapID);
    ov53_OakSpeech_FillBgLayerWithPalette(d, BG_SUB_1, SUB_PAL_OPT_BG + 1);

    /* pixel canvases on SUB_2: one per rule row, then the two footer buttons */
    for (u32 i = 0; i < RULE_COUNT; i++) {
        GmsAddCanvas(c, d, OPT_CANVAS_COL, OPT_ROW_TOP(i), OPT_CANVAS_W, OPT_ROW_TILES, SUB_PAL_OPT_BOX);
    }
    GmsAddCanvas(c, d, OPT_FOOT_CONFIRM_COL, OPT_FOOT_ROW, OPT_FOOT_W, OPT_FOOT_H, SUB_PAL_OPT_FOOT);
    GmsAddCanvas(c, d, OPT_FOOT_CANCEL_COL, OPT_FOOT_ROW, OPT_FOOT_W, OPT_FOOT_H, SUB_PAL_OPT_FOOT);

    /* text windows on SUB_0: header tab, rule names and values, footer labels */
    struct Window *w = GmsAddText(c, d, OPT_LABEL_COL, 0, OPT_HDR_W, OPT_ROW_TILES);
    GmsPrint(c, d, w, c->presets[c->mode].nameMsg, 0, 2, 0, 5, TXT_WHITE);
    CopyWindowToVram(w);
    for (u32 i = 0; i < RULE_COUNT; i++) {
        w = GmsAddText(c, d, OPT_LABEL_COL, OPT_ROW_TOP(i), OPT_LABEL_W, OPT_ROW_TILES);
        GmsPrint(c, d, w, c->rules[i].labelMsg, 0, 4, 0, 5, TXT_WHITE);
        CopyWindowToVram(w);
        GmsAddText(c, d, OPT_VALUE_COL, OPT_ROW_TOP(i), OPT_VALUE_W, OPT_ROW_TILES);
    }
    for (u32 k = 0; k < 2; k++) {
        w = GmsAddText(c, d, k == 0 ? OPT_FOOT_CONFIRM_COL : OPT_FOOT_CANCEL_COL, OPT_FOOT_ROW, OPT_FOOT_W, OPT_ROW_TILES);
        GmsPrint(c, d, w, GMS_MSG_CONFIRM + k, 0, 0, OPT_FOOT_W * 8, 6, TXT_WHITE);
        CopyWindowToVram(w);
    }
    for (u32 i = 0; i < RULE_COUNT; i++) {
        GmsDrawRuleRow(c, d, i);
    }
    GmsShowCursor(c, d);

    GmsLayer(BG_SUB_3, 1);
    GmsLayer(BG_SUB_2, 1);
    GmsLayer(BG_SUB_0, 1);
}

static void GmsBuildScreen(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    if (c->screen == SCR_MODES) {
        GmsBuildModes(c, d);
    } else {
        GmsBuildRules(c, d);
    }
    c->promptMsg = GmsTopMsg(c);
    c->promptY = 0xFFFF;
    c->promptH = 0xFFFF;
    if (c->screen == SCR_RULES) {
        /* keep the prompt above the pane: its window is as tall as its text, so place it by line count */
        u32 rows = 2 * GmsCountLines(c, d, c->promptMsg);
        c->promptH = rows;
        c->promptY = rows < PROMPT_LAST_ROW + 1 ? (PROMPT_LAST_ROW + 1 - rows) / 2 : 0;
    }
}

static void GmsToModes(struct GmsCtx *c)
{
    c->screen = SCR_MODES;
    c->cursor = c->mode - GMS_MODE_OVER_EASY;
    c->ruleValues = 0;
    GmsLeavePrompt(c, PH_BUILD);
}

static void GmsFootPress(struct GmsCtx *c, struct OakSpeechDataView *d, u32 k)
{
    PlaySE(k == 0 ? SEQ_SE_DP_SAVE : SEQ_SE_GS_GEARCANCEL); /* the Options app's own CONFIRM / QUIT sounds */
    c->cursor = RULE_COUNT + k;
    c->footerSel = k;
    c->footAction = k;
    c->footPress = 0;
    GmsUpdateCursor(c, d);
    GmsSetPhase(c, PH_FOOT_PRESS);
}

static void GmsSelectRow(struct GmsCtx *c, struct OakSpeechDataView *d, u32 i)
{
    GmsSE();
    c->cursor = i;
    GmsUpdateCursor(c, d);
}

static void GmsFlipRule(struct GmsCtx *c, struct OakSpeechDataView *d, u32 i)
{
    c->ruleValues ^= 1u << i;
    GmsDrawRuleRow(c, d, i);
}

static BOOL GmsHit(const struct GmsIn *in, u32 x0, u32 x1, u32 y0, u32 y1)
{
    return in->touch && in->x >= x0 && in->x <= x1 && in->y >= y0 && in->y <= y1;
}

/* Screens B and C: Options-style input. */
static void GmsInputRules(struct GmsCtx *c, struct OakSpeechDataView *d, const struct GmsIn *in)
{
    BOOL editable = GmsRulesEditable(c);

    if (in->touch) {
        if (GmsHit(in, OPT_HIT_CONFIRM_X0, OPT_HIT_CONFIRM_X1, OPT_HIT_FOOT_Y0, OPT_HIT_FOOT_Y1)) {
            GmsFootPress(c, d, 0);
            return;
        }
        if (GmsHit(in, OPT_HIT_CANCEL_X0, OPT_HIT_CANCEL_X1, OPT_HIT_FOOT_Y0, OPT_HIT_FOOT_Y1)) {
            GmsFootPress(c, d, 1);
            return;
        }
        for (u32 i = 0; i < RULE_COUNT; i++) {
            if (!GmsHit(in, 0, 255, OPT_ROW_Y(i), OPT_ROW_Y(i) + 8 * OPT_ROW_TILES - 1)) {
                continue;
            }
            BOOL onBox = GmsHit(in, OPT_HIT_ON_X0, OPT_HIT_ON_X1, OPT_HIT_BOX_Y0(i), OPT_HIT_BOX_Y1(i));
            BOOL onOff = GmsHit(in, OPT_HIT_OFF_X0, OPT_HIT_OFF_X1, OPT_HIT_BOX_Y0(i), OPT_HIT_BOX_Y1(i));
            if (editable && (onBox || onOff) && GmsRuleIsOn(c, i) != onBox) {
                GmsFlipRule(c, d, i);
            }
            GmsSelectRow(c, d, i);
            return;
        }
        return;
    }

    if (in->keys & PAD_BUTTON_B) {
        GmsFootPress(c, d, 1);
        return;
    }
    if (in->keys & PAD_KEY_UP) {
        if (c->cursor == 0) {
            c->cursor = RULE_COUNT + c->footerSel;
        } else if (c->cursor >= RULE_COUNT) {
            c->cursor = RULE_COUNT - 1;
        } else {
            c->cursor--;
        }
        GmsSE();
        GmsUpdateCursor(c, d);
    } else if (in->keys & PAD_KEY_DOWN) {
        if (c->cursor >= RULE_COUNT) {
            c->cursor = 0;
        } else if (c->cursor == RULE_COUNT - 1) {
            c->cursor = RULE_COUNT + c->footerSel;
        } else {
            c->cursor++;
        }
        GmsSE();
        GmsUpdateCursor(c, d);
    } else if (in->keys & (PAD_KEY_LEFT | PAD_KEY_RIGHT)) {
        if (c->cursor >= RULE_COUNT) {
            c->footerSel ^= 1;
            c->cursor = RULE_COUNT + c->footerSel;
            GmsSE();
            GmsUpdateCursor(c, d);
        } else if (editable) {
            BOOL wantOn = (in->keys & PAD_KEY_LEFT) ? TRUE : FALSE; /* ON is the left box */
            if (GmsRuleIsOn(c, c->cursor) != wantOn) {
                GmsSE();
                GmsFlipRule(c, d, c->cursor);
            }
        }
    } else if (in->keys & PAD_BUTTON_A) {
        if (c->cursor >= RULE_COUNT) {
            GmsFootPress(c, d, c->cursor - RULE_COUNT);
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The machine
 * ------------------------------------------------------------------------------------------------------ */

/* Takes the toggle screen off the lower display and puts retail's backdrop back, for the yes/no. */
static void GmsTeardownRules(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsRemoveWindows(c);
    GmsFreeTiles(c, d);
    GmsLayer(BG_SUB_1, 0);
    GmsLayer(BG_SUB_2, 0);
    GmsClear(d, BG_SUB_0);
    GmsClear(d, BG_SUB_1);
    GmsClear(d, BG_SUB_2);
    GmsRestoreRetailBackdrop(c, d);
}

/* Returns TRUE once Game Mode Select has finished and Oak's speech may resume at state 44. */
static BOOL GmsStep(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    struct GmsIn in;

    switch (c->phase) {
    case PH_ENTER:
        /* Mirrors retail state 7 (FADE_IN_TUTORIAL_MENU). */
        ov53_OakSpeech_TouchToAdvanceButtonAction(d, 0); /* hidden: its area would turn touches into a simulated A */
        GmsClear(d, BG_SUB_0);
        ov53_OakSpeech_SetButtonTutorialScreenLayout(d, 1);
        ov53_021E67C4(d, 0);
        GmsLayer(BG_MAIN_3, 1);
        GmsLayer(BG_MAIN_0, 1);
        GmsLayer(BG_SUB_0, 1);
        GmsLayer(BG_SUB_3, 1);
        GmsBuildScreen(c, d);
        GmsFade(d, 1);
        GmsSetPhase(c, PH_ENTER_WAIT);
        break;
    case PH_ENTER_WAIT:
        if (WIPE_SYS_EndCheck() == 1) {
            GmsSetPhase(c, PH_PROMPT_IN);
        }
        break;
    case PH_BUILD:
        GmsBuildScreen(c, d);
        GmsSetPhase(c, PH_PROMPT_IN);
        break;
    case PH_PROMPT_IN:
        if (ov53_OakSpeech_PrintAndFadeFullScreenText(d, c->promptMsg, 2, c->promptY, c->promptH) == TRUE) {
            c->promptShown = 1;
            if (c->screen == SCR_RULES) {
                GmsPaneShow(c, d);
                GmsPaneText(c, d, GmsRuleDesc(c, c->descRule));
            }
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
        if (!c->promptShown || ov53_OakSpeech_PrintAndFadeFullScreenText(d, c->promptMsg, 2, c->promptY, c->promptH) == TRUE) {
            c->promptShown = 0;
            GmsSetPhase(c, c->afterOut);
        }
        break;
    case PH_FOOT_PRESS: {
        u32 k = c->footAction;
        c->footPress++;
        if (c->footPress == 1) {
            GmsDrawFooter(c, k, FOOT_PRESS_A);
        } else if (c->footPress == FOOT_PRESS_FRAMES / 2) {
            GmsDrawFooter(c, k, FOOT_SELECTED);
        } else if (c->footPress >= FOOT_PRESS_FRAMES) {
            if (k == 1) {
                GmsToModes(c);
            } else if (c->presets[c->mode].selectable) {
                GmsPaneText(c, d, GMS_MSG_SURE);
                GmsSetPhase(c, PH_SURE_YESNO_START);
            } else {
                GmsPaneText(c, d, GMS_MSG_SORRY);
                GmsSetPhase(c, PH_SORRY);
            }
        }
        break;
    }
    case PH_SORRY:
        GmsReadInput(&in);
        if ((in.keys & (PAD_BUTTON_A | PAD_BUTTON_B)) || in.touch) {
            GmsSE();
            GmsPaneText(c, d, GmsRuleDesc(c, c->descRule));
            GmsSetPhase(c, PH_INPUT);
        }
        break;
    case PH_SURE_YESNO_START:
        /* Retail's "understood?" sequence (oaks_speech.c:1719-1722) on a lower screen reset to retail's backdrop. */
        GmsTeardownRules(c, d);
        ov53_OakSpeechYesNo_SetBackgroundPalette(d->yesnoMenu, 7);
        ov53_OakSpeechYesNo_Start(d->yesnoMenu, 219, 61, 62);
        GmsSetPhase(c, PH_SURE_YESNO);
        break;
    case PH_SURE_YESNO: {
        int r = ov53_OakSpeechYesNo_Main(d->yesnoMenu);
        if (r == YESNO_RESPONSE_YES) {
            GameRules_Commit(c->mode, c->ruleValues);
            GmsLeavePrompt(c, PH_LEAVE_FADE);
        } else if (r == YESNO_RESPONSE_NO) {
            /* back to the toggle list with the same values, cursor on Confirm */
            c->cursor = RULE_COUNT;
            c->footerSel = 0;
            GmsBuildRules(c, d);
            GmsUpdateCursor(c, d);
            GmsSetPhase(c, PH_INPUT);
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
            /* SUB_0 and SUB_2 were switched off by the yes/no, SUB_1 is off. */
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

static struct GmsCtx *GmsCreate(struct OakSpeechDataView *d, const struct GmsServices *svc)
{
    struct GmsCtx *c = &sGmsCtx;
    memset(c, 0, sizeof(*c));
    c->rules = svc->rules;
    c->presets = svc->presets;
    c->retailMsg = d->msgData;
    c->msg40 = NewMsgDataFromNarc(MSGDATA_LOAD_LAZY, 27, 40, d->heapID);
    c->screen = SCR_MODES;
    c->phase = PH_ENTER;
    return c;
}

static void GmsDestroy(struct GmsCtx *c, struct OakSpeechDataView *d)
{
    GmsRemoveWindows(c);
    GmsPaneHide(c);
    GmsFreeTiles(c, d);
    DestroyMsgData(c->msg40);
    c->msg40 = NULL;
}

/* Entry point of overlay 152: must stay the first function in the image (section .init). */
BOOL __attribute__((section(".init"))) GmsUi_Entry(struct OakSpeechDataView *d, u32 op, const struct GmsServices *svc)
{
    switch (op) {
    case 0:
        sGms = GmsCreate(d, svc);
        return FALSE;
    case 1:
        return GmsRun(sGms, d);
    default:
        GmsDestroy(sGms, d);
        sGms = NULL;
        return TRUE;
    }
}
