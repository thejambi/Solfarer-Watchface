# Solfarer

A Pebble watchface that wanders the real stars. Every morning your ship
starts at Sol; every bell (hourly or half-hourly), it hops to a nearby
star — real ones, from the HYG catalog: Proxima Centauri, Barnard's Star,
Wolf 359, Tureis, Guniibuu, and 2,200 more systems of the 100-light-year
Local Bubble, drawn in their true spectral colors and sized by magnitude.

The map is a traveling 40-ly viewport (±12 ly depth slab) that rides the
wander, framed off-center by a seeded per-star jitter so motion is felt.
The gold route sweeps to a different neighbor every minute; a wrist flick
browses the rest of the hour's board (display only — the bell flies its own
deterministic pick). Each hop plays as a twin-paradox cutscene with honest
1g relativity, then an arrival card: name, nature, constellation spelled
out, and the day's tally. A gold signpost rides the frame edge pointing
home whenever Sol is out of view; labels dodge the route and each other.

It's a full watchface besides: steps (large, comma'd), distance, and heart
rate in a corner grid facing weekday/date/temperature across a hairline
rule; a minute-by-minute activity sparkline of the past hour. Weather is
Open-Meteo (manual location supported). The pure star chart is one toggle
away.

Sleep borrows the step slot, the way ActiveHour does it: until you've passed
the **wake threshold** (a settings slider, default 500 steps today), that
first row reads `7h 42m` rather than a step count — no label, since the
shape can only mean one thing. Once you're moving, steps take the row back
for the day. Same font and baseline, so nothing jumps when it flips; with no
sleep recorded, steps simply keep the row. That's all of sleep here, on
purpose: it shows up when you care and then gets out of the way. The Pebble
Health app is the place for a deeper dig.

## The wander

Seeded by the date — every wearer walks the same path. Boards of up to 9
neighbors per bell window are sampled statelessly (seed, day, window, star)
with a mild outward bias, so missed hours replay identically and instantly
at boot; flown slots are a high-water mark that clock jumps can never
re-arm. The day anchors at a configurable start hour (default 7am), folds
into records (farthest ever, streak) at rollover, and greets you back at
Sol with a summary card.

## Building

```
pebble build
pebble install --emulator emery
```

- `tools/make_catalog.py` downloads HYG v4.1 (34 MB, gitignored), curates
  the bubble (dedupes multi-star systems, verifies 20-ly walk connectivity),
  and emits `resources/data/stars.bin` + `src/c/catalog_gen.h`.
- `tools/make_store_art.py` renders the store banner, icons, and menu icon.
- Dev switches: `DEV_FAST_BELLS` (scheduler.c) hops every minute with no
  backlog; `DEV_FAKE_HEALTH` (face.c) feeds synthetic health data — the
  emulator has none.
- Known font quirk: GOTHIC_14_BOLD's digit '4' has a broken advance that
  fuses it into its left neighbor; the step count uses GOTHIC_18_BOLD
  partly for this reason. Panel names with 4s carry a subtle 1px fusion.

Sibling of the [Lighthaul Watchface](https://github.com/thejambi/Lighthaul-Watchface)
— same bones (bell scheduler, single-window scene machine, Clay settings,
health sparkline, weather), pointed at the real galaxy.
