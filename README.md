# Solfarer

A Pebble watchface that wanders the real stars. You start each day at Sol
and, at every bell (hourly or half-hourly), your ship hops to a nearby
star — real stars, from the HYG catalog: Proxima, Barnard's Star, Wolf 359,
and 2,200 more systems of the 100-light-year Local Bubble, colored by their
true spectral class. The flight plays as a twin-paradox cutscene: universe
years pass while you age months. A gold signpost on the frame edge always
points home to Sol.

Sibling of the [Lighthaul Watchface](https://github.com/thejambi/Lighthaul-Watchface),
sharing its bones: the bell scheduler with silent catch-up, the single-window
scene machine, Clay settings, the health sparkline, and the weather corner.
The economy is gone — the wander is the point.

Status: scaffold. The catalog pipeline lives in `tools/make_catalog.py`
(downloads the HYG database CSV, curates the bubble, verifies walk
connectivity, and will emit the packed star resource).
