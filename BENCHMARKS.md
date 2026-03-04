# Phigros Renderer — Benchmark Report

## System

| Key          | Value                           |
| ------------ | ------------------------------- |
| CPU          | AMD EPYC 7702 64-Core Processor |
| Cores        | 4                               |
| OS           | Linux                           |
| Build        | Release (-O3)                   |
| C++ standard | C++17                           |

## 1. Chart Inventory

| Chart                                  | Format   | File Size | Lines | Notes | Holds | Fakes | Duration | Notes+Lines Heap |
| -------------------------------------- | -------- | --------- | ----- | ----- | ----- | ----- | -------- | ---------------- |
| ATHAZA.LeaF/AT.json                    | official | 3.9 MB    | 19    | 1344  | 126   | 0     | 145s     | 233.8 KB         |
| ATHAZA.LeaF/IN.json                    | official | 9.8 MB    | 24    | 1137  | 171   | 0     | 158s     | 201.2 KB         |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | official | 12.8 MB   | 24    | 2025  | 85    | 0     | 187s     | 350.4 KB         |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | official | 14.7 MB   | 24    | 1600  | 163   | 0     | 187s     | 279.0 KB         |
| Aleph0.LeaF/IN.json                    | official | 1.3 MB    | 20    | 885   | 39    | 0     | 132s     | 157.2 KB         |
| BetterGraphicAnimation.ルゼ/IN.json  | official | 1.5 MB    | 11    | 616   | 34    | 0     | 99s      | 108.2 KB         |
| Radiance.Nhato/AT.json                 | official | 6.2 MB    | 24    | 926   | 139   | 0     | 142s     | 165.7 KB         |
| Radiance.Nhato/IN.json                 | official | 4.8 MB    | 24    | 667   | 118   | 0     | 142s     | 122.2 KB         |
| Rrharil.TeamGrimoire/AT.json           | official | 3.2 MB    | 24    | 1300  | 123   | 0     | 130s     | 228.6 KB         |
| Rrharil.TeamGrimoire/IN.json           | official | 3.4 MB    | 18    | 1300  | 53    | 0     | 130s     | 226.0 KB         |

## 2. Parser Throughput

Each chart parsed 8 times (3 warmup); mean/min/max reported.

| Chart                                  | Format   | File Size | Min Parse | Mean Parse | Max Parse | Stddev      | Throughput |
| -------------------------------------- | -------- | --------- | --------- | ---------- | --------- | ----------- | ---------- |
| ATHAZA.LeaF/AT.json                    | official | 3.9 MB    | 301.04 ms | 324.97 ms  | 363.54 ms | 20.54 ms σ | 12.1 MB/s  |
| ATHAZA.LeaF/IN.json                    | official | 9.8 MB    | 783.71 ms | 796.24 ms  | 832.05 ms | 14.84 ms σ | 12.3 MB/s  |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | official | 12.8 MB   | 1.052 s   | 1.085 s    | 1.143 s   | 30.09 ms σ | 11.8 MB/s  |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | official | 14.7 MB   | 1.245 s   | 1.319 s    | 1.422 s   | 51.76 ms σ | 11.1 MB/s  |
| Aleph0.LeaF/IN.json                    | official | 1.3 MB    | 109.30 ms | 115.25 ms  | 124.55 ms | 4.61 ms σ  | 11.6 MB/s  |
| BetterGraphicAnimation.ルゼ/IN.json  | official | 1.5 MB    | 118.67 ms | 122.93 ms  | 126.23 ms | 2.28 ms σ  | 12.0 MB/s  |
| Radiance.Nhato/AT.json                 | official | 6.2 MB    | 499.41 ms | 538.76 ms  | 571.68 ms | 24.29 ms σ | 11.6 MB/s  |
| Radiance.Nhato/IN.json                 | official | 4.8 MB    | 429.21 ms | 450.75 ms  | 475.36 ms | 14.29 ms σ | 10.7 MB/s  |
| Rrharil.TeamGrimoire/AT.json           | official | 3.2 MB    | 264.41 ms | 276.42 ms  | 288.79 ms | 7.77 ms σ  | 11.7 MB/s  |
| Rrharil.TeamGrimoire/IN.json           | official | 3.4 MB    | 262.15 ms | 291.42 ms  | 324.27 ms | 20.67 ms σ | 11.6 MB/s  |

