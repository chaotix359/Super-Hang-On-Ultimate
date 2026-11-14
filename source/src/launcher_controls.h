/* The launcher's CONTROLS screen (part of launcher_core.c, which includes this file once: it uses its drawing
 * helpers). Two tabs:
 *   CONTROLLER - a device drop-down (any controller, or one of those the system has), an Xbox-style pad drawn with
 *     what each button does beside it (click a button or its label for a list of actions; the pad's own presses
 *     light up), CONFIGURE WITH CONTROLLER (asks for each of the game's controls in turn), RESET TO DEFAULTS.
 *   KEYBOARD - each action with up to four keys (click a box, or Enter on it, then press the key).
 * Changes are saved as they are made (host->save).
 */
static const lc_controls_host *host;
static int page;                   /* 0 = the settings, 1 = the controls screen */
static int tab;                    /* 0 = controller, 1 = keyboard */
static int foc;                    /* the line / box the keyboard cursor is on */
static int pop, pop_x, pop_y, pop_w, pop_n, pop_cur, pop_btn;   /* a drop-down list: 1 = devices, 2 = actions (for button pop_btn) */
static int wiz = -1, wiz_armed; static unsigned char wiz_keep[PB_COUNT];   /* configure with controller: the action being asked for (-1 = off) */
static unsigned wiz_prev;
static int cap_a = -1, cap_s;      /* keyboard: waiting for a key for this action / slot */
static double ctl_now; static unsigned nav_prev = ~0u; static double nav_next;   /* the pad moving through this screen (controls_pad_nav) */
static sho_pad live; static int live_ok;   /* the chosen controller's state now (for the diagram) */

void lc_set_controls_host(const lc_controls_host *h) { host = h; }
int lc_wants_key(void) { return page == 1 && cap_a >= 0; }

