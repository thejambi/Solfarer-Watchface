# Solfarer design notes (from the founding brainstorm, 2026-07-24)

## The wander
- Start each day at Sol; the start hour is a setting (e.g. 7am), so the
  "day" anchors there instead of midnight — the scheduler is wall-clock
  based, so this is just an offset on day rollover.
- Each bell (hourly / half-hourly — both supported): hop to a neighbor
  within hop range (~20 ly; 100% of the bubble is reachable from Sol at
  that radius — verified in tools/make_catalog.py).
- Minute rotation previews neighbors like Lighthaul's offer board; the
  bell takes whatever the rotation shows. Walk is seeded by the daily
  seed → everyone wanders the same path; catch-up is a pure recompute.
- Slight outward bias, not forced — glimpsing Sol again mid-day is a
  feature, not a bug.

## The map
- Fixed-scale traveling viewport (~40 ly) — the view moves with you.
- Current star OFF-center: seeded per-(star, day) jitter so even a
  revisit frames differently. Never dead-center; motion must be felt.
- Z-slab: draw only stars within ±12–15 ly of current depth (2,234 stars
  would carpet the screen otherwise; the slab makes depth changes vivid).
- Stars colored by real spectral class (O/B blue → G yellow → M red,
  D pale blue), sized by magnitude. B&W watches: size only.
- Sol: gold ring when in frame; when out of frame, a gold signpost on the
  frame edge pointing home, labeled "SOL 87ly" (Lighthaul's deep-space
  signpost, repurposed).

## Modes (replacing game/chart)
- "Star info" mode: vitals = `@Wolf 359  7.8 ly out`; bottom panel = next
  hop preview (`> Ross 128  6.2 ly  M4`); tap overlay = current star card
  (name, distance, spectral class, magnitude, constellation — fields are
  baked into the catalog now, richer display can come later).
- "Health stats" mode: Lighthaul's chart mode as-is (sparkline, sleep
  before 10am, steps/km, health tap card).
- Keep: flight cutscene with REAL twin-paradox numbers for the hop
  ("universe 11.9 yr, ship 0.5 yr" to Procyon), weather corner, records
  (farthest from Sol ever, streak), day summary ("wandered 214 ly in 24
  hops, ended at Gliese 581").
- Strip: the entire economy — credits, fuel, contracts, upgrades, tow.

## The catalog (tools/make_catalog.py)
- HYG v4.1 → galactic XYZ → dedupe multi-star systems by base id +
  0.15 ly proximity → 100 ly bubble, dim anonymous stars (absmag > 8)
  culled beyond 50 ly → 2,234 systems, ~45 KB packed.
- 558 systems with proper/Bayer names; name preference: proper > Bayer >
  Gliese > HIP/HD.
- Ship as a resource blob: ~12 B/star numeric table (heap-resident,
  ~27 KB) + names read on demand via resource_load_byte_range.
- Naming runner-ups for posterity: Starhaul, Milky Wander, Local Bubble.