## 3. Engine Simulation (Full Loop)

Single simulation pass at 240 Hz (1/240 s step). Includes SimulatePlay, miss detection, hold maintenance, hold finalization.

| Chart                                  | Notes | Frames (240Hz) | Duration | Sim Time | μs/note    | Realtime × |
| -------------------------------------- | ----- | -------------- | -------- | -------- | ----------- | ----------- |
| ATHAZA.LeaF/AT.json                    | 1344  | 35356          | 147s     | 262.7 ms | 195.453 μs | 561×       |
| ATHAZA.LeaF/IN.json                    | 1137  | 38502          | 160s     | 264.4 ms | 232.517 μs | 607×       |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 2025  | 45481          | 189s     | 393.0 ms | 194.086 μs | 482×       |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 1600  | 45537          | 189s     | 431.2 ms | 269.511 μs | 440×       |
| Aleph0.LeaF/IN.json                    | 885   | 32161          | 134s     | 187.6 ms | 211.976 μs | 714×       |
| BetterGraphicAnimation.ルゼ/IN.json  | 616   | 24361          | 101s     | 110.5 ms | 179.377 μs | 919×       |
| Radiance.Nhato/AT.json                 | 926   | 34669          | 144s     | 246.9 ms | 266.675 μs | 585×       |
| Radiance.Nhato/IN.json                 | 667   | 34669          | 144s     | 186.6 ms | 279.806 μs | 774×       |
| Rrharil.TeamGrimoire/AT.json           | 1300  | 31706          | 132s     | 245.4 ms | 188.805 μs | 538×       |
| Rrharil.TeamGrimoire/IN.json           | 1300  | 31706          | 132s     | 238.4 ms | 183.407 μs | 554×       |

### Simulation Step-Rate Comparison

Same chart at different simulation step sizes:

| Step Rate | Frames | Sim Time | Realtime × |
| --------- | ------ | -------- | ----------- |
| 60 Hz     | 7927   | 57.2 ms  | 2308×      |
| 120 Hz    | 15853  | 129.0 ms | 1024×      |
| 240 Hz    | 31706  | 219.3 ms | 602×       |
| 480 Hz    | 63411  | 429.3 ms | 308×       |

## 4. Render Pipeline — build_frame

Pure CPU cost of assembling one render frame snapshot (no GPU calls). Measured across all frames at 240 Hz simulation; p95 column shows tail latency. Frame budget @ 60 fps = 16,667 μs; @ 240 fps = 4,167 μs.

| Chart                                  | L/N       | Frames | Min      | Mean     | Max        | p95       | Realtime × | Budget %              |
| -------------------------------------- | --------- | ------ | -------- | -------- | ---------- | --------- | ----------- | --------------------- |
| ATHAZA.LeaF/AT.json                    | 19L/1344N | 35356  | 2.81 μs | 6.46 μs | 223.75 μs | 10.44 μs | 645×       | 0.2% of 240fps budget |
| ATHAZA.LeaF/IN.json                    | 24L/1137N | 38502  | 2.79 μs | 6.42 μs | 170.78 μs | 10.31 μs | 649×       | 0.2% of 240fps budget |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 24L/2025N | 45481  | 2.56 μs | 8.07 μs | 204.32 μs | 13.32 μs | 517×       | 0.2% of 240fps budget |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 24L/1600N | 45537  | 2.87 μs | 6.94 μs | 260.19 μs | 11.40 μs | 600×       | 0.2% of 240fps budget |
| Aleph0.LeaF/IN.json                    | 20L/885N  | 32161  | 2.32 μs | 5.08 μs | 93.09 μs  | 8.07 μs  | 821×       | 0.1% of 240fps budget |
| BetterGraphicAnimation.ルゼ/IN.json  | 11L/616N  | 24361  | 1.54 μs | 4.22 μs | 260.81 μs | 6.96 μs  | 988×       | 0.1% of 240fps budget |
| Radiance.Nhato/AT.json                 | 24L/926N  | 34669  | 3.09 μs | 6.40 μs | 113.33 μs | 9.81 μs  | 651×       | 0.2% of 240fps budget |
| Radiance.Nhato/IN.json                 | 24L/667N  | 34669  | 2.87 μs | 5.33 μs | 131.91 μs | 7.76 μs  | 782×       | 0.1% of 240fps budget |
| Rrharil.TeamGrimoire/AT.json           | 24L/1300N | 31706  | 2.53 μs | 7.44 μs | 301.00 μs | 11.20 μs | 560×       | 0.2% of 240fps budget |
| Rrharil.TeamGrimoire/IN.json           | 18L/1300N | 31706  | 2.85 μs | 7.07 μs | 151.84 μs | 10.73 μs | 589×       | 0.2% of 240fps budget |

