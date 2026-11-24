# Driver Momentum

AI drivers have a form for the weekend, and in a career that form follows their season. A driver who keeps beating
his usual results and his teammate goes on a hot streak; one who keeps falling short slips into a slump. Veterans are
steadier from weekend to weekend, rookies swing more. Press **F6** in a session to see it all in the **Form Guide**.

Driver Momentum is built on **[Driver Form](https://github.com/constructor-mods/driver-form) by msyeett** (MIT). Everything
Driver Form does is still here; Driver Momentum adds the memory, the Form Guide and a service for other addons.

## What it does

### The weekend form (from Driver Form)
- Every AI driver gets a form for the weekend, from badly out of form (-1) to flying (+1). It lasts through practice,
  qualifying and the race, even if you close the game in between.
- In-form drivers are quicker and defend from further back.
- Out-of-form drivers are slower, take a wider and less tidy line, attack less and give way sooner.
- All of it through the launcher's official live car levers (grip, engine power and the AI's racecraft), which the
  launcher keeps and gives back by itself: nothing is written into the game's memory.
- Bad patches, hot spells and sudden mistakes during the race.
- Your own car is never touched, and Time Trial is left alone.

### Momentum (new)
In **Driver Career** and **My Team**, each driver's form is three things added up:
- **Their streak.** After every race it moves up when they finish better than they usually do and beat their teammate
  by more than they usually do, and down when they don't. Both measures are the driver's own, so a slow car or a faster
  teammate is no slump by itself. A retirement says nothing about the driver and leaves the streak as it is. A sprint
  counts half. The streak fades by itself, so it needs results to last.
- **Their regularity**, from the game's experience and awareness ratings: a veteran's form swings less, a rookie's
  more. Drivers added by other addons without ratings count as average.
- **The dice**: Driver Form's roll, a third of the grid in form, a third out, the rest neutral.

How much the streak counts is the **Momentum** setting: Off (a fresh roll every weekend, as in Driver Form), Light,
Normal or Strong. In a **Grand Prix** there is no season to remember, so the form is Driver Form's roll stretched or
shrunk by each driver's regularity.

Each career keeps its own book: the streaks of every driver and their last five results. A new career's book follows
it when the game gives it its own save id.

### The Form Guide (new)
A drawer docked to the right edge of the screen, opened and closed with **F6** (or another key in the settings) in any
session. It reads the grid like a thermal camera, from ice (cold) to magma (hot):
- Every driver has a **heat index** for the weekend, from -100 to +100, on a **thermometer**, with the time a lap it is
  worth.
- On top, the whole grid's heat as an equaliser, and the **hottest** and the **coldest** driver (in a career, the
  hottest and coldest run, with their last results).
- In a career, each driver's **trend**: their last five results drawn as a line, the newest point in the colour of
  their streak, and the five results as cells coloured by how hot they were (white-hot for a win, R for a
  retirement).
- A word when there is something to say: **HOT STREAK**, **SLUMP**, **FLYING**, **STRUGGLING**, **STEADY** or
  **ERRATIC**. During a race, a driver in a hot spell, a bad patch or a mistake shows it live (**ON A CHARGE**,
  **LOSING TIME**, **MISTAKE**).

The drawer slides out and a scan line reveals the grid. It does not take the mouse or the keys from the game, so you
can drive with it open. Your car is shown last, marked YOU and NOT CHANGED. At the first session of each weekend a tab
slides out of the screen's edge for a few seconds with the key.

## Settings (ADDON SETTINGS)
| Setting | Does |
|---|---|
| Form strength | Off, Subtle (about 0.8 s a lap either way), Normal (1.5 s), Wild (2.2 s), Chaos (3.5 s) |
| Momentum | How much the streak counts in a career: Off, Light, Normal (default), Strong |
| Form Guide key | F6 (default), F5, F9, F10, F11 or None |
| Form Guide size | Small, Normal (default), Large |
| Form Guide reminder | The card that names the key at the first session of each weekend. On by default |
| Compact view on track | While the Form Guide is closed, a small strip with three drivers: in a race the cars ahead and behind you and your teammate, otherwise who is most and least in form. On by default. Move, resize or hide it in the game's HUD editor |
| Form Guide motion | Full (default) or Reduced: no slide, scan line or flashing; live moments stay as fixed marks |
| In form / Out of form toasts | After the first lap, name who is in and out of form, and in a career who is on a hot streak or in a slump. Off by default |
| Bad patches and mistakes | On or off |
| Messages: Practice, Qualifying, Race | How often notifications may show: None to Ultra. Off by default |
| Show mistakes, struggling, on a charge, place changes, pit stops | Which notifications you want |

## Works with
- **Silly Season**: yes. They don't share anything; Silly Season's panel is on F7.
- **Driver Form** (msyeett): turn one off. With both on, Driver Momentum leaves the AI to Driver Form (it keeps only
  its forms and the Form Guide) and says so on screen at each session.
- **Team Orders**, **AI Driver Behaviour** and other addons that change how the AI drives: they work, but both change
  the same AI, so the effects add up (for example your teammate's pace with Push Now).
- Drivers added by other addons get a form and a streak like everyone else.
- Checked on 24 Nov with Swofty Launcher 1.10.35, Silly Season 1.1.51 and Driver Form 0.2.1.

## For other addons: the driver_momentum.form service
Copy [`native/include/driver_momentum_api/form.hpp`](native/include/driver_momentum_api/form.hpp) (MIT) and look the
service up:

```cpp
swofty::services::when<DriverMomentumForm>([](const DriverMomentumForm &f) { g_form = &f; });
float form = 0, streak = 0;
if (g_form && g_form->form(driver_stat, &form, &streak) == 0) { /* ... */ }
```

`form` gives a driver's form this weekend and their streak in the loaded career, by entrants.drivers stat id; `races`
the races the career's book has counted.

## Building
- **MSVC** (Visual Studio 2022 or later, "Desktop development with C++"), from PowerShell:
  - `native\build.bat` builds `native\driver_momentum.dll`;
  - `native\build.bat test` builds it and runs `model_test` (the form and streak engine over whole careers) and
    `career_test` (each career's book, races, sprints and the Grand Prix weekend against a fake core);
  - `native\build.bat preview <dir>` draws the Form Guide into SVG files.
- **zig**, as Driver Form did: `native/build.sh`.
- `package.bat` builds the DLL and makes the `.swoftyaddon` in `release\`.

The engine is `native/src/momentum.cpp`: plain C++, no game calls. The rest of `native/src` is Driver Form's plugin
with the career book (`form_roll.hpp`), the race results (`results.hpp`), the ratings (`ratings.hpp`), the panel
(`form_guide.hpp`, `ui_kit.hpp`) and the service (in `driver_form.cpp`).

## Credits
- **Driver Form**: msyeett ([constructor-mods/driver-form](https://github.com/constructor-mods/driver-form), MIT).
- **Driver Momentum**: Alex Sánchez.
- **Swofty Launcher** and its SDK: Swofty (MIT).

Unofficial fan addon, not affiliated with or endorsed by EA, Codemasters or Formula 1.
