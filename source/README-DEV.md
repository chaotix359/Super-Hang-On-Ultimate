# Super Hang-On Ultimate – developer notes (v0.3)

## Layout
| Path | What |
|---|---|
| `src/sho_core.c` | Out Run board: 68000 x2 scheduling, Z80, memory maps, 315-5195 mapper, IRQs, YM2151, SegaPCM, ROM loading |
| `src/sho_video.c` | tilemaps, text, road, System 16B sprites, priorities; widescreen margin support |
| `src/rc_runtime.[ch]` | runtime for recompiled code: fibers, dispatcher, cycle-exact yields, interrupts |
| `src/gen/main_rc.c`, `sub_rc.c` | **generated** C for the two 68000 programs (do not edit) |
| `recomp/overrides/*.c` | **hand-written** C that replaces generated functions |
| `src/win32_*.c` | Windows frontend (Direct3D 11 / GDI, XInput, waveOut) |
| `src/test_main.c` | headless test driver (screenshots, WAV, state hashes) |

## How the C version works
* `recomp/recomp.py` turns each 68000 subroutine into a C function. Branches are `goto`s, calls are C calls
  that check the 68000 return address/stack (stack tricks fall back to a dispatcher that can enter any instruction).
* Instruction semantics come from Musashi's per-opcode handlers, called with the opcode fixed at compile time,
  and cycles are charged exactly as the interpreter does. Each CPU runs on its own fiber and yields when its
  cycle budget is spent, so interleaving with the other CPU, the Z80 and interrupts is identical.
* Result: the recompiled machine is bit-identical to the interpreter (checked by hashing all RAM + registers
  every frame). Code the recompiler never saw falls back to the interpreter automatically.

## Regenerating the C (needs your ROM)
```
python3 ../tools/build_images.py rom build                      # main.bin / sub.bin etc. (from the investigation kit)
cc -O2 -w -Isrc/third_party/musashi -o recomp/dasm_all recomp/dasm_all.c src/third_party/musashi/{m68kcpu,m68kops,m68kdasm}.c src/third_party/musashi/softfloat/softfloat.c -lm
recomp/dasm_all build/main.bin 40000 > recomp/main.dasm; recomp/dasm_all build/sub.bin 40000 > recomp/sub.dasm
# coverage (optional but recommended): see recomp/cov_main.c header; merge runs with merge_cov.py
python3 recomp/recomp.py main build/main.bin recomp/main.dasm cov/merged.bin src/gen/main_rc.c recomp/overrides
python3 recomp/recomp.py sub  build/sub.bin  recomp/sub.dasm  cov/merged.bin src/gen/sub_rc.c  recomp/overrides
```

## Changing game logic (overrides)
```
python3 recomp/extract_override.py src/gen/main_rc.c M_002A0C    # copies the function to recomp/overrides/
# edit it, then re-run recomp.py so the generated version is skipped
```
Keep changes behind a switch (e.g. `sho_wide_xo`) so the "off" setting stays bit-exact, and use the native
instruction helpers in `rc_overrides.h` when you change an operand.

`M_002A0C` (object -> sprite projection) is the first override: its two horizontal cull limits widen by the
16:9 margin. The entry writer already stores 12 bits of sprite X; the widescreen renderer uses them.

## Verifying
```
./build_linux_test.sh
HASHLOG=interp.txt  ./shotest shangon.zip 20000 auto            # interpreter
RECOMP=1 HASHLOG=rc.txt ./shotest shangon.zip 20000 auto        # recompiled C
cmp interp.txt rc.txt                                            # must be identical (4:3 mode)
RECOMP=1 WIDE=1 SHOTS=300 CLASS=2 ./shotest shangon.zip 3600 auto   # 16:9 screenshots, America course
```
`auto` = scripted coin/start and an autopilot that follows the road; `CLASS=0..3` picks the course.

## Widescreen notes
* 428x224 = 16:9 at the arcade monitor's pixel shape (54 extra pixels each side).
* Race backdrops are two-page panoramas; the game limits their scroll so the page join at virtual x=512 stays
  just off the 4:3 edge. In the right-hand margin the renderer continues the left page's last tile column.
* The HUD stays in the centre 4:3 area (next candidate for an override).

## v0.3 additions
* `M_008F90` override (initials entry input): after the original pedal read, gamepad d-pad requests return
  D0 = +1/-1 and A sets the "enter" flag at ($6,A2). The core queues the requests (`sho_entry_step/select`)
  and knows the screen is active because the hook runs. No gamepad input = original code path.
* HUD spreading and curtain extension are renderer-only (`draw_text` in `sho_video.c`), active in 16:9
  when the race HUD's TOP/TIME/SCORE boxes are on text row 1.