## 5. Kinematics — eval_line_state

Cost of evaluating all track functions for one judge-line at one time step. Benchmarked as a tight loop of 2,000 × lines iterations.

| Chart                                  | Lines | ns/call  | Calls/sec |
| -------------------------------------- | ----- | -------- | --------- |
| ATHAZA.LeaF/AT.json                    | 19    | 87.0 ns  | 11.5M/s   |
| ATHAZA.LeaF/IN.json                    | 24    | 86.3 ns  | 11.6M/s   |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 24    | 71.0 ns  | 14.1M/s   |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 24    | 88.8 ns  | 11.3M/s   |
| Aleph0.LeaF/IN.json                    | 20    | 62.7 ns  | 15.9M/s   |
| BetterGraphicAnimation.ルゼ/IN.json  | 11    | 54.7 ns  | 18.3M/s   |
| Radiance.Nhato/AT.json                 | 24    | 74.2 ns  | 13.5M/s   |
| Radiance.Nhato/IN.json                 | 24    | 64.8 ns  | 15.4M/s   |
| Rrharil.TeamGrimoire/AT.json           | 24    | 56.5 ns  | 17.7M/s   |
| Rrharil.TeamGrimoire/IN.json           | 18    | 100.1 ns | 10.0M/s   |

## 6. Mod Application

Applied to the largest available chart (includes copying ChartData). 12 warmup + measurement iterations.

> Reference chart: AbsoluTedisoRdeR.AcuteDisarray/IN.json

| Mod               | Op Type     | Notes | Mean Time | Throughput       |
| ----------------- | ----------- | ----- | --------- | ---------------- |
| Mirror            | mirror      | 1600  | 41.9 μs  | 38151102 notes/s |
| Mirror+flip       | mirror      | 1600  | 30.6 μs  | 52285445 notes/s |
| Colorize constant | colorize    | 1600  | 30.9 μs  | 51718146 notes/s |
| Colorize gradient | colorize    | 1600  | 47.0 μs  | 34026384 notes/s |
| Colorize hue      | colorize    | 1600  | 54.4 μs  | 29413747 notes/s |
| Speed ×1.5       | speed       | 1600  | 32.1 μs  | 49858604 notes/s |
| Opacity 0.5       | opacity     | 1600  | 30.7 μs  | 52190222 notes/s |
| Wave              | wave        | 1600  | 60.7 μs  | 26375111 notes/s |
| Shuffle           | shuffle     | 1600  | 25.8 μs  | 61943876 notes/s |
| Filter: taps only | note_filter | 1600  | 36.3 μs  | 44088673 notes/s |
| Filter: no holds  | note_filter | 1600  | 33.6 μs  | 47610309 notes/s |
| Flip timing       | flip_timing | 1600  | 29.7 μs  | 53921073 notes/s |
| Scale ×1.2       | scale       | 1600  | 41.0 μs  | 38994114 notes/s |

## 7. Chart Compiler — compile_chart()

One-time compile cost at 60/120/240/480 Hz sample rates. Output size measured via write_phbc to memory buffer.

### ATHAZA.LeaF/AT.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8960    | 11.2 ms      | 3.5 MB    | 2622 B/note  | 24.2 KB/s    |
| 120 Hz      | 17919   | 25.4 ms      | 6.9 MB    | 5155 B/note  | 47.7 KB/s    |
| 240 Hz      | 35836   | 53.6 ms      | 13.7 MB   | 10220 B/note | 94.5 KB/s    |
| 480 Hz      | 71671   | 101.9 ms     | 27.4 MB   | 20352 B/note | 188.2 KB/s   |

