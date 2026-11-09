SUPER HANG-ON ULTIMATE - NATIVE WINDOWS 11 BUILD   v0.9.32 alpha
Native Port + Mega Drive Original Mode Mod
=========================================================

THE LAUNCHER (SETTINGS SCREEN)
- When the game starts, its settings appear inside the game window (fullscreen or not) over the Super Hang-On
  Ultimate picture, in the arcade's own text font: Up/Down choose a line, Left/Right (or a mouse click) change it,
  Enter or PLAY starts, Esc quits. A pad works too (d-pad, A, Start). Click the FOLDER line to open the game folder.
  Underneath: whether shangon.zip and the Mega Drive ROM were found. After PLAY a loading bar shows what is being
  got ready. Start with -play (or set [launcher] show=0 in shangon.ini) to skip it.

WHAT'S NEW IN 0.9.32
- Launcher: the loading bar now sits between the ROM lines and the folder line.

WHAT'S NEW IN 0.9.31
- Launcher: the settings panel sits flush in the bottom-left corner, without the yellow line down its left edge.

WHAT'S NEW IN 0.9.30
- Windows and Mac: the separate launcher window is gone; the launcher is now the same in-window settings screen as
  on Linux (new picture, arcade font, loading bar after PLAY), in all three versions.
- The version number is shown at the bottom right of the launcher, under the bike.

WHAT'S NEW IN 0.9.29
- Linux: after PLAY the launcher stays up with a loading bar at the bottom, with what is happening written in it
  (checking the arcade ROM, loading each arranged song, preparing Original Mode, starting the game).

WHAT'S NEW IN 0.9.28
- The launcher has its artwork back on the left: the original Super Hang-On arcade flyer (Windows and Mac).
- Linux: the launcher now shows the Super Hang-On Ultimate picture behind it, with the settings in the arcade's own
  text font (the one used for the scores and the race HUD) at the bottom left, under the logo.

WHAT'S NEW IN 0.9.27
- Apply Mega Drive Controls now decides the steering in both modes. Ticked: the Mega Drive's handlebar steering in
  Arcade Mode and Original Mode (as before). Not ticked: the arcade's own steering in both - Original Mode used to
  keep the Mega Drive steering regardless. (The FRAME part changes the steering only with the option ticked.)
- Original Mode music follows the Music choice in the launcher on the music select screen and in the race:
  Regular Soundtrack previews and plays the arcade's songs, Arrange Soundtrack the MP3s, Mega Drive Music the
  Mega Drive's. The Original Mode title, menus, garage and results screens keep the Mega Drive's own music.

WHAT'S NEW IN 0.9.26
- The project is now called Super Hang-On Ultimate. The launcher has no artwork any more: it shows the new name and the
  line "Native Port + Mega Drive Original Mode Mod" above the settings, and the game has a new icon.
- Sound levels (pause menu SOUND) are now kept separately for each soundtrack, with new defaults: Regular Soundtrack
  and Mega Drive Music start at music 10 / effects 10, Arrange Soundtrack at music 6 / effects 10. Changes you make in
  the pause menu are remembered for the soundtrack you are using (shangon.ini [audio] music_level_regular/_arrange/
  _megadrive and effects_level_...). Earlier single settings are no longer used.

WHAT'S NEW IN 0.9.25
- The mode choice on the title screen now always starts on ARCADE MODE.

WHAT'S NEW IN 0.9.24
- Mega Drive Music: each song now plays under its right name (Outride a Crisis, Sprinter, Winning Run and Hard Road
  were mixed up).

WHAT'S NEW IN 0.9.23
- 16:9: the Original Mode lap time now sits beside the LAP TIME heading at the same distance as in 4:3 (it used to
  stay in the middle of the screen when the heading moved to the left edge).

WHAT'S NEW IN 0.9.22
- Development cheat: Ctrl+9 on the Original Mode command screen gives the most money the game can hold
  ($9,999,999,900 on the money box). 'MAX MONEY!' shows at the bottom, the command screen redraws with the new
  amount and the save is updated. (Ctrl+8, the upgraded bike, now redraws the command screen too.)

WHAT'S NEW IN 0.9.21
- Development cheat: press Ctrl+8 on the Original Mode command screen (COMMAND / PARTS / MECHANIC / RACE / END) for a
  fully upgraded bike - the best part in every slot, all in top condition. 'BIKE UPGRADED!' shows at the bottom and
  the save is updated straight away.

