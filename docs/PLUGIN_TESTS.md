
## Plugin-level tests (`plugin_tests`, JUCE)

| Test | Measured | Criterion | Result |
|---|---|---|---|
| parameter count == table | 89 | 89 | PASS |
| all parameters automatable | 89 | all | PASS |
| chain order is a host parameter | Chain Order | "Chain Order" | PASS |
| initial state == "Golden Spread" preset | 0 mismatches | 0 | PASS |
| bypass parameter exposed to host | yes | yes | PASS |
| layouts: stereo->stereo, mono->stereo accepted; 5.1 out rejected | S M - | S M - | PASS |
| mono input -> stereo output (right channel populated) | R rms 0.207 | > 0.01, finite | PASS |
| reported latency follows OS / WARP mode | 1098 -> 351 | 1098 -> 351 | PASS |
| latency at 96 kHz | 607 | 607 | PASS |
| host bypass output == input delayed by reported latency | 0.000000000 | < 1e-6 | PASS |
| heap allocations inside processBlock() | 0 | 0 | PASS |
| state save/restore: every parameter (incl. chain order) | 0 differences (12408 bytes) | 0 | PASS |
| state save/restore: chain order | 17 | 17 | PASS |
| state save/restore: A/B slot and preset name | slot 1, Golden Spread | slot 1, Golden Spread | PASS |
| all factory presets load exactly (host programs) | 43/43 | all | PASS |
| next / previous (wraps around) | 1, 42 | 1, 42 | PASS |
| user preset save -> browse -> load (XML file) | ok, drive 21.50 | ok, drive 21.50 | PASS |
| A/B compare recalls each slot | A 3.0 / B 30.0 | A 3.0 / B 30.0 | PASS |
| editor created, default size | 1280x760 | 1280x760 | PASS |
| UI snapshots at 1280x760, 980x620, 1920x1140 (resizable) | written | PNG files | PASS |
| editor panels follow the chain-order parameter | 3210 | 3210 | PASS |

**Plugin tests: 21 checks, 0 failed.**
