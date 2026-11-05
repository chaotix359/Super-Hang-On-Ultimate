/* Super Hang-On Ultimate launcher for macOS (the Mac twin of win32_launcher.c): DIP switch B
 * settings, display mode, ROM checks and PLAY on the right. Returns to main() with the chosen settings. */
#import <Cocoa/Cocoa.h>
#include <stdio.h>
#include <string.h>
#include "mac_launcher.h"

#define ART_W 0     /* no artwork: the window is just the settings panel */
#define ART_H 0
#define PANEL_W 420
#define CLIENT_H 586

static NSColor *rgb(int r, int g, int b) { return [NSColor colorWithSRGBRed:r / 255.0 green:g / 255.0 blue:b / 255.0 alpha:1.0]; }
#define PANEL  rgb(13, 33, 92)
#define YELLOW rgb(255, 214, 0)
#define LIGHT  rgb(170, 190, 230)
#define GOOD   rgb(70, 230, 120)
#define BAD    rgb(255, 90, 90)

/* ------------------------------------------------------------------ views */
@interface LBackground : NSView
@end
@implementation LBackground
- (BOOL)isFlipped { return YES; }          /* top-left origin, like the Windows layout */
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    NSRect b = self.bounds;
    [PANEL setFill]; NSRectFill(NSMakeRect(ART_W, 0, b.size.width - ART_W, b.size.height));
    [YELLOW setFill]; NSRectFill(NSMakeRect(ART_W, 0, 5, b.size.height));   /* accent line down the left edge */
}
@end

@interface LPlayButton : NSButton
@end
@implementation LPlayButton
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    BOOL en = self.isEnabled, down = self.isHighlighted;
    NSColor *fill = !en ? rgb(90, 90, 110) : down ? rgb(170, 20, 25) : rgb(220, 35, 40);
    NSBezierPath *p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(self.bounds, 2.5, 2.5) xRadius:7 yRadius:7];
    [fill setFill]; [p fill];
    p.lineWidth = 3; [(en ? YELLOW : rgb(140, 140, 160)) setStroke]; [p stroke];
    NSFont *f = [NSFont fontWithName:@"Helvetica-BoldOblique" size:28];
    if (!f) f = [NSFont boldSystemFontOfSize:28];
    NSDictionary *a = @{ NSFontAttributeName: f, NSForegroundColorAttributeName: en ? NSColor.whiteColor : rgb(190, 190, 200) };
    NSSize ts = [@"PLAY" sizeWithAttributes:a];
    [@"PLAY" drawAtPoint:NSMakePoint(NSMidX(self.bounds) - ts.width / 2, NSMidY(self.bounds) - ts.height / 2 + (down ? 1 : 0)) withAttributes:a];
}
@end

/* ------------------------------------------------------------------ state */
enum { NOPT = 5 };
static launcher_settings *S;
static launcher_rom_check_fn check_rom;
static int result, rom_ok, md_ok, last_nonmd, finished;
static NSWindow *win;
static NSPopUpButton *pop[NOPT];
static NSButton *chk[2], *play;
static NSTextField *t_rom, *t_romd, *t_md, *t_mdd, *t_chkdesc;
static NSFont *f_text, *f_small, *f_head;
static id observer;

static NSString *ns(const char *s) { NSString *r = s ? [NSString stringWithUTF8String:s] : nil; return r ? r : @""; }

static NSTextField *label(NSView *v, NSString *t, NSRect fr, NSFont *f, NSColor *c) {
    NSTextField *l = [NSTextField labelWithString:t];
    l.frame = fr; l.font = f; l.textColor = c;
    l.lineBreakMode = NSLineBreakByTruncatingTail;
    l.drawsBackground = NO; l.bezeled = NO; l.editable = NO; l.selectable = NO;
    [v addSubview:l];
    return l;
}
static void set_check_title(NSButton *b, NSString *t, NSColor *c) {
    b.attributedTitle = [[NSAttributedString alloc] initWithString:t attributes:@{ NSFontAttributeName: f_text, NSForegroundColorAttributeName: c }];
}

/* the Mega Drive Music entry of the Music list needs the Mega Drive ROM and the Mega Drive Enhancements tick */
static int music_md_ok(void) { return md_ok && chk[0].state == NSControlStateValueOn; }
static void update_music_item(void) {
    NSMenuItem *it = [pop[3] itemAtIndex:2];
    it.enabled = music_md_ok() ? YES : NO;
    if (!music_md_ok() && pop[3].indexOfSelectedItem == 2) [pop[3] selectItemAtIndex:last_nonmd];
}