### ATHAZA.LeaF/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 9747    | 17.4 ms      | 4.8 MB    | 4203 B/note  | 30.2 KB/s    |
| 120 Hz      | 19492   | 26.6 ms      | 9.5 MB    | 8317 B/note  | 59.7 KB/s    |
| 240 Hz      | 38983   | 62.0 ms      | 18.8 MB   | 16546 B/note | 118.7 KB/s   |
| 480 Hz      | 77965   | 148.8 ms     | 37.5 MB   | 33002 B/note | 236.9 KB/s   |

### AbsoluTedisoRdeR.AcuteDisarray/AT.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 11491   | 19.3 ms      | 5.7 MB    | 2812 B/note  | 30.4 KB/s    |
| 120 Hz      | 22981   | 38.9 ms      | 11.2 MB   | 5536 B/note  | 59.8 KB/s    |
| 240 Hz      | 45961   | 68.6 ms      | 22.2 MB   | 10983 B/note | 118.6 KB/s   |
| 480 Hz      | 91921   | 129.1 ms     | 44.3 MB   | 21877 B/note | 236.3 KB/s   |

### AbsoluTedisoRdeR.AcuteDisarray/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 11506   | 28.1 ms      | 5.7 MB    | 3540 B/note  | 30.2 KB/s    |
| 120 Hz      | 23010   | 55.6 ms      | 11.2 MB   | 6991 B/note  | 59.6 KB/s    |
| 240 Hz      | 46018   | 106.0 ms     | 22.2 MB   | 13894 B/note | 118.4 KB/s   |
| 480 Hz      | 92034   | 142.9 ms     | 44.3 MB   | 27698 B/note | 236.1 KB/s   |

### Aleph0.LeaF/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8161    | 10.3 ms      | 3.3 MB    | 3777 B/note  | 25.3 KB/s    |
| 120 Hz      | 16321   | 20.0 ms      | 6.6 MB    | 7465 B/note  | 50.0 KB/s    |
| 240 Hz      | 32641   | 38.5 ms      | 13.1 MB   | 14841 B/note | 99.5 KB/s    |
| 480 Hz      | 65281   | 77.6 ms      | 26.2 MB   | 29594 B/note | 198.4 KB/s   |

### BetterGraphicAnimation.ルゼ/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 6211    | 5.1 ms       | 1.4 MB    | 2307 B/note  | 14.3 KB/s    |
| 120 Hz      | 12421   | 9.1 ms       | 2.8 MB    | 4524 B/note  | 28.0 KB/s    |
| 240 Hz      | 24841   | 18.2 ms      | 5.5 MB    | 8960 B/note  | 55.5 KB/s    |
| 480 Hz      | 49681   | 35.3 ms      | 11.0 MB   | 17832 B/note | 110.4 KB/s   |

### Radiance.Nhato/AT.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8789    | 18.7 ms      | 4.3 MB    | 4644 B/note  | 30.2 KB/s    |
| 120 Hz      | 17576   | 28.3 ms      | 8.5 MB    | 9199 B/note  | 59.8 KB/s    |
| 240 Hz      | 35150   | 55.6 ms      | 17.0 MB   | 18309 B/note | 119.0 KB/s   |
| 480 Hz      | 70298   | 124.5 ms     | 33.8 MB   | 36528 B/note | 237.4 KB/s   |

### Radiance.Nhato/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8789    | 14.6 ms      | 4.3 MB    | 6413 B/note  | 30.0 KB/s    |
| 120 Hz      | 17576   | 26.2 ms      | 8.5 MB    | 12737 B/note | 59.6 KB/s    |
| 240 Hz      | 35150   | 54.9 ms      | 16.9 MB   | 25384 B/note | 118.9 KB/s   |
| 480 Hz      | 70298   | 119.9 ms     | 33.8 MB   | 50678 B/note | 237.3 KB/s   |