WHAT'S NEW IN 0.9.20
- Original Mode backgrounds no longer jump on long corners. On a long bend the arcade engine swaps its backdrop's
  tilemap pages and moves its scroll by half a screen-set at the same moment (invisible on the arcade's own picture);
  the Mega Drive background followed that scroll and so jumped by half its width. It now follows the movement from
  frame to frame, ignoring the page swap, so it pans smoothly all the way round.

WHAT'S NEW IN 0.9.19
- Title screen: Start now brings up ARCADE MODE / ORIGINAL MODE / EXIT GAME straight on the black background under the
  logo, in the arcade's own lettering, where INSERT COINS flashes (no box, no dimming, and the title stays put while
  you choose). EXIT GAME closes the game. The 'Mega Drive Enhancements' line is gone.
- INSERT COINS is centred under the logo and the SEGA badge.
- Original Mode title: the chequered bits left of the S and under the P of the logo are cleaned off.

WHAT'S NEW IN 0.9.18
- Original Mode laps now work like the Mega Drive's. The arcade engine was putting a CHECK gate in the middle of
  every lap (arcade stages are half a Mega Drive lap long), so the progress bar reached a gate at a quarter and the
  race counted that gate as a lap. Now there is one gate per lap, on the lap line, and it says START as on the Mega Drive.
- The end of a race now uses the game's own goal code (the arcade and Mega Drive versions are the same routine):
  on the last lap the lap line becomes a GOAL gate, the bike is steered to the centre of the road, and the camera stops
  just before the gate so the whole gate is in view; then the bike rides away through it. The lap time stops there.
- The LAP TIME plate in the HUD has its rounded bottom edge back, and the stray lines above the progress bar are gone.
- Development course select for Original Mode: press Ctrl+1 to Ctrl+7 to always start that race (shown at the bottom
  of the Mega Drive screens with its lap count); Ctrl+0 turns it off. Kept in shangon.ini under [dev] original_race.

WHAT'S NEW IN 0.9.17
- Roadside objects near the end of the lap (the finish area: billboards and palms placed after the 1800 event)
  are now taken from the Mega Drive too; they were left out before, so the run-in to the goal had gaps.

WHAT'S NEW IN 0.9.16
- The way an Original Mode race ends now follows the Mega Drive's code: after the last lap line the bike does NOT stop
  at once - it runs on to course position $20 (the goal), with the final YOU / RIVAL message showing, and only then
  does the speed drop to 0 and the bike ride away. The result uses your time at the lap line, not at the stop.
- The flashing LAP TIME is the banner above the YOU / RIVAL message (not the HUD label): it blinks every 12 frames,
  green when you are ahead of the rival's lap time and red when behind. The HUD label no longer changes colour.

WHAT'S NEW IN 0.9.15
- Original Mode with the Mega Drive ROM now reads the Mega Drive's own rival data when a race starts: the rival's
  total time and each lap time (the Mega Drive builds these a moment after you choose the race). Before this the
  rival's time was missing at race start, so the rival pace shown in the race was an estimate; the lap message's
  RIVAL times now match what the Mega Drive game uses (for example race 1: 1'50"63, 1'50"43).

WHAT'S NEW IN 0.9.14
- The LAP TIME label now flashes after each lap, green when you are ahead of the rival's lap time and red when behind,
  and the lap message stays about four seconds, as on the Mega Drive.

WHAT'S NEW IN 0.9.13
- Original Mode lap message, from the Mega Drive game's own logic: each time you cross the lap line the
  LAP TIME resets to 0 and a 'YOU m'ss"hh / RIVAL m'ss"hh' message shows for two seconds, as on the
  Mega Drive. The rival's lap time is the ROM's base time for the race (1'50 for race 1 down to 1'13 for
  race 7) with the Mega Drive's random hundredths added, like its rival table.
  (Found in the ROM: lap routine $E5F6, message object $A98A, rival table built at $8DB0.)

WHAT'S NEW IN 0.9.12
- Original Mode progress bar now works exactly like the Mega Drive game's. The Mega Drive adds each
  frame's step along the course (its course position counter, which runs from 0 to $800 per lap) into
  a bar counter; one lap is exactly one section of the bar. The arcade engine uses the same position
  counter for the converted course, so the bar now sums that, and reaches the middle tick at the
  CHECK gate (the lap line).

WHAT'S NEW IN 0.9.11
- Mega Drive music stuttering on the Original Mode menu after a race: behind the Mega Drive's screens the
  arcade machine was being fast-forwarded (6 frames per frame) through its own goal and name entry, which
  could starve the music of time. The arcade machine is now simply reset after each race instead.
- Arrange (and Mega Drive) music in Original Mode starts once, when the race starts. The arcade's music
  select preview no longer starts it early.
- The goal drive-off now copies the rival bikes exactly: same art set, same zoom rule (same zoom both ways;
  before, our bike was drawn too narrow), the same size for each road line as the rivals on that course
  (measured live from their sprites), the 4-frame wheel animation, and the pace of a rival pulling away
  from the start, so it steps down through the sizes the way they do.

WHAT'S NEW IN 0.9.10
- Original Mode with the Arrange soundtrack: the mp3 no longer cuts out mid-race. The arcade's own
  'stop music' commands are ignored during the race and the song ends cleanly when the race does.

WHAT'S NEW IN 0.9.9 (ALPHA)
---------------------------
* The progress bar now follows the bike's distance along the track (the game's own distance counter) against
  the length of the lap, so the middle checkpoint lands at the right place on the bar. The lap length is
  re-measured at each checkpoint; until then the converted course's length is used, with the old time-based
  estimate as a safety net if the two disagree badly.
