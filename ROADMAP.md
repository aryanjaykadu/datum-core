# Datum Core roadmap

## Who it is for

People who design functional parts to be 3D printed: makers with a Bambu, Prusa or Creality
printer, engineering students, product designers and small companies doing additive
manufacturing. There are millions of printers in use and no affordable, easy tool for making
parts that are both **strong and light** and **guaranteed to print**.

Today that work means nTopology (enterprise pricing), hand-tuning infill in a slicer, or
workarounds in Blender and Fusion. Datum Core's geometry is an exact field, so the things that
are hard elsewhere (lattices, blends, shells, watertight output) never fail here.

## The one workflow to be best at

1. **Bring a part in**: STEP/STL import, or a smart template (bracket, enclosure, mount, gear).
2. **Say how it is used**: click where it is bolted down and where the load goes.
3. **See the stress**: a finite-element analysis on the part colours it by stress and gives a safety factor.
4. **Make it lighter**: one click replaces the inside with a lattice that is dense where stress is high and sparse elsewhere, to a target weight.
5. **Check it prints**: walls, overhangs, print time, grams.
6. **Send to the slicer**: 3MF with the right settings for the user's printer.

Everything else (robotics, aerospace generators, calculators, quotes) stays available but out
of the way of this path.

## Milestones

| Version | Goal | Status |
|---|---|---|
| 1.7 | Stress study: voxel FEA, click to place fixtures and loads, stress colours, safety factor | done |
| 1.8 | "Make it lighter": stress-graded lattice to a target weight, before/after mass and strength | done |
| 1.9 | App organised around the workflow: guided steps, print-first start hub, printer profiles in Send to slicer | done |
| 2.0 | Ready for real users: auto-update, first-run tour, in-app feedback, website | done |
| 2.1 | Real users: 10 printer owners try it; fix what they trip over; code-signed installer | next |

## How we judge it

- A first-time user goes from an imported bracket to a lighter, stress-checked, sliced part in under 5 minutes.
- Stress results within 15 % of hand calculations on standard beams (tested).
- 60 fps viewport on integrated graphics (done in 1.4).
