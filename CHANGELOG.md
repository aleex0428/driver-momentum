# Changelog

## 0.2.0

Only the launcher's own APIs, as the Addon Browser now asks (23 Nov):
- **The form works through the launcher's official live car levers.** Pace: `ai.grip_scale` and `engine.power_scale`, half each. Driving quality: a wider line (`ai.offset_variation.max_offset`), a little less grip and less eagerness to attack (`ai.overtake.attacker_close_enough_car_lengths`). Defending: `ai.blocking.max_blocking_distance`, as before. They are layers the core keeps and gives back by itself: the addon no longer writes anything into the game's memory, and no longer reads the game's code (the `+0xa42` mov) or its AI records.
- **The grid comes from the live state:** each car's driver and race number from the game's UDP participants, its name from `live::driver_name`, the session from the live session type. A career still uses its event context. The `site_Ai_BuildSystem_RecordPushed` hook is gone.
- A Grand Prix's weekend is its track (the live state's track name), so a Grand Prix weekend that was under way when you updated starts again once.
- **The compact strip and the Form Guide reminder are in the game's HUD editor** (the launcher's HUD slots, 1.10.21): move, resize or switch them off there. Both only show while the game shows its race HUD.
- In the wet the grip lever does nothing, so a form shows at about half its pace there. The levers' size is still to be calibrated in the game.
- Needs Swofty Launcher 1.10.21 or later. SDK headers updated to the launcher 1.10.35.

From players' reports:
- **Driver Form on too no longer stops Play** with a message that read like a crash. Driver Momentum leaves the AI to Driver Form, keeps its forms and the Form Guide, and a notification at each session says to turn one off.
- **The compact strip:** outside a race its "OUT OF FORM" ran into the driver's name; it reads HOT and COLD now, and a role never leaves its column.

## 0.1.1

Safer on game updates (F1 25 1.0.145, 16 Nov):
- **The AI system is checked before anything is written.** The addon finds the game's AI manager as Driver Form does, then checks it against the AI system the game itself builds, caught at a launcher binding (`site_Ai_BuildSystem_RecordPushed`). If the two don't agree, as after a game update that moves the game's code, every car is left alone and the log says why, instead of writing into the wrong place.
- **The values are checked too:** before a weekend form goes in, every car's base lap time, driving characteristics and difficulty must be in range and the field's lap times close to each other, with one record per car. Otherwise the addon leaves every car alone and says so in the log (`layout check failed: ...`).

## 0.1.0
First version of Driver Momentum, built on msyeett's Driver Form 0.1.5.

- **Momentum** in Driver Career and My Team: a driver's weekend form follows their streak, which moves after every
  race with their results against their usual finish and their teammate. A retirement leaves it as it is, a sprint
  counts half, and it fades without results to back it. New setting **Momentum**: Off (Driver Form's roll), Light,
  Normal, Strong.
- **Regularity**: veterans' form swings less than rookies', from the game's experience and awareness ratings.
- Each career keeps its own book (streaks and the last five results), which follows a new career to its own save id.
  A career weekend is told by its season and round, not by the track's lap time and a day.
- **Form Guide** (F6, or the **Form Guide key** setting): a drawer on the right edge that reads the grid like a thermal
  camera, from ice to magma. Every driver's heat index (-100 to +100) on a thermometer and the time a lap it is worth;
  the grid's heat as an equaliser with the hottest and coldest driver; in a career each driver's trend over the last
  five races as a line and those results as heat-coloured cells; a word for each driver (HOT STREAK, SLUMP, FLYING,
  STRUGGLING, STEADY, ERRATIC) and, during a race, who is on a charge, losing time or making a mistake right now. It
  slides out and a scan line reveals the grid. **Form Guide size** setting: Small, Normal, Large.
- **Form Guide reminder**: at the first session of each weekend a tab slides out of the screen's edge with the key
  for a few seconds (setting, on by default).
- The in form / out of form toasts also name the drivers on a hot streak or in a slump.
- **driver_momentum.form** service for other addons.
- Marked as a conflict with Driver Form, which writes the same values.
- Built with MSVC or zig on the current Swofty SDK; host tests for the engine and the career book.

Fixed after the first race in the game (13 Nov): checking who drives each car took about 10 ms in one frame every
10 s (inherited from Driver Form). The whole grid is now read in one go only when it is built or a car is handed
over; the rest of the time one car a tick, and the log says the checks' worst times at the end of each session.

Fixed before the first release (an audit of 13 Nov):
- **Your car is never touched.** A car the session said was yours could get a form on the first tick when its control
  couldn't be read. A car now gets a form only once the game says the computer drives it, and when the addon stops it
  gives back only what it changed (a car it never wrote to, yours above all, is left as it is).
- Only Driver Career, My Team and Grand Prix get forms; split screen and the other modes are left alone, and a race
  start no longer takes a Time Trial for a race.
- **Carlos Sainz gets a form and a streak** (his game id is 0, which was taken for "unknown"). A car whose identity
  can't be read, or two cars with one id, get none, and a race with too few drivers identified isn't counted.
- **The book is never lost.** A career's book names its career and ends with a checksum, a damaged or foreign file is
  kept aside instead of used, a book from a newer version is never overwritten, two career ids no longer share a file,
  and a new career's book moves to its own id only once the new copy reads back (it's tried again otherwise).
- **A sprint and its race survive a reload.** The weekend's first race now waits in the book, so closing the game
  between the sprint and the race no longer loses the race. A race closed twice counts once, a restarted session
  counts nothing, and a race run again replaces the first.
- **Only a settled result counts:** no two cars on one position and the field at the flag. Disqualified and not
  classified are told apart from a retirement, and a lapped car is no retirement. The live state needs the
  `telemetry` experimental feature, now in the manifest.
- **Form Guide:** FORM (this weekend), STREAK (the career) and AI TARGET (the target lap time the AI really gets) are
  named apart, and the extremes say which they measure. A Momentum setting changed mid-weekend shows as "next
  weekend". The text stays in the safe area, a short screen drops the equaliser and the thermometers, a grid that
  doesn't fit pages with Page Up/Down with your car kept at the bottom, and the history shows DQ and NC. New compact
  view on track and a Reduced motion setting. The reminder belongs to each career's weekend.
- The service checks the stat id's range and answers only for the loaded career or a running Grand Prix.

## Driver Form history (msyeett)

### 0.1.5
- Fixed a small stutter every half second while driving: who drives each car is now checked every 10 seconds
  instead of every frame.
- Time Trial is left alone: no form for your ghosts or rival times.
- Fixed your own car getting a form for a few seconds at the start of a session.

### 0.1.4
- Faster start of a session: the grid is read with far fewer memory checks, and the slow table of driver initials
  is only read when the game has no name for a driver. Shorter pause on the loading screen.
- Shows as tested on Steam in the launcher's Addon Browser, in the AI category. Needs launcher 1.8.36 or newer.

### 0.1.3
- Fixed a burst of toasts after unpausing: mistakes, bad patches and hot spells now wait while the game is paused.
  Needs launcher 1.8.33 or newer.

### 0.1.2
- Fixed heavy lag when the addon can't read the grid: it kept retrying twice a second. It now backs off after a few tries.
- Fixed small frame spikes every 10 seconds.
- The driver names are read once per game run, so later sessions start faster.

### 0.1.1
- The source code is now public, under the MIT licence: https://github.com/constructor-mods/driver-form
- No changes in game.

### 0.1.0
- First release.