/* ---- drawing helpers ---- */
static void frame(int x, int y, int w, int h, uint32_t c) { fill(x, y, w, 1, c); fill(x, y + h - 1, w, 1, c); fill(x, y, 1, h, c); fill(x + w - 1, y, 1, h, c); }
static void disc(int cx, int cy, int r, uint32_t c) { for (int y = -r; y <= r; y++) for (int x = -r; x <= r; x++) if (x * x + y * y <= r * r + r / 2) put(cx + x, cy + y, c); }
static void rrect(int x, int y, int w, int h, int r, uint32_t c) {   /* a rounded box */
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    fill(x + r, y, w - 2 * r, h, c); fill(x, y + r, w, h - 2 * r, c);
    disc(x + r, y + r, r, c); disc(x + w - 1 - r, y + r, r, c); disc(x + r, y + h - 1 - r, r, c); disc(x + w - 1 - r, y + h - 1 - r, r, c);
}
static void line(int x0, int y0, int x1, int y1, uint32_t c) {
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { put(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}
#define OPAQUE(c) (0xFF000000u | (c))
#define C_PANEL  0xF4040A24u       /* the controls screen's panel: darker than the settings panel */
#define C_BOX    0xFF10184Au
#define C_BODY   0xC8CCD8
#define C_BODY2  0x9CA0B4
#define C_DARK   0x30344C
#define C_LINE   0x7078A0

/* ---- the controller tab's geometry ---- */
/* the sixteen buttons: where they are on the drawing (centre and half-size, for clicks and the leader line), which
   side their label is on and which row, and the short name beside it */
typedef struct { int cx, cy, hw, hh, side, row; const char *tag; } btn_geo;
static const btn_geo geo[PB_COUNT] = {
    [PB_LT]    = { 264,  86, 18,  8, 0, 0, "LT" },    [PB_RT]    = { 376,  86, 18,  8, 1, 0, "RT" },
    [PB_LB]    = { 264, 102, 28,  6, 0, 1, "LB" },    [PB_RB]    = { 376, 102, 28,  6, 1, 1, "RB" },
    [PB_L3]    = { 262, 136, 13, 13, 0, 2, "L3" },    [PB_Y]     = { 388, 122,  8,  8, 1, 2, "Y" },
    [PB_BACK]  = { 304, 136,  6,  5, 0, 3, "BACK" },  [PB_X]     = { 374, 136,  8,  8, 1, 3, "X" },
    [PB_UP]    = { 292, 163,  5,  5, 0, 4, "UP" },    [PB_B]     = { 402, 136,  8,  8, 1, 4, "B" },
    [PB_LEFT]  = { 283, 172,  5,  5, 0, 5, "LEFT" },  [PB_A]     = { 388, 150,  8,  8, 1, 5, "A" },
    [PB_DOWN]  = { 292, 181,  5,  5, 0, 6, "DOWN" },  [PB_START] = { 336, 136,  6,  5, 1, 6, "START" },
    [PB_RIGHT] = { 301, 172,  5,  5, 0, 7, "RIGHT" }, [PB_R3]    = { 348, 172, 13, 13, 1, 7, "R3" },
};
/* the order the keyboard cursor goes through them: the left column top to bottom, then the right */
static const int btn_order[PB_COUNT] = { PB_LT, PB_LB, PB_L3, PB_BACK, PB_UP, PB_LEFT, PB_DOWN, PB_RIGHT, PB_RT, PB_RB, PB_Y, PB_X, PB_B, PB_A, PB_START, PB_R3 };
#define ROW_Y(r) (84 + (r) * 17)
#define TAB_Y 22
#define DEV_Y 44
#define DEV_X 104
#define DEV_W 400
#define BTN_Y 290                  /* the buttons along the bottom */
enum { CF_TAB, CF_DEV, CF_BTN, CF_WIZ = CF_BTN + PB_COUNT, CF_RESET, CF_BACK, CF_COUNT };   /* controller tab focus */
enum { KF_TAB, KF_KEY, KF_RESET = KF_KEY + KA_COUNT * KEY_SLOTS, KF_BACK, KF_COUNT };       /* keyboard tab focus */
#define KROW_Y(a) (74 + (a) * 22)
#define KCOL_X(s) (176 + (s) * 108)
#define KCOL_W 100

static int dev_count(void) { return host && host->pad_count ? host->pad_count() : 0; }
static const char *dev_name(int i) { const char *n = host && host->pad_name ? host->pad_name(i) : NULL; return n && n[0] ? n : "CONTROLLER"; }
static int dev_index(void) {       /* the chosen device among those connected: -1 = any, -2 = chosen but not connected */
    if (!sho_ctl.device[0]) return -1;
    for (int i = 0; i < dev_count(); i++) if (!strcmp(dev_name(i), sho_ctl.device)) return i;
    return -2;
}
static void ctl_save(void) { if (host && host->save) host->save(); }
static void set_device(int i) {    /* -1 = any */
    if (i < 0) sho_ctl.device[0] = 0; else snprintf(sho_ctl.device, sizeof sho_ctl.device, "%s", dev_name(i));
    ctl_save();
}

/* a button along the bottom (returns its width) */
static int button(int x, int y, const char *label, int focused) {
    int w = text_w(label, 1) + 16;
    fill(x, y, w, 16, focused ? OPAQUE(0x283070) : C_BOX);
    frame(x, y, w, 16, OPAQUE(focused ? YELLOW : C_LINE));
    text(x + 8, y + 4, label, focused ? YELLOW : WHITE, 1);
    return w;
}
static const char *const bottom_c[3] = { "CONFIGURE WITH CONTROLLER", "RESET TO DEFAULTS", "BACK" }, *const bottom_k[2] = { "RESET TO DEFAULTS", "BACK" };
static int bottom_x(const char *const *labels, int n, int i) {   /* the row is centred */
    int tot = 0; for (int k = 0; k < n; k++) tot += text_w(labels[k], 1) + 16 + (k ? 16 : 0);
    int x = (CW - tot) / 2;
    for (int k = 0; k < i; k++) x += text_w(labels[k], 1) + 32;
    return x;
}

static void draw_pad_picture(void) {
    unsigned on = live_ok ? live.buttons : 0;
    #define LIT(b, c) OPAQUE(((on >> (b)) & 1) ? ORANGE : (c))
    /* triggers and bumpers (behind the body) */
    rrect(246 - 2, 78 - 2, 36 + 4, 22 + 4, 7, OPAQUE(OUTLINE)); rrect(246, 78, 36, 22, 6, LIT(PB_LT, C_BODY2));
    rrect(358 - 2, 78 - 2, 36 + 4, 22 + 4, 7, OPAQUE(OUTLINE)); rrect(358, 78, 36, 22, 6, LIT(PB_RT, C_BODY2));
    rrect(236 - 2, 96 - 2, 56 + 4, 20 + 4, 7, OPAQUE(OUTLINE)); rrect(236, 96, 56, 20, 6, LIT(PB_LB, C_BODY2));
    rrect(348 - 2, 96 - 2, 56 + 4, 20 + 4, 7, OPAQUE(OUTLINE)); rrect(348, 96, 56, 20, 6, LIT(PB_RB, C_BODY2));
    /* the body: outline first, then the shell */
    for (int pass = 0; pass < 2; pass++) {
        int g = pass ? 0 : 2; uint32_t c = OPAQUE(pass ? C_BODY : OUTLINE);
        rrect(228 - g, 108 - g, 184 + 2 * g, 78 + 2 * g, 26 + g, c);
        rrect(228 - g, 150 - g, 54 + 2 * g, 84 + 2 * g, 24 + g, c);
        rrect(358 - g, 150 - g, 54 + 2 * g, 84 + 2 * g, 24 + g, c);
    }
    fill(282, 186, 76, 2, OPAQUE(OUTLINE));
    /* sticks (a press lights the cap; the left one also leans with the stick) */
    { int ox = live_ok ? (int)(live.lx * 4) : 0, oy = live_ok ? (int)(-live.ly * 4) : 0;
      disc(262, 136, 14, OPAQUE(C_DARK)); disc(262 + ox, 136 + oy, 9, LIT(PB_L3, C_BODY2));
      disc(348, 172, 14, OPAQUE(C_DARK)); disc(348, 172, 9, LIT(PB_R3, C_BODY2)); }
    /* d-pad */
    fill(286, 158, 12, 28, OPAQUE(C_DARK)); fill(278, 166, 28, 12, OPAQUE(C_DARK));
    fill(288, 160, 8, 8, LIT(PB_UP, 0x585C78)); fill(288, 176, 8, 8, LIT(PB_DOWN, 0x585C78));
    fill(280, 168, 8, 8, LIT(PB_LEFT, 0x585C78)); fill(296, 168, 8, 8, LIT(PB_RIGHT, 0x585C78));
    /* A B X Y in their colours */
    static const struct { int b; char l; uint32_t c; } face[4] = { { PB_A, 'A', 0x20A840 }, { PB_B, 'B', 0xD03030 }, { PB_X, 'X', 0x3060D8 }, { PB_Y, 'Y', 0xD8A800 } };
    for (int i = 0; i < 4; i++) {
        const btn_geo *g = &geo[face[i].b]; char s[2] = { face[i].l, 0 };
        disc(g->cx, g->cy, 9, OPAQUE(OUTLINE)); disc(g->cx, g->cy, 8, LIT(face[i].b, face[i].c));
        text(g->cx - 4, g->cy - 4, s, WHITE, 1);
    }
    /* back, start, and the middle button (not used) */
    rrect(298, 132, 13, 9, 4, OPAQUE(OUTLINE)); rrect(299, 133, 11, 7, 3, LIT(PB_BACK, C_DARK));
    rrect(330, 132, 13, 9, 4, OPAQUE(OUTLINE)); rrect(331, 133, 11, 7, 3, LIT(PB_START, C_DARK));
    disc(320, 120, 7, OPAQUE(C_BODY2));
    /* the triggers fill as they are pulled */
    if (live_ok && live.lt > 0.04f) fill(248, 80, (int)(32 * live.lt), 3, OPAQUE(YELLOW));
    if (live_ok && live.rt > 0.04f) fill(360, 80, (int)(32 * live.rt), 3, OPAQUE(YELLOW));
    #undef LIT
}

static void draw_popup(void) {
    if (!pop) return;
    int h = pop_n * 12 + 6;
    fill(pop_x - 1, pop_y - 1, pop_w + 2, h + 2, OPAQUE(OUTLINE));
    fill(pop_x, pop_y, pop_w, h, OPAQUE(0x182058)); frame(pop_x, pop_y, pop_w, h, OPAQUE(YELLOW));
    for (int i = 0; i < pop_n; i++) {
        const char *t; char b[64];
        if (pop == 1) { snprintf(b, sizeof b, "%.46s", i == 0 ? "ANY CONTROLLER" : dev_name(i - 1)); t = b; }
        else t = i == ACT_NONE ? "NOTHING" : sho_action_name(i);
        if (i == pop_cur) fill(pop_x + 2, pop_y + 2 + i * 12, pop_w - 4, 12, OPAQUE(0x3040A0));
        text(pop_x + 8, pop_y + 4 + i * 12, t, i == pop_cur ? YELLOW : WHITE, 1);
    }
}

static void draw_tabs(void) {
    static const char *const tabs[2] = { "CONTROLLER", "KEYBOARD" };
    int x = 32;
    for (int i = 0; i < 2; i++) {
        int w = text_w(tabs[i], 1) + 20, act = tab == i;
        fill(x, TAB_Y - 6, w, 20, act ? OPAQUE(0x283070) : C_BOX);
        frame(x, TAB_Y - 6, w, 20, OPAQUE(act ? YELLOW : C_LINE));
        if (act) fill(x + 1, TAB_Y + 13, w - 2, 1, OPAQUE(0x283070));
        text(x + 10, TAB_Y, tabs[i], act ? YELLOW : (foc == 0 ? WHITE : LIGHT), 1);
        x += w + 4;
    }
    if (foc == 0 && blink) text(20, TAB_Y, ">", YELLOW, 1);
    text(CW - 32 - text_w("CONTROLS", 1), TAB_Y, "CONTROLS", LIGHT, 1);
    fill(24, TAB_Y + 14, CW - 48, 1, OPAQUE(C_LINE));
}

static void draw_controller_tab(void) {
    /* the device drop-down */
    int di = dev_index(); char dn[80];
    if (di == -1) snprintf(dn, sizeof dn, "ANY CONTROLLER%s", dev_count() ? "" : " (NONE CONNECTED)");
    else if (di == -2) snprintf(dn, sizeof dn, "%.32s (NOT CONNECTED)", sho_ctl.device);
    else snprintf(dn, sizeof dn, "%.48s", sho_ctl.device);
    text(40, DEV_Y + 4, "DEVICE", foc == CF_DEV ? YELLOW : WHITE, 1);
    fill(DEV_X, DEV_Y, DEV_W, 16, C_BOX); frame(DEV_X, DEV_Y, DEV_W, 16, OPAQUE(foc == CF_DEV ? YELLOW : C_LINE));
    text(DEV_X + 8, DEV_Y + 4, dn, di == -2 ? ORANGE : WHITE, 1);
    fill(DEV_X + DEV_W - 17, DEV_Y + 1, 1, 14, OPAQUE(C_LINE));
    for (int i = 0; i < 4; i++) fill(DEV_X + DEV_W - 12 + i, DEV_Y + 6 + i, 8 - 2 * i, 1, OPAQUE(YELLOW));   /* the drop-down's arrow */
    /* leader lines, then the pad, then the labels */
    for (int b = 0; b < PB_COUNT; b++) {
        const btn_geo *g = &geo[b]; int y = ROW_Y(g->row) + 4, sel = foc == CF_BTN + b || (pop == 2 && pop_btn == b);
        uint32_t c = OPAQUE(sel ? YELLOW : C_LINE);
        if (g->side == 0) { line(218, y, 224, y, c); line(224, y, g->cx - g->hw, g->cy, c); }
        else { line(421, y, 415, y, c); line(415, y, g->cx + g->hw, g->cy, c); }
    }
    draw_pad_picture();
    for (int b = 0; b < PB_COUNT; b++) {
        const btn_geo *g = &geo[b]; int y = ROW_Y(g->row), sel = foc == CF_BTN + b || (pop == 2 && pop_btn == b);
        const char *a = sho_ctl.pad[b] == ACT_NONE ? "-" : sho_action_name(sho_ctl.pad[b]);
        int lit = live_ok && ((live.buttons >> b) & 1);
        uint32_t ca = sel ? YELLOW : sho_ctl.pad[b] == ACT_NONE ? DIM : WHITE, ct = lit ? ORANGE : LIGHT;
        if (g->side == 0) {
            text(216 - text_w(g->tag, 1), y, g->tag, ct, 1);
            text(168 - text_w(a, 1), y, a, ca, 1);
            if (sel && blink) text(168 - text_w(a, 1) - 12, y, ">", YELLOW, 1);
        } else {
            text(424, y, g->tag, ct, 1);
            text(472, y, a, ca, 1);
            if (sel && blink) text(472 + text_w(a, 1) + 4, y, "<", YELLOW, 1);
        }
        if (sel) { const btn_geo *q = g; frame(q->cx - q->hw - 2, q->cy - q->hh - 2, q->hw * 2 + 5, q->hh * 2 + 5, OPAQUE(YELLOW)); }
    }
    text((CW - text_w("THE LEFT STICK ALWAYS STEERS. THE STICK AND D-PAD MOVE THROUGH MENUS.", 1)) / 2, 250,
         "THE LEFT STICK ALWAYS STEERS. THE STICK AND D-PAD MOVE THROUGH MENUS.", DIM, 1);
    text((CW - text_w("IN ORIGINAL MODE'S MENUS A SELECTS AND B GOES BACK.", 1)) / 2, 262, "IN ORIGINAL MODE'S MENUS A SELECTS AND B GOES BACK.", DIM, 1);
    for (int i = 0; i < 3; i++) button(bottom_x(bottom_c, 3, i), BTN_Y, bottom_c[i], foc == CF_WIZ + i);
    const char *help = foc >= CF_BTN && foc < CF_WIZ ? "ENTER OR A ON THE PAD OPENS THE LIST OF WHAT IT CAN DO" :
                       foc == CF_DEV ? "ENTER OR A ON THE PAD LISTS THE CONTROLLERS" :
                       "ARROWS OR D-PAD MOVE   ENTER OR A SELECTS";
    text((CW - text_w(help, 1)) / 2, 318, help, LIGHT, 1);
    if (wiz >= 0) {   /* configure with controller: asks for each control in turn */
        int x = 140, y = 104, w = 360, h = 132; char b[64];
        fill(x - 1, y - 1, w + 2, h + 2, OPAQUE(OUTLINE)); fill(x, y, w, h, OPAQUE(0x101848)); frame(x, y, w, h, OPAQUE(YELLOW));
        text(x + (w - text_w("CONFIGURE WITH CONTROLLER", 1)) / 2, y + 10, "CONFIGURE WITH CONTROLLER", LIGHT, 1);
        text(x + (w - text_w("PRESS THE BUTTON FOR", 1)) / 2, y + 34, "PRESS THE BUTTON FOR", WHITE, 1);
        const char *a = sho_action_name(wiz);
        text(x + (w - text_w(a, 2)) / 2, y + 52, a, blink ? YELLOW : WHITE, 2);
        snprintf(b, sizeof b, "%d OF %d", wiz, ACT_COUNT - 1);
        text(x + (w - text_w(b, 1)) / 2, y + 78, b, LIGHT, 1);
        const char *note = (wiz == ACT_LEFT || wiz == ACT_RIGHT) ? "(THE LEFT STICK STEERS AS WELL)" : !live_ok ? "NO CONTROLLER FOUND" : "";
        text(x + (w - text_w(note, 1)) / 2, y + 94, note, live_ok ? DIM : ORANGE, 1);
        text(x + (w - text_w("SPACE SKIPS THIS ONE   ESC CANCELS", 1)) / 2, y + 114, "SPACE SKIPS THIS ONE   ESC CANCELS", DIM, 1);
    }
}

static void draw_keyboard_tab(void) {
    text(40, 52, "ACTION", LIGHT, 1);
    for (int s = 0; s < KEY_SLOTS; s++) { char b[16]; snprintf(b, sizeof b, "KEY %d", s + 1); text(KCOL_X(s) + 8, 52, b, LIGHT, 1); }
    for (int a = 0; a < KA_COUNT; a++) {
        int y = KROW_Y(a), rowsel = foc >= KF_KEY + a * KEY_SLOTS && foc < KF_KEY + (a + 1) * KEY_SLOTS;
        text(40, y + 4, sho_key_action_name(a), rowsel ? YELLOW : WHITE, 1);
        for (int s = 0; s < KEY_SLOTS; s++) {
            int x = KCOL_X(s), f = foc == KF_KEY + a * KEY_SLOTS + s, cap = cap_a == a && cap_s == s, code = sho_ctl.key[a][s];
            fill(x, y, KCOL_W, 16, cap ? OPAQUE(0x503010) : f ? OPAQUE(0x283070) : C_BOX);
            frame(x, y, KCOL_W, 16, OPAQUE(cap ? ORANGE : f ? YELLOW : C_LINE));
            char b[20]; const char *n = code >= 0 && host && host->key_name ? host->key_name(code) : NULL;
            if (cap) snprintf(b, sizeof b, "%s", blink ? "PRESS A KEY" : "");
            if (cap) { text(x + 6, y + 4, b, ORANGE, 1); continue; }
            else if (code < 0) snprintf(b, sizeof b, "-");
            else if (n && n[0]) snprintf(b, sizeof b, "%.11s", n);
            else snprintf(b, sizeof b, "KEY %d", code);
            text(x + 6, y + 4, b, code < 0 ? DIM : f ? YELLOW : WHITE, 1);
        }
    }
    text((CW - text_w("MENUS: THE ARROWS MOVE, ENTER OR SPACE SELECTS, BACKSPACE GOES BACK.", 1)) / 2, 254,
         "MENUS: THE ARROWS MOVE, ENTER OR SPACE SELECTS, BACKSPACE GOES BACK.", DIM, 1);
    text((CW - text_w("THE ACCELERATE AND BRAKE KEYS SELECT AND GO BACK THERE TOO.", 1)) / 2, 266, "THE ACCELERATE AND BRAKE KEYS SELECT AND GO BACK THERE TOO.", DIM, 1);
    for (int i = 0; i < 2; i++) button(bottom_x(bottom_k, 2, i), BTN_Y, bottom_k[i], foc == KF_RESET + i);
    const char *help = cap_a >= 0 ? "PRESS THE KEY TO USE   ESC OR B ON THE PAD CANCELS" :
                       foc >= KF_KEY && foc < KF_RESET ? "ENTER, A OR A CLICK, THEN PRESS THE KEY   DELETE CLEARS IT" : "CLICK A BOX, THEN PRESS THE KEY TO USE";
    text((CW - text_w(help, 1)) / 2, 318, help, LIGHT, 1);
}

static void draw_controls(void) {
    memset(cv, 0, sizeof cv);
    fill(0, 0, CW, CH, 0x70000010u);                 /* the picture dimmed behind it */
    fill(16, 8, CW - 32, CH - 20, C_PANEL); frame(16, 8, CW - 32, CH - 20, OPAQUE(C_LINE));
    draw_tabs();
    if (tab == 0) draw_controller_tab(); else draw_keyboard_tab();
    draw_popup();
    text((CW - text_w("ESC GOES BACK", 1)) / 2, 334, pop ? "ESC OR B CLOSES THE LIST" : wiz >= 0 || cap_a >= 0 ? "" : "ESC GOES BACK", DIM, 1);
}

/* ---- input ---- */
static void controls_open(void) { nav_prev = ~0u; page = 1; tab = 0; foc = dev_count() ? CF_BTN + btn_order[0] : CF_DEV; pop = 0; wiz = -1; cap_a = -1; }
static void controls_close(void) { page = 0; pop = 0; wiz = -1; cap_a = -1; ctl_save(); }
static void open_device_list(void) { pop = 1; pop_x = DEV_X; pop_y = DEV_Y + 17; pop_w = DEV_W; pop_n = 1 + dev_count(); { int d = dev_index(); pop_cur = d >= 0 ? d + 1 : 0; } }
static void open_action_list(int b) {
    const btn_geo *g = &geo[b];
    pop = 2; pop_btn = b; pop_n = ACT_COUNT; pop_w = 104; pop_cur = sho_ctl.pad[b];
    pop_x = g->side == 0 ? 72 : 468; pop_y = ROW_Y(g->row) + 11;
    if (pop_y + pop_n * 12 + 8 > 284) pop_y = ROW_Y(g->row) - pop_n * 12 - 9;
    foc = CF_BTN + b;
}
static void pop_choose(int i) {
    if (pop == 1) set_device(i - 1);
    else if (pop == 2) { sho_ctl.pad[pop_btn] = (unsigned char)i; ctl_save(); }
    pop = 0;
}
static void wiz_start(void) { memcpy(wiz_keep, sho_ctl.pad, sizeof wiz_keep); memset(sho_ctl.pad, 0, sizeof sho_ctl.pad); wiz = ACT_ACCEL; wiz_armed = 0; wiz_prev = ~0u; pop = 0; }
static void wiz_next(void) { wiz_armed = 0; if (++wiz >= ACT_COUNT) { wiz = -1; ctl_save(); } }
static void wiz_cancel(void) { memcpy(sho_ctl.pad, wiz_keep, sizeof wiz_keep); wiz = -1; }
static void cycle_action(int b, int d) { sho_ctl.pad[b] = (unsigned char)((sho_ctl.pad[b] + d + ACT_COUNT) % ACT_COUNT); ctl_save(); }
static void cycle_device(int d) {
    int n = dev_count(), i = dev_index(); if (i == -2) i = -1;
    i += d; if (i < -1) i = n - 1; if (i >= n) i = -1;
    set_device(i);
}
static int order_pos(int b) { for (int i = 0; i < PB_COUNT; i++) if (btn_order[i] == b) return i; return 0; }

static int btn_at(int side, int row) { for (int b = 0; b < PB_COUNT; b++) if (geo[b].side == side && geo[b].row == row) return b; return -1; }
/* pad: 1 = from a controller (controls_tick): on the diagram it moves up and down a column and across to the other one */
static void controls_nav(int k, int pad) {
    if (pad && !pop && cap_a < 0 && wiz < 0 && tab == 0 && foc >= CF_BTN && foc < CF_WIZ && k != LK_ENTER) {
        const btn_geo *g = &geo[foc - CF_BTN]; int b = -1;
        if (k == LK_LEFT || k == LK_RIGHT) b = btn_at(!g->side, g->row);
        else if (k == LK_UP) { b = btn_at(g->side, g->row - 1); if (b < 0) { foc = CF_DEV; return; } }
        else if (k == LK_DOWN) { b = btn_at(g->side, g->row + 1); if (b < 0) { foc = g->side == 0 ? CF_WIZ : CF_BACK; return; } }
        if (b >= 0) foc = CF_BTN + b;
        return;
    }
    if (wiz >= 0) { if (k == LK_ESC) wiz_cancel(); else if (k == LK_SPACE) wiz_next(); return; }
    if (cap_a >= 0) { if (k == LK_ESC) cap_a = -1; return; }
    if (pop) {
        if (k == LK_ESC) pop = 0;
        else if (k == LK_UP) pop_cur = (pop_cur + pop_n - 1) % pop_n;
        else if (k == LK_DOWN) pop_cur = (pop_cur + 1) % pop_n;
        else if (k == LK_ENTER || k == LK_SPACE) pop_choose(pop_cur);
        return;
    }
    if (k == LK_ESC) { controls_close(); return; }
    if (k == LK_TAB) { tab = !tab; foc = 0; return; }
    if (foc == 0 && (k == LK_LEFT || k == LK_RIGHT)) { tab = !tab; return; }
    if (tab == 0) {
        /* the cursor's path: tabs, device, the sixteen buttons (left column, right column), the three bottom buttons */
        int pos = foc == CF_TAB ? 0 : foc == CF_DEV ? 1 : foc < CF_WIZ ? 2 + order_pos(foc - CF_BTN) : 2 + PB_COUNT + (foc - CF_WIZ), n = 2 + PB_COUNT + 3;
        if (k == LK_UP || k == LK_DOWN) {
            pos = (pos + (k == LK_UP ? n - 1 : 1)) % n;
            foc = pos == 0 ? CF_TAB : pos == 1 ? CF_DEV : pos < 2 + PB_COUNT ? CF_BTN + btn_order[pos - 2] : CF_WIZ + (pos - 2 - PB_COUNT);
        } else if (foc == CF_DEV) { if (k == LK_LEFT) cycle_device(-1); else if (k == LK_RIGHT) cycle_device(1); else if (k == LK_ENTER || k == LK_SPACE) open_device_list(); }
        else if (foc >= CF_BTN && foc < CF_WIZ) {
            int b = foc - CF_BTN;
            if (k == LK_LEFT) cycle_action(b, -1); else if (k == LK_RIGHT) cycle_action(b, 1);
            else if (k == LK_ENTER || k == LK_SPACE) open_action_list(b);
            else if (k == LK_CLEAR) { sho_ctl.pad[b] = ACT_NONE; ctl_save(); }
        } else if (foc >= CF_WIZ) {
            if (k == LK_LEFT && foc > CF_WIZ) foc--; else if (k == LK_RIGHT && foc < CF_BACK) foc++;
            else if (k == LK_ENTER || k == LK_SPACE) { if (foc == CF_WIZ) wiz_start(); else if (foc == CF_RESET) { sho_controls_pad_defaults(0); ctl_save(); } else controls_close(); }
        }
    } else {
        if (foc >= KF_KEY && foc < KF_RESET) {
            int a = (foc - KF_KEY) / KEY_SLOTS, s = (foc - KF_KEY) % KEY_SLOTS;
            if (k == LK_UP) foc = a == 0 ? KF_TAB : foc - KEY_SLOTS;
            else if (k == LK_DOWN) foc = a == KA_COUNT - 1 ? KF_RESET : foc + KEY_SLOTS;
            else if (k == LK_LEFT && s > 0) foc--;
            else if (k == LK_RIGHT && s < KEY_SLOTS - 1) foc++;
            else if (k == LK_ENTER || k == LK_SPACE) { cap_a = a; cap_s = s; }
            else if (k == LK_CLEAR) { sho_ctl.key[a][s] = -1; ctl_save(); }
        } else if (foc == KF_TAB) { if (k == LK_DOWN) foc = KF_KEY; else if (k == LK_UP) foc = KF_BACK; }
        else {
            if (k == LK_UP) foc = KF_KEY + (KA_COUNT - 1) * KEY_SLOTS; else if (k == LK_DOWN) foc = KF_TAB;
            else if (k == LK_LEFT) foc = KF_RESET; else if (k == LK_RIGHT) foc = KF_BACK;
            else if (k == LK_ENTER || k == LK_SPACE) { if (foc == KF_RESET) { sho_controls_key_reset(); ctl_save(); } else controls_close(); }
        }
    }
}
static void controls_key(int k) {
    if (k == LK_PAD_UP || k == LK_PAD_DOWN || k == LK_PAD_LEFT || k == LK_PAD_RIGHT || k == LK_PAD_A || k == LK_PAD_START) return;   /* the pad is read in controls_tick here */
    controls_nav(k, 0);
}

static int in_box(int mx, int my, int x, int y, int w, int h) { return mx >= x && mx < x + w && my >= y && my < y + h; }
static void controls_click(int mx, int my) {
    if (wiz >= 0) return;
    if (cap_a >= 0) cap_a = -1;                       /* a click elsewhere gives up waiting for a key (and may start another) */
    if (pop) {
        int h = pop_n * 12 + 6;
        if (in_box(mx, my, pop_x, pop_y, pop_w, h)) { int i = (my - pop_y - 2) / 12; if (i >= 0 && i < pop_n) pop_choose(i); }
        else pop = 0;
        return;
    }
    {   /* tabs */
        int x = 32, w0 = text_w("CONTROLLER", 1) + 20, w1 = text_w("KEYBOARD", 1) + 20;
        if (in_box(mx, my, x, TAB_Y - 6, w0, 20)) { tab = 0; foc = CF_TAB; return; }
        if (in_box(mx, my, x + w0 + 4, TAB_Y - 6, w1, 20)) { tab = 1; foc = KF_TAB; return; }
    }
    if (tab == 0) {
        if (in_box(mx, my, DEV_X, DEV_Y, DEV_W, 16)) { foc = CF_DEV; open_device_list(); return; }
        for (int b = 0; b < PB_COUNT; b++) {          /* a button on the drawing (the small ones first: the d-pad sits by the stick) */
            const btn_geo *g = &geo[b];
            if (in_box(mx, my, g->cx - g->hw - 1, g->cy - g->hh - 1, g->hw * 2 + 3, g->hh * 2 + 3)) { open_action_list(b); return; }
        }
        for (int b = 0; b < PB_COUNT; b++) {          /* or its label */
            const btn_geo *g = &geo[b]; int y = ROW_Y(g->row);
            if (g->side == 0 ? in_box(mx, my, 60, y - 4, 160, 16) : in_box(mx, my, 420, y - 4, 160, 16)) { open_action_list(b); return; }
        }
        for (int i = 0; i < 3; i++) if (in_box(mx, my, bottom_x(bottom_c, 3, i), BTN_Y, text_w(bottom_c[i], 1) + 16, 16)) {
            foc = CF_WIZ + i;
            if (i == 0) wiz_start(); else if (i == 1) { sho_controls_pad_defaults(0); ctl_save(); } else controls_close();
            return;
        }
    } else {
        for (int a = 0; a < KA_COUNT; a++) for (int s = 0; s < KEY_SLOTS; s++)
            if (in_box(mx, my, KCOL_X(s), KROW_Y(a), KCOL_W, 16)) { foc = KF_KEY + a * KEY_SLOTS + s; cap_a = a; cap_s = s; return; }
        for (int i = 0; i < 2; i++) if (in_box(mx, my, bottom_x(bottom_k, 2, i), BTN_Y, text_w(bottom_k[i], 1) + 16, 16)) {
            foc = KF_RESET + i;
            if (i == 0) { sho_controls_key_reset(); ctl_save(); } else controls_close();
            return;
        }
    }
}

/* a key press while the keyboard tab waits for one: it becomes that action's key (and stops being any other's) */
void lc_raw_key(int code) {
    if (!lc_wants_key() || code < 0) return;
    for (int a = 0; a < KA_COUNT; a++) for (int s = 0; s < KEY_SLOTS; s++) if (sho_ctl.key[a][s] == code) sho_ctl.key[a][s] = -1;
    sho_ctl.key[cap_a][cap_s] = code; cap_a = -1;
    ctl_save();
    draw_controls();
}

/* every tick: the chosen controller's state for the drawing, and the wizard's next button */
/* The pad on this screen: the d-pad or left stick moves (held: repeats), A selects, B closes a list or gives up waiting
   for a key. Any controller does this, whichever is chosen; the diagram lights up as it is pressed all the same. */
static void controls_pad_nav(void) {
    sho_pad p; memset(&p, 0, sizeof p);
    if (!host || !host->pad_read || !host->pad_read(-1, &p)) { nav_prev = 0; return; }
    unsigned now = p.buttons & ((1u << PB_A) | (1u << PB_B) | (1u << PB_UP) | (1u << PB_DOWN) | (1u << PB_LEFT) | (1u << PB_RIGHT));
    if (p.ly > 0.6f) now |= 1u << PB_UP;
    if (p.ly < -0.6f) now |= 1u << PB_DOWN;
    if (p.lx < -0.6f) now |= 1u << PB_LEFT;
    if (p.lx > 0.6f) now |= 1u << PB_RIGHT;
    unsigned fresh = now & ~nav_prev, dirs = (1u << PB_UP) | (1u << PB_DOWN) | (1u << PB_LEFT) | (1u << PB_RIGHT);
    if (fresh & dirs) nav_next = ctl_now + 0.40;
    else if ((now & dirs) && ctl_now >= nav_next) { fresh |= now & dirs; nav_next = ctl_now + 0.12; }   /* held */
    nav_prev = now;
    if (wiz >= 0) return;                             /* configure with controller: presses are answers */
    if (cap_a >= 0) { if (fresh & (1u << PB_B)) cap_a = -1; return; }   /* waiting for a key */
    if (fresh & (1u << PB_UP)) controls_nav(LK_UP, 1);
    else if (fresh & (1u << PB_DOWN)) controls_nav(LK_DOWN, 1);
    else if (fresh & (1u << PB_LEFT)) controls_nav(LK_LEFT, 1);
    else if (fresh & (1u << PB_RIGHT)) controls_nav(LK_RIGHT, 1);
    else if (fresh & (1u << PB_A)) controls_nav(LK_ENTER, 1);
    else if ((fresh & (1u << PB_B)) && pop) pop = 0;
}
static void controls_tick(void) {
    int di = dev_index();
    controls_pad_nav();
    memset(&live, 0, sizeof live);
    live_ok = host && host->pad_read && di != -2 && host->pad_read(di, &live);
    if (wiz >= 0 && live_ok) {
        unsigned now = live.buttons;
        if (!wiz_armed) { if (!now) wiz_armed = 1; }     /* wait for everything to be let go first */
        else {
            unsigned fresh = now & ~wiz_prev;
            if (fresh) { for (int b = 0; b < PB_COUNT; b++) if ((fresh >> b) & 1) { sho_ctl.pad[b] = (unsigned char)wiz; break; } wiz_next(); }
        }
        wiz_prev = now;
    }
}