### Rrharil.TeamGrimoire/AT.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8048    | 14.1 ms      | 4.0 MB    | 3060 B/note  | 30.6 KB/s    |
| 120 Hz      | 16094   | 30.6 ms      | 7.8 MB    | 6031 B/note  | 60.3 KB/s    |
| 240 Hz      | 32187   | 63.7 ms      | 15.6 MB   | 11973 B/note | 119.6 KB/s   |
| 480 Hz      | 64372   | 112.7 ms     | 31.0 MB   | 23856 B/note | 238.4 KB/s   |

### Rrharil.TeamGrimoire/IN.json

| Sample Rate | Samples | Compile Time | PHBC Size | Bytes/Note   | KB/Chart-Sec |
| ----------- | ------- | ------------ | --------- | ------------ | ------------ |
| 60 Hz       | 8048    | 10.9 ms      | 3.0 MB    | 2317 B/note  | 23.2 KB/s    |
| 120 Hz      | 16094   | 20.7 ms      | 5.9 MB    | 4545 B/note  | 45.4 KB/s    |
| 240 Hz      | 32187   | 44.3 ms      | 11.7 MB   | 9002 B/note  | 89.9 KB/s    |
| 480 Hz      | 64372   | 75.8 ms      | 23.3 MB   | 17914 B/note | 179.0 KB/s   |

## 8. PHBC Binary I/O

Comparing source chart load (JSON parse + track build) vs PHBC load (binary read + to_chart_data) at 240 Hz compilation. Speedup = source_load / (read_phbc + to_chart_data).

| Chart                                  | PHBC Size | Source Load | Compile | Write   | Read    | →ChartData | PHBC Total | Speedup |
| -------------------------------------- | --------- | ----------- | ------- | ------- | ------- | ------------ | ---------- | ------- |
| ATHAZA.LeaF/AT.json                    | 13.7 MB   | 381.7 ms    | 53.4 ms | 18.4 ms | 12.6 ms | 5.7 ms       | 18.4 ms    | 20.78× |
| ATHAZA.LeaF/IN.json                    | 18.8 MB   | 870.7 ms    | 59.4 ms | 55.5 ms | 13.9 ms | 7.5 ms       | 21.4 ms    | 40.63× |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 22.2 MB   | 1122.1 ms   | 70.7 ms | 46.5 ms | 19.8 ms | 11.5 ms      | 31.4 ms    | 35.79× |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 22.2 MB   | 1285.5 ms   | 95.6 ms | 69.1 ms | 18.5 ms | 11.6 ms      | 30.1 ms    | 42.66× |
| Aleph0.LeaF/IN.json                    | 13.1 MB   | 114.8 ms    | 40.5 ms | 23.2 ms | 11.9 ms | 7.8 ms       | 19.7 ms    | 5.84×  |
| BetterGraphicAnimation.ルゼ/IN.json  | 5.5 MB    | 125.8 ms    | 18.4 ms | 6.4 ms  | 3.7 ms  | 1.0 ms       | 4.7 ms     | 26.55× |
| Radiance.Nhato/AT.json                 | 17.0 MB   | 551.9 ms    | 50.1 ms | 61.5 ms | 17.1 ms | 10.3 ms      | 27.4 ms    | 20.12× |
| Radiance.Nhato/IN.json                 | 16.9 MB   | 453.6 ms    | 62.8 ms | 36.7 ms | 19.5 ms | 9.7 ms       | 29.2 ms    | 15.51× |
| Rrharil.TeamGrimoire/AT.json           | 15.6 MB   | 327.4 ms    | 52.1 ms | 38.5 ms | 11.4 ms | 5.6 ms       | 17.0 ms    | 19.31× |
| Rrharil.TeamGrimoire/IN.json           | 11.7 MB   | 293.5 ms    | 35.4 ms | 15.9 ms | 9.5 ms  | 4.6 ms       | 14.1 ms    | 20.84× |

## 9. Memory Footprint

### Struct Sizes

| Struct    | Size (bytes) | Notes                                                     |
| --------- | ------------ | --------------------------------------------------------- |
| Note      | 168          | per-note data: times, position, kind, tint, state         |
| Line      | 424          | judge-line: 4× TrackFn (std::function) + PiecewiseTrack  |
| NoteState | 72           | runtime judge state per note                              |
| LineState | 64           | per-frame eval result (x, y, rot, alpha, scroll, cos/sin) |

### Per-Chart Heap Estimate