## v0.4 additions
* `M_004944` override (class/music select input): both select loops add the step word at $261016 to their
  index and wrap with AND 3; gamepad navigation supplies +1/-1. While a select screen is active (hook seen),
  the core maps A to the Start bit of the input port.
* `win32_launcher.c`: settings window shown before the game (no artwork; just the settings panel). Settings are written to shangon.ini.

## v0.5 additions
* ts743's arranged-soundtrack support (`sho_music.c`, dr_mp3): MP3s decoded at start; song start commands are
  detected at the Z80's command read (PC 0x006E) and the replaced song headers in the in-memory sound ROM are
  blanked so the original driver stays silent while SFX continue. `external_music=0` = original behaviour
  (bit-exact). `build_win.ps1` builds natively on Windows with LLVM-MinGW.
* Launcher "Music" option writes `[audio] external_music`.

## v0.6 additions (prototype): Mega Drive Enhancements / Mega Drive Controls
* `sho_md.c` / `sho_md.h`: everything is gated on `md_enabled`; with both launcher options off, no hook runs
  and the core is unchanged (lockstep interpreter vs recompiled: identical over 20,000 frames).
* Arcade RAM used: sub $260000 game state (0 attract/menus, 1 countdown, 2 racing, 3 goal), $260012 class,
  $260078 speed (16.16, integrated by the main CPU at 0x6382; 279 = 324 km/h on the meter), $260094 crash,
  work $060408 credit game flag, $060438 stage, $060500 BCD time, $066400-$0665BF high-score tables.
* Original Mode drives the real machine: menus are drawn over the dimmed arcade picture (`md_compose`), a race
  is launched by injecting coin/Start and stepping the class select via `sho_menu_step`; the time limit is held
  in `md_pre_frame`; part effects are applied in `md_post_frame` to the speed delta (engine/muffler/oil scale
  acceleration, engine caps top speed, brakes scale braking deceleration). Tyre grip scales the outward push on
  curves in the new `M_0065EC` override (`md_grip_q8`, 256 = original). After the goal the machine is
  fast-forwarded hidden (5 extra frames per frame) through its goal/name-entry sequence and the high-score
  tables are restored.