static void refresh_rom(void) {
    char msg[256] = "", detail[256] = "";
    rom_ok = check_rom(msg, sizeof msg, detail, sizeof detail);
    t_rom.stringValue = ns(msg);    t_rom.textColor = rom_ok ? GOOD : BAD;
    t_romd.stringValue = ns(detail); t_romd.textColor = rom_ok ? rgb(150, 210, 170) : rgb(255, 170, 170);
    play.enabled = rom_ok ? YES : NO; [play setNeedsDisplay:YES];
    if (S->md_check) {   /* Mega Drive ROM: without it the Mega Drive options are greyed out */
        char m2[256] = "", d2[256] = "";
        md_ok = S->md_check(m2, sizeof m2, d2, sizeof d2);
        t_md.stringValue = ns(m2);  t_md.textColor = md_ok ? GOOD : rgb(175, 180, 200);
        t_mdd.stringValue = ns(d2); t_mdd.textColor = md_ok ? rgb(150, 210, 170) : rgb(140, 148, 175);
    }
    static NSString *const cl[2] = { @"Mega Drive Enhancements", @"Apply Mega Drive Controls" };
    for (int i = 0; i < 2; i++) { chk[i].enabled = md_ok ? YES : NO; set_check_title(chk[i], cl[i], md_ok ? YELLOW : rgb(105, 115, 150)); }
    t_chkdesc.textColor = md_ok ? LIGHT : rgb(95, 105, 140);
    update_music_item();
}

static void read_choices(void) {
    S->demo_sounds = pop[0].indexOfSelectedItem == 0;
    S->difficulty = (int)pop[1].indexOfSelectedItem & 3;
    S->time_adj = (int)pop[2].indexOfSelectedItem & 3;
    { int ms = (int)pop[3].indexOfSelectedItem; S->arrange_music = ms == 1; S->md_music = ms == 2 && music_md_ok(); }
    S->view = (int)pop[4].indexOfSelectedItem;
    if (md_ok) {   /* greyed out: keep the saved choices for when the ROM is back */
        S->md_enhancements = chk[0].state == NSControlStateValueOn;
        S->md_controls = chk[1].state == NSControlStateValueOn;
    }
}

static void finish(int r) {
    if (finished) return;
    finished = 1; result = r;
    if (r) read_choices();
    [NSApp stopModal];
    [win orderOut:nil];
}
void launcher_cancel(void) { if (win && !finished) finish(0); }   /* Cmd+Q while the launcher is up */

/* ------------------------------------------------------------------ controller */
@interface LCtl : NSObject <NSWindowDelegate>
@end
@implementation LCtl
- (void)play:(id)sender { (void)sender; if (rom_ok) finish(1); }
- (void)quit:(id)sender { (void)sender; finish(0); }
- (void)checkChanged:(id)sender { (void)sender; update_music_item(); }
- (void)musicChanged:(id)sender {
    (void)sender;
    int sel = (int)pop[3].indexOfSelectedItem;
    if (sel == 2 && !music_md_ok()) [pop[3] selectItemAtIndex:last_nonmd]; else if (sel != 2) last_nonmd = sel;
}
- (void)openFolder:(id)sender {
    (void)sender;
    if (S->game_dir) [[NSWorkspace sharedWorkspace] openURL:[NSURL fileURLWithPath:ns(S->game_dir) isDirectory:YES]];
}
- (BOOL)windowShouldClose:(NSWindow *)w { (void)w; finish(0); return NO; }
@end

static NSPopUpButton *popup(NSView *v, NSRect fr, NSArray<NSString *> *items, int sel, id target, SEL act) {
    NSPopUpButton *p = [[NSPopUpButton alloc] initWithFrame:fr pullsDown:NO];
    [p addItemsWithTitles:items];
    if (sel >= 0 && sel < (int)items.count) [p selectItemAtIndex:sel];
    p.font = f_text; p.target = target; p.action = act;
    [v addSubview:p];
    return p;
}