> Notes vector: `sizeof(Note) × note_count` (lower bound — does not include string heap for hitsound_path or PiecewiseTrack segment vectors). PHBC size is the on-disk binary; in-memory it becomes SampledTrack float arrays.

| Chart                                  | Notes | Lines | Notes Vec (lb) | States Vec | PHBC @ 240Hz |
| -------------------------------------- | ----- | ----- | -------------- | ---------- | ------------ |
| ATHAZA.LeaF/AT.json                    | 1344  | 19    | 225.8 KB       | 96.8 KB    | 13.7 MB      |
| ATHAZA.LeaF/IN.json                    | 1137  | 24    | 191.0 KB       | 81.9 KB    | 18.8 MB      |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 2025  | 24    | 340.2 KB       | 145.8 KB   | 22.2 MB      |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 1600  | 24    | 268.8 KB       | 115.2 KB   | 22.2 MB      |
| Aleph0.LeaF/IN.json                    | 885   | 20    | 148.7 KB       | 63.7 KB    | 13.1 MB      |
| BetterGraphicAnimation.ルゼ/IN.json  | 616   | 11    | 103.5 KB       | 44.4 KB    | 5.5 MB       |
| Radiance.Nhato/AT.json                 | 926   | 24    | 155.6 KB       | 66.7 KB    | 17.0 MB      |
| Radiance.Nhato/IN.json                 | 667   | 24    | 112.1 KB       | 48.0 KB    | 16.9 MB      |
| Rrharil.TeamGrimoire/AT.json           | 1300  | 24    | 218.4 KB       | 93.6 KB    | 15.6 MB      |
| Rrharil.TeamGrimoire/IN.json           | 1300  | 18    | 218.4 KB       | 93.6 KB    | 11.7 MB      |

## 10. Track Evaluation — Piecewise vs SampledTrack

Cost of one `eval(t)` call for a judge-line's `pos_x` track. Piecewise = `std::function` wrapping a `PiecewiseTrack` (binary search over segments). Sampled = `SampledTrack::eval()` at 240 Hz (two array lookups + linear interp). 100,000 calls per iteration, 3 warmup + 8 measured iterations.

| Chart                                  | Piecewise ns/call | SampledTrack ns/call | Speedup |
| -------------------------------------- | ----------------- | -------------------- | ------- |
| ATHAZA.LeaF/AT.json                    | 15.03 ns          | 5.63 ns              | 2.67×  |
| ATHAZA.LeaF/IN.json                    | 15.28 ns          | 6.28 ns              | 2.43×  |
| AbsoluTedisoRdeR.AcuteDisarray/AT.json | 18.44 ns          | 6.13 ns              | 3.01×  |
| AbsoluTedisoRdeR.AcuteDisarray/IN.json | 17.55 ns          | 6.11 ns              | 2.87×  |
| Aleph0.LeaF/IN.json                    | 18.25 ns          | 6.99 ns              | 2.61×  |
| BetterGraphicAnimation.ルゼ/IN.json  | 16.40 ns          | 6.78 ns              | 2.42×  |
| Radiance.Nhato/AT.json                 | 15.11 ns          | 6.22 ns              | 2.43×  |
| Radiance.Nhato/IN.json                 | 15.31 ns          | 6.01 ns              | 2.55×  |
| Rrharil.TeamGrimoire/AT.json           | 14.36 ns          | 6.29 ns              | 2.28×  |
| Rrharil.TeamGrimoire/IN.json           | 18.27 ns          | 6.46 ns              | 2.83×  |

## Summary

| Area                   | Headline Result                                        |
| ---------------------- | ------------------------------------------------------ |
| Parser throughput      | 12 MB/s peak (worst-case: 1319.0 ms for largest chart) |
| Engine simulation      | Up to 919× realtime at 240Hz step                     |
| build_frame (renderer) | Up to 988× realtime; worst-case mean 8.07 μs/frame   |
| Mod application        | All mods < 60.66 μs; range 26–61 μs                |
| PHBC load speedup      | Up to 42.7× faster than source chart load             |
| Note struct size       | 168 bytes; NoteState 72 bytes                          |

---

*Generated by `bench` — run `cpp/build/bench <charts_dir>` to reproduce.*