* The goal bike shrinks more slowly, so it stays closer to the size of the bikes ahead, and the ride lasts a
  little longer.
* Mega Drive music after a race: the sound driver is now restarted completely clean (its memory is cleared
  like a first start, so nothing is left over from the song before), and the arcade game's own sound commands
  no longer interfere with the Mega Drive's screens or the goal.

WHAT'S NEW IN 0.9.8 (ALPHA)
---------------------------
* The goal bike is now drawn the way the game draws the other bikes. They are single hardware sprites
  that use pre-drawn bike art in four sizes plus the sprite chip's zoom between sizes. Our bike goes through
  the same sprite code with the same art, so it shrinks and pixelates exactly like the bikes ahead of you.
* Its line along the road now comes straight from the road generator's own data (the road's left and right
  edge on each screen line, and the true horizon) instead of being guessed from the picture's colours.

WHAT'S NEW IN 0.9.7 (ALPHA)
---------------------------
* Mega Drive music: the Parts screen music now plays. After the menu click sound the sound driver leaves its
  own marker in its command slot, which the command queue wrongly read as "busy", so later music commands
  were never delivered.
* The goal sequence keeps the course's own colours, sky and background (they were switching to the arcade's
  once the race ended).
* The goal bike's look and path are still being worked on.

WHAT'S NEW IN 0.9.6 (ALPHA)
---------------------------
* Mega Drive music: fixed the wrong track in the Original Mode menus (a stale song command could be sent
  after the right one). Only the newest song or stop command is kept now.