int launcher_run(launcher_settings *s, launcher_rom_check_fn check) {
    S = s; check_rom = check; result = 0; finished = 0;
    f_text = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
    f_small = [NSFont systemFontOfSize:11];
    f_head = [NSFont systemFontOfSize:21 weight:NSFontWeightBold];
    LCtl *ctl = [LCtl new];

    win = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, ART_W + PANEL_W, CLIENT_H)
                                      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
                                        backing:NSBackingStoreBuffered defer:NO];
    win.title = @"Super Hang-On Ultimate";
    win.releasedWhenClosed = NO;
    win.delegate = ctl;
    if (@available(macOS 10.14, *)) win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    LBackground *v = [[LBackground alloc] initWithFrame:NSMakeRect(0, 0, ART_W + PANEL_W, CLIENT_H)];
    win.contentView = v;

    const int x0 = ART_W + 28, w = PANEL_W - 56;
    int y = 18;
    label(v, @"Super Hang-On Ultimate", NSMakeRect(x0, y, w, 30), f_head, YELLOW); y += 30;
    label(v, @"Native Port + Mega Drive Original Mode Mod", NSMakeRect(x0, y, w - 110, 18), f_small, LIGHT);
    if (s->game_dir) {   /* where the ROMs, music and settings live */
        NSButton *b = [NSButton buttonWithTitle:@"Open game folder" target:ctl action:@selector(openFolder:)];
        b.bordered = NO;
        b.attributedTitle = [[NSAttributedString alloc] initWithString:@"Open game folder"
            attributes:@{ NSFontAttributeName: f_small, NSForegroundColorAttributeName: GOOD, NSUnderlineStyleAttributeName: @(NSUnderlineStyleSingle) }];
        b.frame = NSMakeRect(x0 + w - 105, y - 1, 105, 18);
        b.alignment = NSTextAlignmentRight;
        b.toolTip = ns(s->game_dir);
        [v addSubview:b];
    }
    y += 28;

    NSArray *onoff = @[ @"On", @"Off" ], *levels = @[ @"Easy", @"Normal", @"Hard", @"Hardest" ];
    NSArray *display = @[ @"Fit screen", @"16:9 widescreen", @"4:3 original" ];
    NSArray *music = @[ @"Regular Soundtrack", @"Arrange Soundtrack", @"Mega Drive Music" ];
    NSString *labels[NOPT] = { @"Demo sounds", @"Difficulty", @"Time limit", @"Music", @"Display" };
    NSString *descs[NOPT] = { @"Sound during the attract mode", @"How often rival bikes appear", @"Time allowed to reach each checkpoint",
                              s->music_note[0] ? ns(s->music_note) : @"The four music-select songs",
                              @"⌘1 / ⌘2 / ⌘3 switch during play" };
    NSArray *items[NOPT] = { onoff, levels, levels, music, display };
    last_nonmd = s->arrange_music ? 1 : 0;
    int sel[NOPT] = { s->demo_sounds ? 0 : 1, s->difficulty & 3, s->time_adj & 3, s->md_music && s->md_enhancements ? 2 : last_nonmd,
                      s->view >= 0 && s->view <= 2 ? s->view : 0 };
    for (int i = 0; i < NOPT; i++) {
        label(v, labels[i], NSMakeRect(x0, y + 1, 130, 20), f_text, NSColor.whiteColor);
        pop[i] = popup(v, NSMakeRect(x0 + 130, y - 4, w - 130, 26), items[i], sel[i], ctl, i == 3 ? @selector(musicChanged:) : (SEL)0);
        label(v, descs[i], NSMakeRect(x0, y + 23, w, 16), f_small, LIGHT);
        y += 47;
    }
    pop[3].autoenablesItems = NO;   /* the Mega Drive Music entry is enabled by hand */
    y += 6;
    {   /* Mega Drive options (no description under Enhancements: the ROM status covers it) */
        int on[2] = { s->md_enhancements, s->md_controls };
        for (int i = 0; i < 2; i++) {
            chk[i] = [NSButton checkboxWithTitle:@"" target:ctl action:@selector(checkChanged:)];
            chk[i].frame = NSMakeRect(x0, y, w, 22);
            chk[i].state = on[i] ? NSControlStateValueOn : NSControlStateValueOff;
            [v addSubview:chk[i]];
            if (i == 1) t_chkdesc = label(v, @"Mega Drive steering feel, for arcade or Original Mode", NSMakeRect(x0 + 22, y + 22, w - 22, 16), f_small, LIGHT);
            y += i == 0 ? 32 : 44;
        }
    }
    /* bottom: ROM status lines above PLAY */
    const int ybottom = CLIENT_H, yplay = ybottom - 22 - 60, yb = yplay - 12;
    t_mdd  = label(v, @"", NSMakeRect(x0, yb - 18, w, 18), f_small, LIGHT);
    t_md   = label(v, @"", NSMakeRect(x0, yb - 18 - 22, w, 22), f_text, LIGHT);
    t_romd = label(v, @"", NSMakeRect(x0, yb - 18 - 22 - 6 - 18, w, 18), f_small, LIGHT);
    t_rom  = label(v, @"", NSMakeRect(x0, yb - 18 - 22 - 6 - 18 - 22, w, 22), f_text, LIGHT);
    if (!s->md_check) { t_md.hidden = YES; t_mdd.hidden = YES; }

    play = [[LPlayButton alloc] initWithFrame:NSMakeRect(x0, yplay, w, 60)];
    play.title = @"PLAY"; play.bordered = NO; play.target = ctl; play.action = @selector(play:);
    [play setButtonType:NSButtonTypeMomentaryPushIn];
    play.keyEquivalent = @"\r";                      /* Return plays */
    [v addSubview:play];
    {   /* Esc closes the launcher (quits) */
        NSButton *esc = [NSButton buttonWithTitle:@"" target:ctl action:@selector(quit:)];
        esc.keyEquivalent = @"\033"; esc.frame = NSMakeRect(-100, -100, 1, 1); esc.bordered = NO;
        [v addSubview:esc];
    }

    refresh_rom();
    observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidBecomeKeyNotification object:win queue:nil
                                                             usingBlock:^(NSNotification *n) { (void)n; if (!finished) refresh_rom(); }];   /* re-check after the user adds the ROM */
    [win center];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp runModalForWindow:win];

    [[NSNotificationCenter defaultCenter] removeObserver:observer];
    observer = nil;
    [win orderOut:nil];
    win.delegate = nil;
    win = nil;
    (void)ctl;
    return result;
}