* Mega Drive handlebar: rates from the Mega Drive ROM's frame table ($A038): centre-return / steer-in /
  counter-steer per frame grade, plus speed/128. Grade 6 = Mega Drive Arcade Mode (used by "Apply Mega Drive
  Controls" in arcade play); Original Mode uses the owned frame (scaled by its condition).
* Parts names/prices: Mega Drive parts shop (prices also in its ROM table at $15662). Rival roster after
  Mia Ferraru, rival paces (x arcade time allowance from the $8888 table), prizes and effect strengths are
  prototype values.

## v0.7 additions: Mega Drive screens
* `sho_mdgfx.c`: a minimal Mega Drive (68000 + VDP rendered line by line, 3-button pad, no Z80/sound) runs the
  player's Mega Drive ROM (World Rev A, CRC32 3877D107) headless at start-up, scripted into the title, the
  Original Mode command screen and the parts screen (cursor on each of the six categories). Capture frames are
  rendered without the window plane (which holds all of that game's text), giving clean backdrops; the font
  (tiles $614-$637 plus symbols) and the four text palettes are taken from VRAM/CRAM. Nothing is saved to disk.
* The Mega Drive run borrows the shared Musashi core: while `sho_alt_bus` is set, the core's 68000 memory
  callbacks forward to the Mega Drive bus. It runs before `sho_reset()`; with it NULL the arcade path is
  unchanged (lockstep hashes identical to v0.6.2).
* `sho_md.c` draws the Mega Drive versions of the Original Mode screens when captures exist (`md_draw_*`),
  otherwise the arcade-style ones.

## v0.8 additions: Mega Drive course conversion (stage 1)
* The Mega Drive game is a port of the arcade program and keeps its course formats. `sho_mdcourse.c`:
  road path = Mega Drive table $FAAA (one heading vector per 2 arcade steps) doubled into the arcade's per-step
  format (sub CPU table $19E8); curve stream (6-byte records) copied as is; section events converted from 4-byte
  to 8-byte records. Original Mode races: $8E58 (class, stage, extra) and laps at $FA92; stage ids via $FBC2,
  descriptors at $FB62 (+8 curves, +$C events, +$10 objects).
* Installed into the unused $40000-$5FFFF space of the main/sub program ROM images, with every entry of the
  arcade's descriptor table ($8AEC) and path table (sub $19E8) pointed at it; restored once the arcade's game has
  ended. Races launch on the Senior class (14 stages) and finish after the Mega Drive lap count (stage counter
  $060438). Class select cursor is $260012; with a credit the arcade enters the class select by itself.

## v0.8.1 additions: Mega Drive course look
* `sho_mdgfx.c` `capture_scene()`: for each Original Mode race (index poked into Mega Drive RAM $B606, copied to
  $C85A when the race is set up) the Mega Drive game is run to its starting grid; palette lines 2/3 entries B-F
  give the light/dark band colours of kerb, ground, road, edge line and centre line; CRAM $30 the sky; plane B
  above the horizon (per-line scroll recorded while rendering) is unrolled to a full-width background strip.
* `sho_video.c`: with `sho_md_scene` set, the arcade road palette pairs ($7F0-$7FF road parts, $7C0-$7CF ground,
  $7D0-$7EF sky) take the Mega Drive colours, the two tile layers are skipped, and the sky above the road
  generator's first road line shows the strip, shifted by the arcade background layer's scroll (scaled to the
  strip width). Off (NULL) = unchanged rendering.

## v0.8.3 additions: Mega Drive roadside objects
* Mega Drive object streams are 6-byte (pos, count, pattern); patterns at $5771C: spacing mask + 6-byte entries
  (end flag, kind, x, attribute). Converted to arcade 4-byte streams and arcade patterns (pattern pointer table at
  main $13F04 patched during the race) using one carrier object definition ($4754) for placement/size/collision,
  with the Mega Drive kind in the entry byte the arcade skips. The `M_006F16` override (object spawn) records
  kind per sprite palette (palette $34 + slot register); `sho_video.c` measures each carrier sprite and draws the
  Mega Drive picture in its place (bottom-centred, scaled by the carrier's height and the picture's height at the
  reference distance) into an RGB layer.
* Pictures: `capture_objects()` drives each course with the throttle held and, when an object (slots at
  $FFCE00, kind +$2D, distance +$0C, screen anchor +$10/+$14) is at distance $50-$5F, reads the hardware sprite
  table and draws the run of pieces that sits on the anchor (neighbouring links/tiles; the player's bike is the
  first 5 entries). Other distances are kept as a fallback, scaled linearly.

## v0.8.4: object pictures from the Mega Drive frame definitions
* `capture_objects()` no longer guesses pieces from the sprite table. The Mega Drive's kind table ($204, 20 bytes
  per kind = 10 frame pointers, smallest to largest; $6EC8[distance] picks the frame) points at frame definitions:
  a count word (n-1), then per piece a y byte (loaded into a word preset to -1, so y = byte - 256), a size byte (as
  the sprite table), a tile/attribute word added to the object's base word (slot +2 = pattern attribute) and an x
  word. Pictures are drawn straight from those pieces and video memory.
* Tiles are only trusted while the Mega Drive is drawing that frame: at least half of its pieces must be in the
  sprite table (same tile word, within 12 pixels) and the frame must be held 2 frames. The largest frame seen is
  kept; `norm_h` is the height of the frame shown at the reference distance (frame $6EC8[$58]/2), from ROM.
* Billboards of different kinds share a tile area (same base word); only the course's first is loaded at the
  start, the rest are reloaded later in a way the capture drive doesn't reach (jumps). Later ones are taken from a
  race where they come first (`fill_objects()`), e.g. Bird Bird for course 1 from course 7. Kinds first placed
  at $700+ (the finish area) are left out.
* The drive holds the throttle and every 300 frames moves the course position ($FF0616) to just before the
  nearest placement of a kind not yet seen at frame 7+ (never backwards).
* HUD: in 16:9 the HUD columns 0-12 move to the left edge and 22+ to the right; the bar is the Mega Drive's two
  rails with the yellow band between them from the S cap, and green lap ticks.

## v0.8.5
* Pause (Mega Drive enhancements on, during a race): `sho_pause_frame(paused, tick)` keeps the last frame and draws
  the Mega Drive's "P A U S E" over it (`md_draw_pause`: its yellow font, letters 16 px apart at (120,112) of the
  320-wide picture, blinking 48 frames on / 48 off - measured on the Mega Drive). Start toggles it when
  `sho_md_pause_allowed()`; P still pauses anywhere.
* Launcher artwork 414x586 (fills the window height).

## v0.8.6
* Icon: res/shangon.ico (16-256 px) from the user's photo. Launcher: `launcher_settings.md_check` (win32_main
  `md_rom_check`) fills a status + detail pair under the arcade ROM lines; without the ROM the check boxes are
  disabled and drawn grey, their saved values untouched. `g_md_present` gates `sho_md_configure` so ini settings
  never apply without the ROM.