* Original Mode progress bar: it now fills smoothly through each lap (by the lap's time against its expected
  length, the rival's pace for the first lap and your last lap after that) and is exact at every checkpoint.
* Goal sequence: the game now keeps running through it, so the other bikes keep riding away ahead of you
  with the game's own movement and shrinking, while your bike rides off and the camera holds still.

WHAT'S NEW IN 0.9.5 (ALPHA)
---------------------------
* The Original Mode race progress bar now measures the real length of each lap (one lap of a converted
  course is longer than the bar assumed), so it fills steadily and no longer stalls or jumps at checkpoints.
* Mega Drive music commands are now queued and handed to the sound driver one at a time, only when it is ready
  for them. Before, a command sent while the driver was restarting (for example the screen's music right
  after a race, or when moving between the command and mechanic screens) could be wiped or overwritten,
  which caused glitchy or wrong music.

WHAT'S NEW IN 0.9.4 (ALPHA)
---------------------------
* The Mega Drive goal: the bike now rides along the real road. Its path, size and the horizon are taken
  from the road in the picture (the road's width at each line), so the wheels stay on the road, it follows
  the road's bends and it meets the horizon exactly where the road vanishes.
* Mega Drive Music is now a choice in the Music list (Regular / Arrange / Mega Drive Music). It is greyed
  out and cannot be picked unless the Mega Drive ROM is found and Mega Drive Enhancements is ticked.
  (The separate tick box is gone.)
* The Mega Drive sound now starts and stops cleanly: after a race the Mega Drive screens' music restarts
  from a clean state instead of picking up in the middle (this was the glitchy music when coming back to
  the Original Mode menus), and the sound driver is shut down when Original Mode is left if the Mega Drive
  Music is not in use.

WHAT'S NEW IN 0.9.3 (ALPHA)
---------------------------
* Mega Drive Music plays at the right speed. The Mega Drive's sound program was being run far too slowly
  (the Z80 : FM clock ratio was inverted), which made the songs slow and thin; they now play in full.
* Choosing ARCADE MODE on the mode screen gives one credit and goes straight to the map (class) screen -
  no extra Start press needed.
* The Mega Drive goal: the bike now drives off along the road at a steady speed, shrinking quickly towards
  the horizon where it vanishes (no easing, no rising into the sky).

WHAT'S NEW IN 0.9.2 (ALPHA) - MEGA DRIVE MUSIC
-----------------------------------------------
* NEW LAUNCHER OPTION "Mega Drive Music" (needs your Mega Drive ROM, like the other Mega Drive options).
  The Mega Drive's own sound program is run from your ROM - its Z80 sound driver, a YM2612 FM chip and
  the PSG are emulated - so the real Mega Drive songs play, nothing is recorded or stored on disk.
  With it ticked the Mega Drive versions of the four songs replace the arcade's in the whole game
  (Sprinter, Winning Run, Hard Road and Outride a Crisis).
* With the option off, Regular or Arrange music still applies everywhere - except on the Original Mode
  screens (menus, mechanic, parts) which always play their own Mega Drive music.
* The Mega Drive's bumps: touching another bike now costs the frame and the muffler one point each,
  as on the Mega Drive (detected as a sudden speed loss without a fall or braking).
* The launcher window is a little taller to fit the new option.
* Mega Drive sound effects (engine, crashes) are not played yet - only its music.
* New credit: Nuked-OPN2 YM2612/YM3438 emulator - nukeykt (LGPL-2.1).

WHAT'S NEW IN 0.9.1 (ALPHA)
---------------------------
* Original Mode: after choosing the song on the Mega Drive's music screen the game goes straight to the
  start of the race (the arcade's class and music screens are no longer shown).
* The Mega Drive's goal: crossing the line on the last lap, the picture stops, your bike rides off and
  shrinks into the distance with the lap time held (SPEED goes), then the screen fades to black and the
  Mega Drive's results come up.
* The arcade's checkpoint messages (EXTENDED PLAY, LAP TIME) no longer appear in Original Mode races.
* The Mega Drive's damage model, taken from its program, now applies in the race: every fall costs
  frame 3, engine 1, brake 2 and muffler 2; a frame, engine or tire below zero is broken and you retire
  ("YOUR FRAME IS BROKEN!"). At 180 km/h and over a worn-out engine breaks, and so does an empty oil; a
  worn-out brake breaks when used; a worn-out muffler or tire wears through after a while. The damage is
  handed back to the Mega Drive, which then shows it on its screens (and asks for repairs).
* Arcade mode and Original Mode are now walled off from each other: entering Original Mode restarts the
  arcade machine cleanly, and leaving it (END, the pause menu's EXIT, BACK) resets to the title screen,
  restoring the arcade's courses, high scores and HUD.

WHAT'S NEW IN 0.9 (ALPHA) - THE MEGA DRIVE'S OWN ORIGINAL MODE (STAGE 3)
---------------------------------------------------------------------
* With the Mega Drive ROM present, Original Mode is now the Mega Drive game itself, running live inside
  the port: its command screen, parts shop, mechanic, rivals, sponsor, prize money, wins and losses and
  its own rules. Only the races are run by the arcade engine, on the Mega Drive's courses.
* Choosing RACE and a song on the Mega Drive starts the arcade race on that course with that song and
  your Mega Drive parts. When you finish, your time goes back to the Mega Drive, which works out the
  result its own way (YOU WIN!! / YOU LOSE, prize, rival's comment, parts wear).
* Progress is saved automatically every time you come back to the command screen (a snapshot of the
  Mega Drive in shangon_original_md.sav). CONTINUE picks up exactly where you left off.
* END on the command screen shows the Mega Drive's password, then returns to the arcade title screen.
* Controls on the Mega Drive screens: d-pad/stick to move, A = select (the Mega Drive's C button),
  B = the Mega Drive's A button, X/Y/RB = its B button, Start = Start. Keyboard: arrows, Z/Space,
  Backspace/X, Enter.
* Known: the Mega Drive screens are silent for now (its sound chips aren't emulated); crashes in the
  arcade race don't yet add extra parts damage (the Mega Drive applies its normal per-race wear); the
  arcade's class and music screens flash up briefly while the race is being set up.

WHAT'S NEW IN 0.8.9 (ALPHA)
---------------------------
* Mega Drive Controls now use the Mega Drive's exact steering model, taken from its program: a
  virtual steering wheel that the d-pad turns at the Mega Drive's own rates (from its table, by frame
  part in Original Mode, plus a little more with speed), updated at the Mega Drive game's own pace (every
  third frame). Letting go straightens the bike up gradually, exactly as on the Mega Drive. The stick
  works like the d-pad (past a third of its travel = pressed).

WHAT'S NEW IN 0.8.8 (ALPHA)
---------------------------
* Pause menu: no background box, and PAUSE and the options are centred on the screen (above the bike).

WHAT'S NEW IN 0.8.7 (ALPHA)
---------------------------
* The Mega Drive mode pause screen now has a menu under the blinking PAUSE:
    CONTINUE - back to the race
    SOUND    - MUSIC and EFFECTS levels (0-10, left/right to change), BACK to return
    EXIT     - back to the title screen
  Gamepad: d-pad/stick to move, A or Start to choose, B to go back. Keyboard: arrows, Enter/Z/Space,
  Backspace/X. P opens and closes it too. The sound levels are saved in the ini (music_level,
  effects_level).

WHAT'S NEW IN 0.8.6 (ALPHA)
---------------------------
* New icon for SuperHangOn.exe.
* The launcher now shows the Mega Drive ROM's status on its own line under the arcade ROM's.
  Without the Mega Drive ROM, the two Mega Drive options are greyed out (your choices are kept for
  when the ROM is added; the launcher re-checks when you switch back to it).

WHAT'S NEW IN 0.8.5 (ALPHA)
---------------------------
* Pause screen in Mega Drive mode: press Start (or P) during a race and the game freezes with the
  Mega Drive's own blinking "P A U S E". Start again to carry on.
* The launcher artwork is wider and now fills the full height of the window.

WHAT'S NEW IN 0.8.4 (ALPHA)
---------------------------
* Roadside object pictures are now drawn from the Mega Drive's own sprite definitions, so every object
  is clean and complete - including the Bird Bird sign on the first course, the Genesis, Soreban and
  Disco signs, rock piles, cacti, lamps and trees. No more stray riders or wrong pieces.
* Object sizes follow the Mega Drive's own sizes (trees and palms a little bigger).
* In 16:9 the Mega Drive HUD is spread across the screen: LAP TIME and the progress bar on the left,
  the time in the middle, SPEED on the right.
* The progress bar now matches the Mega Drive: two rails with the yellow band filling between them from
  the S, and green ticks between laps.
* Known: objects in the finish area of a lap are not drawn yet.

WHAT'S NEW IN 0.8.3 (ALPHA) - MEGA DRIVE ROADSIDE OBJECTS
-------------------------------------------------------
* Mega Drive courses now have the Mega Drive's roadside objects (palms, NOK Spark Plugs signs, chevrons,
  cacti, street lamps, trees, banners...) in the Mega Drive's positions, placed and scaled by the arcade
  engine and drawn with the Mega Drive pictures from your ROM.
* The pre-race banner is gone.
* The Mega Drive graphics are now collected in the background while the launcher is open.
* Known: an object kind that the capture couldn't get a clean picture of still shows an arcade sign, and
  a few pictures may pick up a passing rider. Object sizes are a first calibration.

WHAT'S NEW IN 0.8.2 (ALPHA)
---------------------------
* Mega Drive courses now have both of the Mega Drive's background layers: the sky and clouds, and the
  nearer layer in front of it (the sea with its rocks and islands, hills, skylines...), which scrolls a
  little faster through the bends exactly as on the Mega Drive.
* Original Mode races use the Mega Drive race HUD: LAP TIME, SPEED and the Mega Drive S-G progress bar
  (your position across all laps, with a tick between laps). The arcade TOP/TIME/SCORE panel is hidden.

WHAT'S NEW IN 0.8.1 (ALPHA) - MEGA DRIVE COLOURS AND BACKGROUNDS
---------------------------------------------------------------
* Each Mega Drive Original Mode course now has its Mega Drive look in the arcade engine: its road,
  kerb, line, ground and sky colours, and its own background (beach and sea, sunset sea, city skyline,
  night city, sunset hills, purple-sky hills...) as a single layer with no parallax, scrolling through
  the bends. These are taken from your Mega Drive ROM at start-up (about a second).
* Still to come: the Mega Drive roadside objects (arcade objects are still shown), then the Mega Drive's
  own Original Mode menus and rules.

WHAT'S NEW IN 0.8 (ALPHA) - MEGA DRIVE COURSES IN THE ARCADE ENGINE, STAGE 1
-------------------------------------------------------------------------
* With the Mega Drive ROM present, Original Mode races are now the Mega Drive's own Original Mode
  courses: its road layouts (curves and bends) converted at start-up and raced in the arcade engine,
  each driven for the Mega Drive's number of laps (LAP 1/2 on screen; the race ends after the last lap).
* Still to come: the Mega Drive colour schemes, backgrounds (single layer, no parallax) and roadside
  objects for these courses - for now they use arcade scenery. Then the Mega Drive's own Original Mode
  menus and rules running live.

WHAT'S NEW IN 0.7 - MEGA DRIVE SCREENS FOR ORIGINAL MODE
--------------------------------------------------------
* Original Mode now uses the Mega Drive version's own screens: the title, the COMMAND screen (rival,
  mechanic and sponsor portraits and boxes) and the PARTS screen with the bike diagram, which lights up
  the part you are looking at. Text uses the Mega Drive font.
* These graphics come from YOUR Mega Drive ROM: put "Super Hang-On (World) (En,Ja) (Rev A)" (.md, .bin,
  .gen, or a .zip containing it) in the same folder as SuperHangOn.exe. The launcher says whether it was
  found. When the game starts it briefly runs the Mega Drive game in the background to collect the
  screens (well under a second); nothing is written to disk. Without the ROM, Original Mode uses the
  arcade-style screens from 0.6. (A different file can be named with [megadrive] rom= in shangon.ini.)
* MECHANIC uses the parts screen with a condition bar in each box; RACE and the results appear on the
  COMMAND screen (lap times, WIN/LOSS, wear from the mechanic, prize money from the sponsor).

WHAT'S NEW IN 0.6.2
-------------------
* The game always starts on the title screen: credits left over from the last session are no longer
  kept (with credits in hand the arcade skips straight to the class-select map).

WHAT'S NEW IN 0.6.1
-------------------
* Free play: the arcade goes straight to the class-select map, so the ARCADE / ORIGINAL mode choice now
  appears over the map (its countdown is held while you choose or use the Original Mode screens).

WHAT'S NEW IN 0.6 (PROTOTYPE) - MEGA DRIVE ENHANCEMENTS
-------------------------------------------------------
Two new tick boxes on the launcher. Both are off by default; with both off the game is exactly the
arcade original, as before.

* Mega Drive Enhancements - adds the Mega Drive version's ORIGINAL MODE, played on the arcade engine.
  Press Start on the title/attract screen and choose ARCADE MODE or ORIGINAL MODE.
  - COMMAND menu: PARTS, MECHANIC, RACE, END - with your rival, lap times, wins/losses and money.
  - PARTS: the Mega Drive shop - frame, engine, brake, muffler, oil and tire, every grade with the
    Mega Drive's names and prices (Iron Cradle up to Titanium Monocoque, OHC Normal up to Twin Turbo...).
    You start with $0 and stock parts, and earn prize money by racing.
  - RACE: pick a course (Africa first; beat all four rivals on a course to open the next one). The
    race is the full arcade course with no time limit: beat your rival's lap time to win. A lap timer
    replaces TIME, the "R" bar shows the rival's pace, and at each checkpoint you see the gap.
    Hold Back (View) for 1.5 seconds to retire.
  - Parts change the bike: the FRAME sets the steering inertia (lighter frames turn in and
    counter-steer faster, using the Mega Drive's own handlebar rates; with Apply Mega Drive Controls
    ticked); ENGINE sets power and top speed
    (only turbo engines reach the arcade's 324 km/h and use the supercharger); MUFFLER and OIL add
    power; BRAKE sets braking; TIRE sets grip in corners.
  - Parts wear in races (crashes wear the tires and frame). Worn parts perform worse. The MECHANIC
    repairs them for money.
  - Progress saves automatically to shangon_original.sav in the game folder (a small text file:
    money, parts, wear, rivals beaten, best times). Choose CONTINUE next time; delete the file, or
    choose NEW GAME, to start again.
  - Original Mode races don't touch the arcade high-score tables.
  Menus: stick/d-pad to move, A to choose, B to go back (keyboard: arrows, Z/Space/Enter, Backspace).

* Apply Mega Drive Controls - steering behaves like the Mega Drive version: the stick drives a
  virtual handlebar that turns in, counter-steers and re-centres at set rates (a little quicker at
  speed), with full lock at full stick travel. Makes analogue sticks much easier to use. Works in
  the arcade game too (using the Mega Drive Arcade Mode handling) as well as in Original Mode (using
  the handling of your frame part). Not ticked, both modes steer like the arcade machine.

Prototype notes: the rivals after Mia Ferraru, their lap times, prize money and the part effect
strengths are first-pass values for testing and will be tuned. Original Mode needs cpu=recompiled
(the default).

WHAT'S NEW IN 0.5.1
-------------------
* Class select and music select can also be confirmed with the right trigger (as well as Start or A),
  so you can cruise through both menus on the throttle. Pull it again for each screen.

WHAT'S NEW IN 0.5
-----------------
* Arrange Soundtrack support (added by ts743 - see ARRANGE SOUNDTRACK above): the four music-select songs can
  play arranged MP3s from the "music" folder while engine sounds, effects and jingles stay original.
* Launcher: new "Music" option - Regular Soundtrack or Arrange Soundtrack. The line under it shows
  whether the arranged tracks were found. Missing tracks fall back to the regular music.
  Volume: [audio] music_volume=0..100 in shangon.ini.

WHAT'S NEW IN 0.4
-----------------
* Launcher: set Demo sounds, Difficulty, Time limit (the arcade's DIP switch B) and the display mode
  (16:9 default, or 4:3) before the game boots. It also checks your ROM: green = shangon.zip found and
  all 25 files verified, red = missing or wrong (PLAY stays disabled). Your choices are remembered.
  Enter = PLAY, Esc = quit. Start with -play (or set [launcher] show=0) to skip it. F9 in-game is unchanged.
* Class select and music select: navigate with the left stick or d-pad (down/right = next,
  up/left = previous) and choose with Start or A. The brake still cycles as on the arcade cabinet.
* Initials entry also accepts the left stick (up/down) as well as the d-pad.

WHAT'S NEW IN 0.3
-----------------
* 16:9 HUD: TOP score, course, stage and the progress bar sit at the left edge, TIME stays centred,
  SCORE and SPEED sit at the right edge.
* The dissolving curtain at the start of a race now covers the full 16:9 view.
* Initials entry on the gamepad: D-pad up/down changes the letter (both directions, holding repeats),
  A enters it. Pick "<" to delete and "=" to finish. The original pedal controls still work.

WHAT'S NEW IN 0.2
-----------------
* The game now runs as C code recompiled from the arcade program (both 68000 CPUs).
  It is proven identical to the original CPU emulation: over 20,000-frame test runs, every
  byte of RAM and every CPU register matched on every frame.
* 16:9 extended view (default). The game itself now places roadside objects and rival bikes
  in the extra area instead of culling them at the old 4:3 edge. F9 switches 16:9 / 4:3.
* GPU presentation (Direct3D 11): vsync-locked frame pacing - on 60 Hz and 120 Hz displays
  the game locks to the display for perfectly even motion - plus sharp-bilinear scaling.
  Falls back to the previous GDI output automatically if Direct3D is unavailable.

SETUP
-----
1. Put your MAME ROM set "shangon.zip" next to SuperHangOn.exe (Super Hang-On sitdown/upright,
   unprotected - MAME 'shangon'). The game graphics and sound come from it.
2. Run SuperHangOn.exe. It starts fullscreen in 16:9. SmartScreen may warn because the exe is
   unsigned: "More info" > "Run anyway".
3. Keep the folder somewhere writable (not Program Files): high scores go to shangon.nv and
   settings to shangon.ini. If you used v0.1, delete the old shangon.ini to get the new options.
NOTE: this build contains C code translated from your copy of the game's program ROM.
It still needs your ROM to run, but please keep it for personal use and don't share it publicly.

ARRANGE SOUNDTRACK (OPTIONAL)
-----------------------------
The launcher's "Music" option can play arranged versions of the four music-select songs instead of the
arcade originals. The arranged MP3s are NOT included: you supply them. The game only finds them if:

  1. They are in a folder called  music  (lower case) inside the game folder, i.e. next to SuperHangOn.exe:

         SuperHangOn.exe
         shangon.zip
         music\
             Outride a Crisis.mp3
             Sprinter.mp3
             Winning Run.mp3
             Hard Road.mp3

  2. They have exactly these names, with the .mp3 ending: "Outride a Crisis", "Sprinter", "Winning Run" and
     "Hard Road" (spaces and capital letters as shown). They must be real MP3 files, mono or stereo.
     A file with any other name is ignored.

Then choose "Arrange Soundtrack" in the launcher's Music list. The line under the list tells you what was
found: "all 4 tracks found", "2 of 4 tracks found", or "not found". If a file is missing or can't be read, only
that song falls back to the arcade original, so you can add the songs one at a time.

Only the four music-select songs change. Engine noise, sound effects, the countdown and the other arcade
jingles stay original. Songs loop on long races; pause freezes the music. The MP3s are decoded when the game
starts and use about 170 MB of memory in total. Volume: the pause menu SOUND screen, or
[audio] music_volume=0..100 in shangon.ini. Arrange Soundtrack also applies in Original Mode.

XBOX-STYLE GAMEPAD
------------------
  Left stick / D-pad ..... Steer (analogue handlebar)
  Right trigger .......... Throttle (analogue)    A = full throttle
  Left trigger ........... Brake (analogue)       B or X = full brake
  RB or Y ................ Supercharger (turbo - works at high speed)
  Start (Menu) ........... Start
  Back (View) ............ Insert coin
  Class / music select ... Left stick or d-pad = move, Start, A or right trigger = choose
  Initials screen ........ D-pad or stick up/down = change letter, A = enter letter
  Original Mode screens .. Stick/d-pad = move, A = choose, B = back; hold Back in a race = retire

KEYBOARD
--------
  Left/Right or A/D ...... Steer       Up / W / Z / Ctrl ... Throttle
  Down / S / X ........... Brake       Space / Shift ....... Supercharger
  5 or C ................. Coin        1 or Enter .......... Start
  F9 ..................... 16:9 / 4:3  F8 .................. Scaling filter (sharp / smooth / sharp-bilinear)
  Alt+Enter or F11 ....... Fullscreen  Esc ................. Leave fullscreen
  Alt+F4 ................. Quit        P ................... Pause      F3 ..... Reset
  F2 (hold) .............. Arcade test/service menu   9 ...... Service coin

SETTINGS (shangon.ini)
----------------------
  [video]    fullscreen, widescreen, filter, stretch, renderer (d3d11/gdi),
             vrr (1 = for G-Sync/FreeSync monitors that aren't 60/120 Hz: arcade-rate presents with tearing allowed)
  [input]    deadzone, steer_curve, swap_pedals
  [game]     demo_sounds, difficulty, time, free_play (the first three are also on the launcher)
  [audio]    external_music (launcher "Music": 0 regular, 1 arrange), music_volume 0..100
  [megadrive] enhancements (Original Mode), controls (Mega Drive steering) - both on the launcher
  [launcher] show (1 = show the launcher first)
  [advanced] cpu = recompiled (default) or interpreter (original CPU emulation, for comparison)
  Command line: -play (skip launcher), -window, -fullscreen, -gdi, -interpreter
  The window title shows the active mode, e.g. "[16:9, sharp-bilinear, Direct3D 11, C]".

KNOWN LIMITS (v0.2)
-------------------
* Gamepad menu navigation and initials entry need cpu=recompiled (the default); in interpreter mode
  use the original controls (brake to cycle, Start to choose).
* On displays that are not a multiple of 60 Hz (e.g. 144/165 Hz) without VRR, motion is
  slightly uneven; set vrr=1 if your monitor supports G-Sync/FreeSync.

CREDITS / LICENCES (see licenses folder)
  Musashi 68000 core - Karl Stenerud (MIT-style)    Z80 core - superzazu (MIT)
  Nuked-OPM YM2151, Nuked-OPN2 YM2612 - nukeykt (LGPL-2.1)   puff inflate - Mark Adler (zlib)
  dr_mp3 - David Reid (MIT-0)                       Arrange soundtrack integration - ts743
  Hardware behaviour documented by the MAME project. Super Hang-On is (c) SEGA; not affiliated with SEGA.
