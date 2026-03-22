# Math

> 🌐 [中文](MATH.zh.md)

The math layer provides the reusable evaluators used by parsers, compilation, kinematics, and rendering.

## Easing

`math/easing.hpp` contains the easing table used by RPE-oriented event evaluation.

Use it when:

- parsing RPE easing-indexed events
- sampling tracks during chart compilation
- evaluating piecewise eased curves at runtime

## Piecewise Tracks

`math/tracks.hpp` is the core time-domain evaluation layer.

Important concepts:

- constant and linear segments
- eased segments for RPE-style transitions
- `IntegralTrack` for accumulated scroll distance
- seek/binary-search based evaluation for piecewise curves

This is the layer that turns chart event lists into callable time functions.

## Utility Math

`math/util.hpp` holds low-level helpers shared everywhere:

- clamp and numeric helpers
- lightweight RGB/color structs
- small reusable math utilities used by core, engine, and render

## Beat/Time and Sampling Helpers

Supporting structures in `chart/` also belong to the math story:

- `bpm_map.hpp`: beat-to-seconds conversion over BPM segments
- `sampled_track.hpp`: uniformly sampled float-array evaluator
- `compiled_chart.hpp`: sampled-track storage and rehydration into runtime lambdas

## Runtime Relationship

The flow is:

```text
source events
  -> piecewise tracks / BPM mapping
  -> optional compile-time sampling
  -> runtime TrackFn evaluation
  -> kinematics and render snapshot generation
```

## Cross-References

- Movement and coordinate rules: [KINEMATICS.md](KINEMATICS.md)
- Format-specific event sources: [FORMAT.md](FORMAT.md)
- Core structs carrying these evaluators: [DATA_STRUCTURES.md](DATA_STRUCTURES.md)
