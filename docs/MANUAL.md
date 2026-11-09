# Skell Delay – manual

## Quick start (one minute)

1. Put the plugin on a vocal or instrument track as an insert, or on a send.
2. Click the preset name in the middle (or `<` `>`) and pick something. The browser plays each preset as you select it.
3. Set **TIME**, **FEEDBACK** and **WET** until it sits right. **DUCKER** lowers the echoes while you are singing so the words stay clear.
4. Click **ADV** to open more controls. It cycles through four pages and then closes.
5. Hover any control for a description and its current value.

Handy: double-click a knob to reset it. Hold **Shift** while dragging for fine control. Right-click (or press Enter on a focused knob) to type an exact value such as `250`, `1.5k`, `35%` or `1/8`. **Ctrl/Cmd+Z** undoes, **Ctrl/Cmd+Shift+Z** redoes.

## Front panel

| Control | What it does |
|---|---|
| TIME | Delay time. Free time is 0.01 ms to 2.5 s; with SYNC it is a note value. |
| FEEDBACK | How much of each echo is fed back in. |
| TONE | Low-pass on the echoes (turn left for darker echoes). |
| TAPE | Wobble, saturation, roll-off and a little hiss that only appears while echoes play. |
| PAN | Where the echoes sit in the stereo field. |
| DUCKER | Lowers the echoes while the dry signal is playing. |
| DRY / WET | Level of the original sound / of the echoes. |
| FREE / SYNC | Free milliseconds or locked to the host tempo. |
| PING | Echoes bounce left and right. |
| REV | Echoes play backwards. |
| +12 (shimmer) | Click to cycle pitch-shifted repeats: +12, +7, -12, -7, off. |
| Feel | Straight, dotted or triplet (when synced). |
| BBD | Analogue bucket-brigade chip character. |
| KILL | Clears all echoes at once. |
| BYP | Stops new sound entering; echoes ring out. |
| APAN | Auto-pan speed (click to cycle). |
| A / B | Two complete settings to compare. The button shows which one you are on. |
| Display | Shows the echoes. Click for the next gate pattern, scroll for gate depth. Top right: how long the tail takes to fall 60 dB. |

## ADV pages

**ADV 1 – Sound:** LO CUT, ATTACK, SUSTAIN, SWIRL (phaser), SAUCE (reverb), WIDER, RICHER (chorus), SHIMMER amount, SMOOTH (how gently time changes glide), DUCK REL, GATE depth, PAN depth, MOD RATE, FB MODE (normal / inverted / ping pong), REPEATS (stop after exactly N echoes), and the toggles CHEAP (11-bit vintage digital), MONO, DESTINY (drifting time).

**ADV 2 – Smart features** (these listen to the performance; the note under each says what it is doing, and says so when it cannot help):

- **ELASTIC** – the delay time follows how fast the performer is going. Fast rap gets tighter repeats, slow passages get wider ones, limited to 0.5x–2x.
- **TELEPATHY** – learns the rhythm of the last phrase and swells a faint ghost of the previous word just before the next one is expected. It needs two or three phrases to learn. It stays quiet when the rhythm is too loose.
- **ECOLOGY** – every repeat lives its own life: it ages, darkens, drifts and may die; quiet rooms let echoes bloom, busy ones eat them.
- **NUDGE / POCKET** – POCKET shows how early or late each word lands against the 1/16 grid (the host must be playing, with a tempo). NUDGE pulls words toward the grid by up to 30 ms. NUDGE above zero adds 40 ms of latency (the plugin reports it to the host, so the track stays in time).

**ADV 3 – Play:** GRAIN (echoes scattered into grains), SPECTRAL (lows later, highs earlier), STUTTER and TAPE STOP (hold the button), REV THROW (click for one backwards swell), TAP (tap the beat; sets the delay time), LEARN (sets the delay time from how fast you are playing right now).

**ADV 4 – View:** the echo trail (one bar per repeat: height = loudness, colour = brightness; an x marks a repeat that died), HIGH CONTRAST, LARGE TEXT, UNDO / REDO and RESET SIZE.

## Presets

Click the preset name to open the browser: type to search names and tags, use the chips to filter, click the star to favourite, and use the buttons to save your own (type `Folder/Name` to file it in a folder) or delete a saved one. RANDOMISE draws new settings from safe ranges (feedback never runs away); the slider sets how far from the current sound it goes, and KEEP TIME / KEEP MIX hold those two.

User presets are saved in `Skell-Delay/Presets` inside your user application-data folder. Favourites are in `Skell-Delay/favourites.txt` next to it.

## Window size and accessibility

Drag the bottom-right corner to resize from 75% to 200%. The size is remembered with the project. LARGE TEXT and HIGH CONTRAST (ADV 4) are saved the same way. Every control has a spoken name and description for screen readers, and knobs can be tabbed to, stepped with the arrow keys, and reset with Delete.

## Troubleshooting

- **Pocket says "needs the host playing, with a tempo".** The plugin only reads word timing against the beat while the host transport is running.
- **Telepathy says "rhythm too loose".** It cannot predict what is not regular. Try a steadier phrase, or lower the amount.
- **Echoes are suddenly quiet.** DUCKER may be up; it lowers the echoes while you play. Also check REPEATS (it stops after N echoes) and the gate.
- **A hiss under the echoes.** That is TAPE; it only appears while echoes play. Turn TAPE down.
- **The track sounds late.** NUDGE above zero adds 40 ms of latency; the host should compensate. Set NUDGE to zero if your host does not.
- **Settings did not come back.** Please send us the host name and version, and the project if you can.