## v0.8.7
* Pause menu: `sho_pause_begin/update/end` (core) keep the frame and call `md_pause_menu` (sho_md.c): CONTINUE /
  SOUND (MUSIC, EFFECTS 0-10, BACK) / EXIT, Mega Drive font, darkened band behind it. EXIT = `reset_to_title()`
  (same as F3; `sho_md_configure` now also uninstalls a converted course).
* Sound levels in `sho_audio`: FM gain x music level (x effects level while an arranged track plays, since the FM
  chip then only carries effects), PCM gain x effects level; the arranged soundtrack's volume = ini music_volume x
  music level / 10.

## v0.8.8
* Pause menu: shading removed; block centred on line 112 (PAUSE y 80, options 104/120/136), every line centred on x 160 to the pixel.

## v0.8.9: Mega Drive steering model
* Mega Drive $9842-$990E: virtual pot $C73C ($80 centre, $3A-$C6), rates from $A038 (8-byte rows: return,
  steer-in, counter-steer) by frame part $B618 (row 6 when not in Original Mode), +speed>>7 from row 2 up;
  released it steps toward $80 without clamping (settles in the dead zone $6C-$94); output (v-$80)*256/$8C.
  Measured in the embedded Mega Drive: this runs once every 3 frames. `md_steer_adc` does the same and feeds the
  arcade ADC scaled by $90/$8C (the arcade's divisor), forced to $80 inside the Mega Drive dead zone.

## v0.9: live Mega Drive Original Mode (stage 3)
* `sho_mdgfx.c` live API (`mdlive_*`): the embedded Mega Drive keeps its own Musashi context (`live_ctx`) and is
  switched in for each frame (the arcade core sets its CPU contexts before every slice). `mdlive_boot` powers on and
  presses through the logos/title to a new game's command screen; `mdlive_save/load` write/read memory, VDP state
  and the 68000 registers at a frame boundary.
* Mega Drive state machine: word $FFC704, handler table at $6CF2 (index & $7FFC; bit 0 = initialised): $24/$25
  command screen, $0C/$0D music select, $10/$11 race set-up, $18 racing, $1C race over ($B8A2), $20.. title.
  Race index $FFC85A, music $FF0516, parts $FFB618.. (frame, engine, brake, muffler, oil, tire), condition
  $FFB648.., rival time $FFC83A (BCD m'ss"hh, total).
* `sho_md.c` PH_MDLIVE: the pad goes to the Mega Drive (A->C select, B->A, turbo->B, Start); on state $11 the arcade
  race is launched (`begin_race_r` with the Mega Drive race, its music chosen on the arcade music select, parts to
  the physics). `finish_race` then runs the Mega Drive on headless to state $18, writes the time to $C832
  ($FFFFFFFF = retired), $FF06C0 = 1, $C70E = 1 and state $1C: `$B8B0` (Original Mode, $C82C set) copies the times
  and goes to the results ($24) - prize, win/loss, rival and wear are the Mega Drive's. Back on the command screen
  ($25) the session is saved. Leaving it (END -> state < 8) asks the host to reset to the title screen
  (`sho_md_reset_requested`).

## v0.9.1
* Hidden launch (`launch_hidden`): black, muted and 3 extra frames per frame while PH_LAUNCH or the arcade's class/
  music select is up; revealed at the start line.
* Goal (PH_FINISH): `finish_begin` renders the frame twice (`sho_video_hide_player` skips sprites with bottom >= 190
  near the centre = the bike) - the difference is the bike; `md_frozen()` stops the arcade frames
  (`sho_run_frame`), `finish_draw` draws the still road, the bike shrinking towards (centre, 116), the HUD
  without SPEED, then a fade. 100 + 40 + 20 frames, then `finish_race`.
* Damage (Mega Drive $AC3A, $A0BA, $A5D8, $A6D2, $A7EE): see `md_crash_damage`/`md_damage_frame`; conditions are
  written back to $FFB648.. before the result is handed over.
* Walls: `reset_request` 2 on entering Original Mode (host: `sho_md_before_reset` + machine reset, phase kept), 1 on
  leaving (reset to title); `sho_md_before_reset` restores the high-score block and the arcade courses and clears
  the Mega Drive scene. With a Mega Drive scene the whole arcade text layer is skipped.

## v0.9.2 - Mega Drive music
- `src/sho_mdsnd.c/.h`: the Mega Drive's sound driver run natively. The Z80 program is the 0x1A24 bytes at ROM 0x7D7F4 (the 68000 copies it to Z80 RAM). Z80 core (superzazu) + Nuked-OPN2 (`third_party/nukedopn2`, YM2612 mode) + a small SN76489.
  The Z80 and the YM are interleaved clock by clock (2.8 Z80 cycles per YM clock), otherwise back-to-back register writes are lost. The chips are run from the audio mixer (`mdsnd_add`), 48 kHz, linear resampling from 53267 Hz, one-pole DC removal.
  The driver clears its mailbox ($1C00..) while starting; `mdsnd_command` therefore holds a command for ~0.28 s after boot.
- Mailbox: the 68000 writes a command byte to Z80 RAM $1C03. Songs from the music-select table at $7E2A: $82 Sprinter, $84 Winning Run, $83 Hard Road ($85 the fourth); $81 title/menus; $B0 stops/fades; $B1 at boot.
- Live Original Mode: the live Mega Drive's writes to $A00000-$A0FFFF and the Z80 reset line ($A11200) are forwarded (`snd_live`), so its menus and mechanic play their own music. Saves remember the last song in the unused top byte of the saved Z80 RAM (no format change).
- `sho_md_audio_mode()`: 2 = live Mega Drive screens, 1 = "Mega Drive Music" option (arcade sound commands for the four songs are mapped by `sho_md_music_command`, and the arcade's own song headers are silenced with `sho_silence_music`), 0 = arcade audio. Arcade/Arrange playback is untouched with the option off (lockstep and out.wav identical).
- Bump damage: the Mega Drive routine at $D53A (rival contact): muffler -1, frame -1 (Original Mode, not while retiring). The arcade has no bump flag; `md_bump_check` treats a speed loss of 8 or more in a frame without a fall or braking as a bump (1 s cooldown).
- Launcher: third Mega Drive tick box "Mega Drive Music" (ini `[megadrive] music=`); the window is 630 px tall.

## v0.9.3
- Sound: Z80 must run 2.8 cycles per YM clock (3.579545 MHz / 1.278409 MHz); the first version had it inverted (Z80 at ~14% speed -> slow tempo, missing notes).
- Arcade chosen on the mode screen: `arc_t` state machine in PH_ARCADE - coin pulse, then a Start press if the class select has not appeared.
- Goal: bike scale = 1/(1+0.45 f) (constant world speed), wheels on the line from the bike's start to the road horizon (`sho_video_horizon`), FIN_RIDE 50 frames.
- test hook: `FINISHTEST=<frame>` (harness) starts the goal sequence and writes fin_NN.ppm frames.

## v0.9.4
- Goal: `finish_road_profile` measures the road per scanline from the road colours of the scene (edges), fits half-width = a*(y - yv) (exact vanishing row) and keeps a smoothed centre line; the bike keeps its relative position across the road, size = road width ratio, y from the fit.
- Launcher: Music list is owner-drawn; item 3 "Mega Drive Music" greyed unless the ROM is found and Mega Drive Enhancements is ticked (ini `[megadrive] music=` only counts with enhancements=1).
- Sound lifecycle: `mdsnd_resync()` (chip + Z80 restart on the current screen's music) after the live Mega Drive's fast-forward (race result); `md_sound_release()` stops the music or shuts the driver down when Original Mode is left.
- test hooks: `FINDUMP=1` writes fin_NN.ppm during a real goal sequence.

## v0.9.5
- Progress bar: lap = 2308 road steps, course position ($260030) advances 2 per step -> MD_LAP_POS 4616 (measured by jumping the position in converted courses; the old divisor 2000 was wrong).
- sho_mdsnd: mailbox ($1C03) writes and mdsnd_command go through a 16-entry queue, delivered when warm-up is over and the driver has cleared the mailbox.
- test hooks: POSDBG, STDBG, MDINST=<race> (install a converted course in the arcade flow), DESCDUMP.

## v0.9.6
- sho_mdsnd command queue: a new song/stop command drops waiting song/stop commands; 2400-sample gap between deliveries.
- HUD bar: lap time / expected lap (rival pace, then previous lap), capped at 0.97 until the checkpoint. Course position $260030 and $260090 advance far too slowly to use (0x90 ~ 13 x 0x30).
- Goal: md_frozen() is 0, finish_step holds speed 0 and the time limit, hides the player's bike; our bike is drawn over the live frame.

## v0.9.7
- sho_mdsnd queue: the driver leaves $80 in the mailbox ($1C03) after taking an SFX command, so "mailbox == 0" is not "free"; a command is delivered once the mailbox differs from the last one written (or after 0.5 s).
- sho_md_scene is also set in PH_FINISH.

## v0.9.8
- Rival bikes = one sprite each: art tiers by pitch 9/5/3/2 (71/35/17/8 lines, bank 0; addrs fff7/ffff, 060b/0287, 06c1/033b, 0371), zoom regs d5 = vzoom<<5 | hzoom, source lines = h*(1+vz/32), skipped columns hz/64. sho_video_extra_sprites[] injects sprites into draw_sprites (draw_one_sprite).
- sho_video_road_extent(y) reads the road generator's rows (pixel != 3) for the road edges; finish profile fits half-width = a*(y-yv).
- test hooks: SPRLOG, FINDBG.

## v0.9.9
- HUD bar: $260090 (distance, ~13 x $260030) over the lap length (previous lap's value at the checkpoint, 42100 measured by forcing the counter on the converted courses); blended with the time estimate in lap 1.
- Goal: shrink 1/(1+0.2 f), FIN_RIDE 70.
- mdsnd_resync clears Z80 RAM first; sho_md_music_command ignores arcade commands in overlay phases and PH_FINISH.

## 0.9.10
Original Mode race: arcade stop commands (00,83,8e,92,9e,9f) no longer reach the arrange player while PH_RACE/PH_LAUNCH; music stops when the race phase ends (sho_md_hold_music).

## 0.9.11
- Live Original Mode: `reset_request = 2` after the result hand-over (arcade reset behind the Mega Drive screens);
  `md_extra_frames` returns 0 in PH_MDLIVE (the 5-extra-frame hurry starved the audio); `sho_md_before_reset`
  keeps the sound driver when the live screens are up.
- `sho_md_route_sound` (called from the sound latch): Original Mode holds track commands until `racing`
  (music-select previews), plays the race-start command or the held one; stops ignored during the race.
- Goal bike: rival art set (flipped ffff/0287/033b/0371), z = round(32*Ha/h - 32) both axes, tiers at 36/18/10;
  rival size rule h = k*(bottom - y0) fitted from the race's rival sprites (`rival_sample`/`rival_fit`, bank 0,
  palette not c0/c1), fallback k=0.75, y0 = yv - 0.06*(by0 - yv); distance 1/sc += 0.03..0.06 per frame; FIN_RIDE 170.
- Test hooks: MDAUTO=<races> (state-driven Original Mode loop), PHDBG, FINISHTEST samples rivals first.

## 0.9.12
- Mega Drive bar ($BBBA draws it): long $FF0530 += the per-frame course step D0 that is also added to $FF0616
  ($9E5A..$9EBE), capped by $9FE4[race] = laps*$08000000-1; one lap = $0800 in the high word = 4 tiles (32 px);
  laps end when $FF0616 > $800 ($E3FE: $FF060A++, position cleared). Arcade twin: $260030 (+= $6512) and its own
  bar $60444 (cap $8878[class]); stage end when $260030 > $800 ($676E). `bar_acc` sums $260030's steps.
- Harness: RECOMP=1 is needed for the class/music select hook (sho_menu_last_frame), so MDAUTO + RECOMP=1 +
  MDAUTORACE=1 runs a real Original Mode race; HOLDTIME=1; md_harness: POKE=f:addr:val, WATCH=addr,..., BARLOG.

## 0.9.13
- Lap message (Original Mode): MD lap-end routine $E5F6 (gated by $FF06B4<0, set by the checkpoint object $C58C / end-of-race $9B82) formats the lap time ($E844 -> buffer $B804), spawns message object type $20 in slot 6 ($CA80; handler $A98A via table $710A; 120 frames) showing YOU / RIVAL ($C860+8*lap, built at $8DB0: base BCD times at $8D84 = 1'50,1'46,1'30,1'32,1'21,1'17,1'13, hundredths = (3..6)*10 + {0,3,6,0,3,6,0,3}). Laps per race $FA92, lap+1 $FA9A.
- sho_md.c: rival_laps_init, msg_t, LAP TIME shows the current lap; test hook STAGEJUMP.

## 0.9.14
- Lap message sprite template at $9576 (26 sprites: digits pal1, quotes pal2, YOU/RIVAL pal3, banner sprites pal2); LAP TIME flash colour is approximated (green/red letters vs white) - the exact ROM trigger not yet found ($C8B4 is set at $96A8 when the rival's clock passes its lap time, but nothing reads it by absolute address).

## 0.9.15
- Live MD rival table: `$C864 + 8*lap` (BCD 00 mm ss hh) and total `$C83A` are only built ~50 frames after state $11, so race start steps the MD until `$C864` is non-zero (sho_md.c). A lap is ~1'50 at ~150 km/h; the old 31 s/lap estimate was wrong.

## 0.9.16
- MD race end: final lap wrap `$E464` sets `$FF06BC`=$1ED0 and loads the finish stream (`$576F0`); the bike runs until `$FF0616`>$1F (`$9B62`), then `$FF06BE`=-1 clears the throttle (`$9BFA`), the goal object `$B44E` counts `$FF06BC` down by $80 per object step to $FA0, then `$C720`=$1C -> `$C704`=$1D -> results ($25). Goal gate art (checkered block at the roadside) not yet reproduced.
- Banner: message sprites 24/25 (tiles $6F0/$6F3, pal2) = LAP TIME, drawn only when `($2d & 8) == 0`.

## 0.9.17
- END_ZONE objects are captured by running through the `$708` (1800) event (capture jumps to $690 first). Start grid in the MD: 5 rival slots (13-17, type $48/$49) at lateral offsets (+$18) -180,-90,0,90,180 and depth ($0C) $8F,$79,$66,$79,$8F (a V); the port still uses the arcade's grid (more bikes).

## 0.9.18
- Arcade: `$260030` runs 0..$800 per segment; `$6DA4 cmpi.w #$400,D0` spawns a second gate at mid-segment (arcade stages are $400 long); `$260010` counts segments, goal when it equals `$8870[class]` (`$676E`). With a Mega Drive course installed: `$6DA6` patched to $7FFF (no mid gate), `$6E43` banner $2F->$2E (START on lap lines), `$8870[0..3]` = laps. Restored on uninstall.
- Goal: the arcade's `$2600EA`=$1ED0 at the last wrap, `$B6D2` pulls `$260022` (lateral) to centre by 4/frame (MD `$9F26`: 8 per 3-frame step), at pos > $23 `$2600EC`/`$2600E2`=-1 (camera stop, gate counted) -> st > laps -> finish_begin. The MD is the same routine (`$9B50`, stop at $1F).
- HUD plate rows 30-31 of the capture moved back to 24-25. Dev race: Ctrl+0..7 (win32_main) -> sho_md_dev_race -> `$B606` poked while on the Mega Drive menus. Test hooks: GOD, SUBSET, WATCHSUB, POKESUB, DEVRACE, ROMPEEK.

## 0.9.19
- Mode choice on the title: md_frozen() holds the arcade while PH_MODESEL is up on the title (sho_core re-renders the held frame), sho_video_arcade_text() draws in the arcade's text font/palette (colour 1 yellow, 2 white), sho_video_hide_title_text hides INSERT COINS. From the free-play class select the boxed version is still used. EXIT GAME -> sho_md_exit_requested() -> WM_CLOSE.
- INSERT COINS (text row 14, cols 13-24) is drawn 15 px right (centre 167, like the logo and SEGA). clean_logo() clears all-white/black 2-pixel chequer cells from the captured Mega Drive title.

## 0.9.20
- MD background strip: the arcade background layer's scroll (layer 1, `$0C0 - xs`) steps by 512 when its page register flips (e.g. $3302 <-> $3321) on long corners; sho_video now accumulates frame deltas, folding +-512 steps to 0 (unbounded accumulator; plane B and the 4/3 near layer wrap by their own widths).

## 0.9.21
- Ctrl+8 (win32_main) -> sho_md_cheat_upgrade(): on live MD state $25 writes grades `$B618+2i` = top grade (5,5,4,3,3,4) and condition `$B648+2i` = 15, then saves. Test hook CHEAT=frame.

## 0.9.22
- Ctrl+9 -> sho_md_cheat_money(): money is a BCD long at `$B638` (the game's own cap is `#$99999999`, `$57AC`); after a cheat `$C704`=$24 makes the command screen set itself up again. Test hook CHEATM=frame.

## 0.9.23
- md_race_hud: the lap time digits take the same 16:9 left shift (xl) as the LAP TIME plate and the bar.

## 0.9.24
- Mega Drive song commands: $82 Outride a Crisis, $84 Sprinter, $83 Winning Run, $85 Hard Road (sho_md_music_command; corrected by ear by the user).

## 0.9.25
- Mode choice defaults to ARCADE MODE (cur_modesel = 0); the MDAUTO test loop presses down at frame 440.

## 0.9.26
- win32_main: sound levels per soundtrack (sound_mode(): 2 = MD Music on, 1 = external_music, 0 = regular), defaults 10/10, 6/10 (arrange), 10/10.

## macOS build (0.9.26 Mac 1)
| Path | What |
|---|---|
| `src/mac_main.m` | macOS frontend, the twin of `win32_main.c` (same settings, ROM checks, sound levels, pause/reset handling and frame pacing) |
| `src/mac_metal.m/.h` | Metal presenter, the twin of `win32_d3d11.c` (same sharp / smooth / sharp-bilinear shader, compiled at run time) |
| `src/mac_launcher.m/.h` | settings launcher, the twin of `win32_launcher.c` (+ Display "Fit screen", "Open game folder") |
| `src/ini_file.c/.h` | portable shangon.ini reader/writer with the Windows profile-function rules |
| `src/rc_ctx.c/.h` | fiber stack switch in assembly for arm64 and x86-64 (macOS) |
| `build_mac.sh` | universal app (arm64 + x86_64), Command Line Tools only; ends with a 1200-frame self-test |
| `tests/selftest_1200.txt` | state hashes (RECOMP=1, `auto`, 1200 frames) the self-test must reproduce |
| `res/shangon.ico`, `res/shangon.icns` | Windows and Mac icons (made from the same 500x500 picture, rounded corners) |

* Any view width: `sho_set_view_width(w)` (320..`SHO_MAX_W`, even; margin = (w-320)/2) and
  `sho_view_width_for_aspect(a)` = a * 240 rounded to even (the arcade pixel is 4/3/320*224 wide). The renderer,
  `M_002A0C`'s cull limits, the HUD spreading and the Original Mode HUD all work from the margin, so nothing else
  changed. `sho_set_widescreen(1)` = `sho_set_view_width(428)`; 4:3 and 16:9 are bit-identical to before (hash logs
  and screenshots over 6000 frames). Test hook: `VIEWW=370` (or any width).
* Mac "Fit screen": `update_view_width()` sets the width from the shape of the window's usable area every frame
  (not while paused - the pause menu redraws a kept frame of the old width). In native fullscreen macOS keeps
  windows below the notch; if a window does reach the top of a notched screen, `usable_rect()` leaves out
  `safeAreaInsets.top`. 1710x1112 / 1470x956 desktops -> 370 wide; below the notch ~380. The presenter fills the
  area exactly when the picture is within 1.5% of its shape (whole-pixel rounding), otherwise letterboxes.
* Fibers: the system `makecontext`/`swapcontext` are reported broken on native Apple Silicon, so on `__APPLE__`
  (or with `-DRC_ASM_FIBERS`) rc_runtime uses `rc_ctx_switch`: it pushes the callee-saved registers (arm64: x19-x28,
  fp, lr, d8-d15; x86-64: rbx, rbp, r12-r15, MXCSR, x87 CW) and swaps stack pointers; a new context's first switch
  "returns" into `rc_ctx_entry`, which calls fn(arg). x86-64: lockstep-identical to ucontext over 6000 frames (4:3
  and 16:9) and ~7% faster; arm64: assembled for arm64-apple-macos11 and checked by disassembly; the build's
  self-test runs it on the Mac.
* Pacing: a CVDisplayLink signals each refresh (the Windows build's vsync'd present); 60/120 Hz lock k refreshes per
  game frame, other rates distribute 60.05 Hz frames; no ticks -> clock pacing. Presents skip a hidden window
  (no waiting for drawables), so the game and audio keep running.
* Audio: default output AudioUnit (48 kHz s16 stereo) pulling from a lock-free ring; the frame loop keeps ~3200
  samples queued with the same +/-1% drift correction as Windows.
* Input: GameController framework (extendedGamepad) with the XInput mapping; keyboard from key codes (Carbon kVK
  values). Cmd+key goes to the menus; Ctrl is not a throttle (Ctrl+arrows are Mission Control). All keys are
  released when Cmd goes down (macOS sends no key-up for keys released while Cmd is held).
* Game folder = the folder holding the .app; App Translocation or a read-only folder -> ~/Library/Application
  Support/Super Hang-On Ultimate. The Mega Drive capture thread gets a 64 MB stack (macOS secondary threads default to 512 KB).
* `-ffp-contract=off` keeps arm64 from fusing float multiply-adds, so float-derived inputs (steering ADC) round
  as on x86 and the self-test hashes match across machines.
* Gamepads (Mac 2): `read_pad()` reads `extendedGamepad`, else the named controls of `physicalInputProfile`
  (macOS 11+, third-party pads), else `microGamepad`. `GCController.shouldMonitorBackgroundEvents = YES` (the game
  runs its own loop, so macOS may not consider it the front app) and `pump_events()` also runs the main run loop
  (`CFRunLoopRunInMode`) so connection notifications are delivered. `gamepad.log` (game folder): pads seen at start,
  (dis)connections, and control changes for 60 s after each; the title shows "pads: N".
* Gamepads (Mac 3): the framework delivered nothing to the game for a GameSir Nova Lite (connected, but no value
  changes even with `shouldMonitorBackgroundEvents`). Pads are now also read with IOKit (`IOHIDManager`, GamePad /
  Joystick / MultiAxis usages), polled every frame with `IOHIDDeviceGetValue` - no run loop or front-app logic
  involved. X/Y = left stick, hat = d-pad, triggers = Simulation Brake/Accelerator or a Z/Rz (else Rx/Ry) pair that
  rests at its minimum; buttons by number via `[pad] map=` (default 1,2,4,5,8,11,12 = A B X Y RB Back Start). HID pads
  are ignored once the framework delivers events (no doubled input). The event pump now uses `[NSDate distantPast]`.
