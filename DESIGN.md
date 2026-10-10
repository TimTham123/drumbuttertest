# DrumSpread — Design Document

DrumSpread is an original drum-processing multi-effect ("drum bus character"
category). The name, module names, UI and presets are all original. No
third-party samples, impulse responses or presets are used. Every noise source,
reverb and preset is synthesized or authored in this repository.

Status markers used in this document:
- **[verified]**: checked by the automated test harness (see `TEST_REPORT.md`).
- **[unverified]**: a design assumption or literature recollection that was
  not independently confirmed (see §12).

---

## 1. Signal flow

```
            ┌────────────────────────────── dry (original input, latency-compensated) ─────────────────────────────┐
            │                                                                                                       │
 In ─► mono→stereo ─► Input gain ─► [LAYER] ─► [slot A] ─► [slot B] ─► [slot C] ─► [slot D] ─► [SMASH] ─► MASTER ─►(Dry/Wet)─► Output gain ─► Master Clip ─► Out
                                    fixed      └──────── four reorderable modules ─────────┘     fixed
                                    first      (WARP, ROOM, PRESS, HEAT in any order)            last
```

The MASTER block runs: global transient shaper → 3-band EQ → HPF/LPF → Air.

Each module (including the fixed ones) sits inside a **ModuleSlot**. The slot
provides the shared controls (§3), latency compensation and bypass:

```
 x ──┬──────────────────────────────────────────────── delay(L) ───────────── dx ──┐
     │                                                                              ▼
     └─► FocusSplit ─► band ─┬─► Module.process ─► proc ──────────┐        out = dx + g·w·(proc − dband)
                             ├─► delay(L) ───────────────► dband ─┘        g = mix·intensity·(1−bypass)
                             └─► TransientDetector ─► w (Target weight, delayed by L for output-mode modules)
```

`L` is the module's latency (0 for non-oversampled, non-STFT modules).

---

## 2. Modules

| # | Name | Position | Function | Latency (48 kHz, base-rate samples) |
|---|------|----------|----------|------------------------|
| 1 | **LAYER** | fixed first | Triggered sub oscillator (with growl) + triggered noise layer | 0 (§4.1) |
| 2 | **WARP** | reorderable | Independent pitch & formant shift (phase vocoder) | HQ: N−1 = 1023, Lo-Fi: 255 (scales with SR) |
| 3 | **ROOM** | reorderable | Ambience: Room, Plate, Spring, Gated, Vintage | 0 |
| 4 | **PRESS** | reorderable | Six compressor characters | oversampler latency |
| 5 | **HEAT** | reorderable | Six saturation modes | oversampler latency |
| 6 | **SMASH** | fixed last | Waveshaper (5 curves, drive, bias) → soft/hard clipper | oversampler latency |
| – | **MASTER** | after chain | Transient shaper, 3-band EQ, HPF/LPF, Air, output, Dry/Wet, Master Clip | 0 |

Oversampler latency (§6): 1× = 0, 2× = 25, 4× = 32 base-rate samples,
independent of the sample rate.

Default reorderable order: WARP → PRESS → HEAT → ROOM. Compression goes before
saturation and the reverb comes last, so the room stays clean.

---

## 3. Shared per-module controls

### 3.1 Focus filters (HP "Lo" / LP "Hi")
**Design: complementary-by-subtraction split built from 2nd-order (12 dB/oct)
Butterworth TPT state-variable filters.**

```
band     = LP2(HP2(x))          (module processes this)
residual = x − band             (bypasses the module; implicit, never computed)
out      = x + (proc(band) − band)   ⇒ if proc = identity, out ≡ x
```

Why this design and not a plain Linkwitz-Riley band split:
- An LR4 split sums to an **allpass**, not to the input. A module at 0 % mix
  would then not null against the dry signal, and any full-band dry/wet blend
  would comb-filter (the LR4 allpass is −180° at the crossover, giving a
  complete notch at 50 % mix).
- A linear-phase FIR split is perfectly coherent, but a 20–100 Hz crossover
  needs a filter tens of milliseconds long. That would add latency to every
  module.
- The subtraction split reconstructs perfectly by construction, adds zero
  latency, and composes cleanly with Mix and Bypass: `out = dx` exactly at
  0 % mix.
- Tradeoff: the residual is not itself a clean filter shape. A module that
  changes the in-band level by a gain g gives a response of
  |1 + (g−1)·HP(f)·LP(f)|. With a single 2nd-order Butterworth edge,
  B(fc) = ±j/√2 (90° phase), so at the edge frequency the response is
  √(1 + (g−1)²/2). Fully removing the band (g = 0) leaves a +1.76 dB bump at
  fc (worst case +2.1 dB just below fc). A +6 dB boost (g = 2) reaches
  +1.76 dB at fc. This is monotonic, with no notch. 24 dB/oct LR4 edges would
  give a −6 dB notch at fc for g = 2. 12 dB/oct is therefore the deliberate
  choice. With both edges engaged, the other filter's phase shifts these
  values slightly (focus 300 Hz–3 kHz: +1.14 dB at 300 Hz for g = 0). The
  harness compares the measured response with the analytic formula; it
  matches within 0.25 dB **[verified]**.
- "Off" positions: Lo ≤ 20.5 Hz or Hi ≥ 19.5 kHz disables that filter, so
  the band equals the full signal.
- For the additive modules (LAYER, ROOM), focus shapes what the module
  *listens to*. LAYER detects from the focus band. ROOM sends the focus band
  into the reverb.

### 3.2 Target (Transient ↔ Sustain), −100 … +100 %
A dual-envelope transient detector runs on the in-band signal: peak of
|L|,|R|, then two one-pole followers with time constants (not per-sample
coefficients, so behavior is sample-rate independent):
- fast: attack 0.5 ms, release 40 ms
- slow: attack 25 ms, release 40 ms

Transientness is t = clamp((dB(fast) − dB(slow)) / 9 dB, 0, 1), smoothed over
1 ms. With bias b = target/100:
- b < 0: w = 1 − |b|·(1 − t) (b = −1 → acts only on attacks)
- b > 0: w = 1 − b·t (b = +1 → acts only on tails)
- b = 0: w = 1

`w` scales the module's **wet amount** (`out = dx + g·w·(proc − dband)`).
Exception: ROOM applies `w` to the reverb **send**. This keeps tails ringing
naturally ("transient-only send" is the classic trick for a clean room).

### 3.3 Mix and Bypass
- Mix is a per-sample smoothed gain (control rate 16 samples with linear
  ramps, ~20 ms one-pole target smoothing).
- Bypass is a 20 ms linear ramp of the same gain `g`. At g = 0 the output is
  `dx`, the latency-compensated input, bit-exact. A fully bypassed module
  stops processing to save CPU. On re-enable it is reset, run "warm" for L
  samples while still bypassed, then ramped in.
- For additive modules (LAYER, ROOM), `proc − band` is just the added layer
  or reverb. Mix is therefore a send/return level, and the dry signal never
  ducks. This keeps reverb gain staging predictable.

---

## 4. Module details

### 4.1 LAYER (fixed first)
**Sub section**
- Kick detector, as built after tuning against the harness:
  - Band: 4th-order Linkwitz-Riley LP at 150 Hz plus a 2nd-order HP at 60 Hz.
    The HP attenuates the previous kick's tail and sustained bass notes.
  - Envelopes on band power: fast (1.5 ms / 20 ms) and slow (20 ms / 60 ms).
    The slow release is longer than the fast one, so the fast/slow ratio
    collapses inside decays. With equal releases the ratio stays frozen and
    the detector re-triggers on every hold-off.
  - A trigger needs all of the following:
    1. Fast power above the absolute threshold.
    2. A rise over the slow envelope.
    3. Low-band dominance: band power > −10 dB of full-band power. This
       rejects snare bodies.
    4. A level within a relative threshold of a ~1.5 s decaying memory of
       recent kick peaks.
    5. 3 dB above a 300 ms background average. This rejects sustained bass.
  - The detector re-arms only after the envelope has fallen 3 dB below the
    triggering peak, plus a 70 ms hold-off.
- `sens` 0–100 % maps:
  - absThresh from −12 to −54 dBFS;
  - the rise from 6 to 0 dB (3 dB at 50 %);
  - the relative threshold from −4 to −12 dB (−8 dB at 50 %).
- Velocity is captured over the first 6 ms after the trigger: (peak −
  absThresh)/18 dB, clamped to [0.3, 1].
- Oscillator: sine with a pitch envelope
  f(t) = f₀ + (f₀·2^(drop/12) − f₀)·e^(−t/τp), with τp = clamp(0.12·decay,
  8 ms, 60 ms). The amplitude has a 1.5 ms raised-cosine attack, then an
  exponential decay with T60 = `decay`.
- **Phase alignment:** at the trigger, the instantaneous phase of the
  band-filtered source is estimated by quadrature-by-differentiation,
  φ = atan2(lp, lp′/ω). The analytic phase lag of the detection filter at the
  start frequency is added back. The oscillator starts at that phase, so it
  reinforces rather than cancels the source's fundamental. The same estimate
  sets the polarity.
- **Growl:** the oscillator is synthesized directly at the oversampled rate.
  It is shaped by an asymmetric tanh (drive 1…16×), blended by `growl`, and
  decimated with the halfband decimator. The decimator's group delay
  (12.5 / 15.75 base samples at 2×/4×) is compensated by advancing the
  oscillator phase rather than by adding plugin latency. The sub is a
  synthesized layer, so a 0.3 ms offset relative to a detector that already
  fires 1–3 ms after the onset is inaudible.
- Two voices: a retrigger fades the old voice out over 5 ms.

**Noise section**
- Snare/hat detector: 4th-order LR HP at 2 kHz with power envelopes, fast
  (1 ms / 15 ms) vs slow (12 ms / 30 ms).
  - Rise of 7.5–1.5 dB (4.5 dB at 50 %), re-arm at −3 dB, 40 ms hold-off.
  - **Kick-click veto:** the noise voice starts immediately. If the kick
    detector confirms a kick within 8 ms, the onset was the kick's beater
    click: the noise voice is cancelled with a 2 ms fade and not counted.
- Types:
  - White: xorshift32, uniform, decorrelated L/R.
  - Pink: Paul Kellet's refined 7-pole filter.
  - Crackle: Poisson-distributed impulses with random amplitude and polarity,
    density 4 kHz, through a first-difference emphasis (x − 0.6·x₋₁).
  - Band: white noise through a resonant SVF bandpass with Q 2.5.
- Tone: for White/Pink/Crackle, a 1-pole split at 2 kHz whose low and high
  halves are blended from dark (low half only) to bright (high half only),
  flat at 50 %. For Band, the center frequency runs from 300 Hz to 12 kHz
  (log).
- Envelope: 0.5 ms attack, then exponential decay with T60 = `decay`, scaled
  by velocity. Changing type crossfades over 20 ms.
- Trigger LEDs: an atomic flag per detector, read by the UI at 30 Hz.

### 4.2 WARP (pitch / formant shift)
Phase vocoder with transient phase reset and cepstral formant envelope:
- Frame size N = 1024 (HQ) or 256 (Lo-Fi) at 44.1/48 kHz, doubled at
  88.2/96 kHz and quadrupled at 176.4/192 kHz. This keeps time resolution
  constant in milliseconds. Hop H = N/4, periodic Hann analysis and synthesis
  windows, zero-phase (centered) frames.
- Analysis: per-bin true frequency from the phase difference
  (standard PV: ω = ωk + princarg(Δφ − ωk·H)/H).
- Pitch shift by bin remapping: each source bin k goes to j = round(k·P),
  with frequency P·ω (the strongest contributor wins). Output phases are
  accumulated as ψj += H·ω′.
- Formant: the spectral envelope is estimated by cepstral smoothing (real
  cepstrum, lifter with nc = N/8, i.e. 2.7 ms, and a raised-cosine taper).
  - It is computed once per frame from the mean L/R magnitude and applied as
    a correction factor env(j/F)/env(j/P) to the remapped magnitudes. The
    cepstrum is computed only when F ≠ P.
  - nc = N/16 under-shifted the formant by about 50 %. N/8 measures within
    ±5 % **[verified]**, using noise through a resonance.
  - Limitation: harmonic sources with F0 > 375 Hz have a pitch quefrency
    below the lifter, so their envelope picks up pitch ripple. Drums are
    largely unaffected.
- Linked mode: effective formant = formant + pitch, so with formant = 0 it
  behaves like a varispeed shift.
- Transients: a frame whose high-frequency-weighted spectral energy (Σ k·|X_k|²)
  rises more than 6 dB above the previous frame's flags a transient. There is
  a 2-frame minimum spacing. All output phases are then reset to the
  analysis phases of their source bins (Röbel/Duxbury-style phase reset). With
  centered frames, a click at the frame center stays vertically coherent.
- Loudness matching. The raw PV output level drifted from −11 dB to
  +7 dB depending on the shift, the formant mode and the material, because of
  empty or merged bins, content pushed past Nyquist and the envelope correction.
  Two stages correct this:
  1. A frame-wise spectral energy match (output to input frame energy, shared
     by L/R, ±12 dB, smoothed over ~3 frames).
  2. A slow 150 ms time-domain match against the aligned input (±6 dB, held
     in silence). This covers the ~3 dB overlap-add coherence loss of
     decorrelated frames.
  Measured: within ±0.5 dB over −12…+12 st, both modes, on drums and noise
  **[verified]**.
- Neutral shortcut: when P = F = 1 the wet path is the exactly delayed input.
  Leaving neutral warms the PV for N samples, then crossfades over 20 ms.
- Latency: N − 1 samples. At 48 kHz that is 21.3 ms (HQ) or 5.3 ms (Lo-Fi).
  This is the main latency tradeoff in the plugin. Shorter frames improve
  transients and latency but smear low-frequency partials, because 1024 at
  48 kHz is 47 Hz bin spacing. Lo-Fi deliberately uses N = 256, which gives
  a grainy, robotic character at 1/4 of the latency.
- Changing mode changes the reported latency (see §6.3).

### 4.3 ROOM (ambience)
All types share predelay (0–200 ms), an input bandwidth LP (from Tone),
send weighting (Target), M/S width on the wet signal (0–150 %), and wet-level
normalization (§4.3.1).

| Type | Structure |
|------|-----------|
| Room | 14-tap stereo early-reflection pattern (taps scaled by Size, alternating L/R, 1/r-style gains) + 8-line FDN (Householder feedback matrix). Base delays 23–61 ms × (0.35…1.6 by Size), per-line Jot-style one-pole absorption filters giving the DC T60 = Decay and HF T60 = Decay·(0.15…0.9 by Tone). |
| Plate | Dattorro figure-of-eight tank (JAES 1997): 4 input diffusers (142/107/379/277 samples @29761 Hz, coefficients 0.75/0.625), modulated decay diffusers (672/908, ±8 samples at ~1 Hz), damping, decay diffusers 1800/2656, tank delays 4453/3720/4217/3163, and the published 7-tap L/R output sums. All lengths are scaled to the current SR and by Size (0.5…1.3). The decay gain occurs **twice per tank half**, so decay = 10^(−3·loop/(4·T60)). The first implementation used 2·T60 and measured exactly half the set RT60. |
| Spring | Two springs (L/R, lengths differ by 13 %): a spectral-delay chain of 64 first-order allpasses (a = −0.62) inside a feedback loop with a 28–45 ms delay (by Size), loop gain from Decay, a 4.5 kHz LP, and slow random delay modulation. This follows the Välimäki/Parker/Abel "parametric spring reverb" idea. |
| Gated | Dense FDN (Room engine, fixed T60 3 s) followed by a gate keyed from the send envelope. The gate opens on a rise, holds for Decay (clamped to 40 ms–1.5 s) and closes in 15 ms. This gives the classic gated-snare shape. |
| Vintage | 4-line FDN with chorused delays (0.4–1.3 Hz, ±2 ms), wet band-limited to 9 kHz × Tone, plus 12-bit quantization of the wet signal for an early-digital grain. |

Mono input gives convincing stereo because the left and right inputs feed
disjoint FDN lines with different delay lengths, and the outputs use different
sign patterns (decorrelated). Plate uses the Dattorro L/R taps; Spring uses
detuned springs. This is **[verified]** by the inter-channel correlation of
the wet output for mono input (report: `room.stereo`).

#### 4.3.1 Gain staging
ROOM is additive (`out = dx + mix·w_send·wet`), and the dry signal never
changes level.
- The wet signal is normalized by g_type·(1 + T/0.6 s)^−0.45, where T is the
  decay time (the gate hold for Gated). This law was fitted to the measured
  growth of wet energy with decay time on drum material.
- g_type is calibrated by the harness so that 100 % mix gives wet RMS ≈ −4 dB
  re dry for every type. Measured: −3.9…−4.0 dB, with a 0.5 dB spread over
  Decay 0.3…8 s **[verified]**.

#### 4.3.2 Type switching
The new algorithm's **input** fades in over 50 ms while the old algorithm's
input fades out. The old algorithm keeps running, so its tail rings on and
fades out over a further 250 ms. A plain output crossfade from a freshly reset
reverb produced an audible onset (click-test ratio 3.0), because a reset reverb
is hit by a full-level signal.

### 4.4 PRESS (compressor)
Everything runs at the oversampled rate (§6) so the coloration stages do not
alias. A shared sidechain HPF (2nd order, 20 Hz = off, default 80 Hz) feeds a
stereo-linked detector (max of L/R). The gain computer works in the dB domain
with a quadratic soft knee (Giannoulis/Massberg/Reiss 2012), and ballistics
are smoothed on the gain-reduction signal in dB.

`Amount` a ∈ [0, 1] sets the threshold T = −36·a dB together with a ratio
from the type's ratio law. `Attack` and `Release` (0–100 %) map log-linearly
into each type's range.

| Type | Topology | Ratio(a) | Knee | Attack range | Release range | Character |
|------|----------|----------|------|--------------|---------------|-----------|
| FET | feed-forward, peak | 4 → 12 (All-Buttons: 20 + over-compression) | 2 dB | 0.02–0.8 ms | 50–1100 ms, program-dependent | Class-A odd harmonics (tanh, more with GR); All-Buttons adds over-compression (−0.25 dB/dB above T + 3 dB), +6 dB drive into the color stage |
| Opto | feedback | 2 → 4 | 10 dB | 5–30 ms | two-stage: 60 ms fast half + 0.5–5 s slow half, slow time grows with "memory" | gentle even harmonics |
| VCA | feed-forward, peak | 1.5 → 8 | Hard 0 / Soft 8 dB (Knee switch) | 0.1–30 ms | 20–1000 ms | clean |
| Diode | feed-forward | 1.5 → 6 | 8 dB | 0.5–50 ms | 50–1500 ms | asymmetric distortion proportional to GR depth + light transformer LF saturation |
| Vari-Mu | feedback | 1.2 → 4.2, ratio rises above threshold | 18 dB | 1–50 ms | 100–2000 ms | 2nd-harmonic tube stage, level-dependent |
| Glue | feed-forward, peak | 2 → 10 (log) | Hard 1 / Soft 6 dB | stepped 0.1/0.3/1/3/10/30 ms | stepped 0.1/0.3/0.6/1.2 s/Auto | clean; Auto = program-dependent dual release |

Program-dependent release (FET, Glue Auto): a 300 ms smoother tracks how
continuously the unit is compressing, h ∈ [0, 1]. Then
τrel = rel·(0.3 + 0.7·h): short hits release fast, sustained material slowly.

Auto-makeup: makeup = 0.5·|T|·(1 − 1/R) dB (half the static GR at 0 dBFS).
It is applied after the color stage.

Type changes crossfade over 25 ms. Both type processors run during the fade,
and the new one starts from the old one's gain reduction.

GR meter: the per-block maximum gain reduction, through an atomic.

### 4.5 HEAT (saturation)
Runs at the oversampled rate.
- Drive g = 0–36 dB.
- Level compensation: y = f(g·x)·r/f̄(g·r), with r = 0.25 (−12 dBFS) and f̄
  the mean of |f(±g·r)|. Small signals pass near unity at any drive and peaks
  are compressed.
- Tone: a post tilt (±6 dB) around 800 Hz.
- Trim: −24 to +12 dB.
- DC blocker (5 Hz) after the asymmetric modes.

| Mode | Model |
|------|-------|
| Tape | +6 dB pre-emphasis shelf (3 kHz) → soft saturator with mild asymmetry and a simple hysteresis term (state-dependent offset) → matching de-emphasis; head-bump bell +1.5 dB at 70 Hz; HF loss that grows with drive |
| Tube | triode-like asymmetric curve (positive side tanh, negative side harder grid-conduction knee), dynamic bias that follows the signal envelope ("bloom") → strong 2nd harmonic |
| Transformer | flux model: leaky integrator → soft saturation (x/(1+|x|³)^(1/3)) → leaky differentiator. LF saturates first, as in a real core |
| Diode | Shockley-style asymmetric diode pair (1 vs 2 diodes in series) using a smooth asinh law, between a +6 dB pre-emphasis high shelf at 1.2 kHz and the matching de-emphasis (upper mids clip first) |
| Fuzz | very high gain, asymmetric hard-knee clip with envelope-dependent bias starvation (sputter/gating on decays) |
| Crush | bit-depth reduction (Drive → 16…2 bits, plain rounding with no dither, for grit) + sample-and-hold rate reduction (Tone → 1…32× hold). Tone acts on the hold factor instead of tilt |

Mode changes crossfade over 25 ms.

### 4.6 SMASH (fixed last)
Waveshaper → clipper → trim, at the oversampled rate.
- Curves:
  - Smooth: tanh.
  - Crunch: cubic soft knee into hard limit.
  - Fold: sine wavefolder.
  - Rectify: blend toward full-wave rectification, which adds an octave.
  - Stairs: smooth staircase x − sin(2πnx)/(2πn).
- Drive 0–36 dB uses the same level compensation as HEAT.
- Bias −100 … +100 % adds a DC offset before the shaper (asymmetry). A
  DC blocker removes it afterwards.
- Clipper: Soft (C1-continuous tanh knee starting at −3 dB below the ceiling)
  or Hard. Ceiling runs from −24 to 0 dB, with no automatic makeup; Trim is
  provided.
- Curve and clip-mode changes crossfade.

### 4.7 MASTER
- Global transient shaper (Attack/Sustain ±100 %): SPL-style. The attack
  gain comes from dB(fast) − dB(slow, slow attack); the sustain gain from
  dB(slow release) − dB(fast release). Each is scaled to ±12 dB at ±100 %,
  stereo-linked and smoothed.
- EQ: low shelf (30–400 Hz), mid bell (200 Hz–8 kHz, Q 0.8), high shelf
  (2–16 kHz), each ±12 dB, all TPT SVF (Simper).
- HPF 10–500 Hz (10 = off) and LPF 1–22 kHz (22 kHz = off), 12 dB/oct.
- Air (0–100 %): a high shelf at 10 kHz up to +6 dB, plus a parallel exciter
  (HP 5 kHz → gentle asymmetric saturator → mixed in up to −14 dB).
- Dry/Wet blends the latency-compensated original (pre-input-gain) signal.
- Output gain, then **Master Clip**: identity below 0.7·ceiling, then a
  C1-continuous tanh knee that never exceeds the ceiling. It can be switched
  off.

---

## 5. Parameters

All parameters are exposed through `AudioProcessorValueTreeState`. The single
source of truth is `Source/dsp/Params.cpp`, which is shared by the plugin, the
presets and the test harness. "Taper" is the knob law: lin = linear; log =
logarithmic (skewed so the centre is the geometric mean).

### 5.1 Global
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| in_gain | Input | −24 … +24 | 0 | lin | dB |
| out_gain | Output | −24 … +24 | 0 | lin | dB |
| dry_wet | Dry/Wet | 0 … 100 | 100 | lin | % |
| intensity | Intensity ("Magnitude") | 0 … 200 | 100 | lin | % |
| oversampling | Oversampling | 1× / 2× / 4× | 2× | choice | – |
| bypass | Bypass | off/on | off | toggle | – |
| chain_order | Chain Order | 24 permutations | Warp>Press>Heat>Room | choice | – |

### 5.2 Shared (for each `m` ∈ layer, warp, room, press, heat, smash)
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| m_bypass | Bypass | off/on | off | toggle | – |
| m_mix | Mix | 0 … 100 | 100 (room: 30) | lin | % |
| m_focus_lo | Focus Lo (HP) | 20 … 5000 | 20 (= off) | log | Hz |
| m_focus_hi | Focus Hi (LP) | 200 … 20000 | 20000 (= off) | log | Hz |
| m_target | Target | −100 (transient) … +100 (sustain) | 0 | lin | % |

### 5.3 LAYER
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| layer_sub_sens | Sub Sens | 0 … 100 | 50 | lin | % |
| layer_sub_freq | Sub Freq | 30 … 120 | 52 | log | Hz |
| layer_sub_drop | Sub Drop | 0 … 24 | 7 | lin | st |
| layer_sub_decay | Sub Decay | 20 … 1000 | 220 | log | ms |
| layer_sub_growl | Growl | 0 … 100 | 0 | lin | % |
| layer_sub_level | Sub Level | −48 … +12 | −12 | lin | dB |
| layer_noise_sens | Noise Sens | 0 … 100 | 50 | lin | % |
| layer_noise_type | Noise Type | White/Pink/Crackle/Band | Pink | choice | – |
| layer_noise_tone | Noise Tone | 0 … 100 | 50 | lin | % |
| layer_noise_decay | Noise Decay | 10 … 800 | 90 | log | ms |
| layer_noise_level | Noise Level | −48 … +6 | −24 | lin | dB |

### 5.4 WARP
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| warp_pitch | Pitch | −24 … +24 (0.01 steps = cents) | 0 | lin | st |
| warp_formant | Formant | −24 … +24 (0.01 steps) | 0 | lin | st |
| warp_link | Link | off/on | off | toggle | – |
| warp_mode | Mode | HQ / Lo-Fi | HQ | choice | – |

### 5.5 ROOM
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| room_type | Type | Room/Plate/Spring/Gated/Vintage | Room | choice | – |
| room_size | Size | 0 … 100 | 40 | lin | % |
| room_decay | Decay | 0.1 … 8 | 0.8 | log | s |
| room_predelay | Pre-delay | 0 … 200 | 8 | log-ish (skew) | ms |
| room_tone | Tone (damping) | 0 (dark) … 100 (bright) | 55 | lin | % |
| room_width | Width | 0 … 150 | 100 | lin | % |

### 5.6 PRESS
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| press_type | Type | FET/Opto/VCA/Diode/Vari-Mu/Glue | Glue | choice | – |
| press_amount | Amount | 0 … 100 | 35 | lin | % |
| press_attack | Attack | 0 … 100 | 50 | lin (mapped per type) | % |
| press_release | Release | 0 … 100 | 40 | lin (mapped per type) | % |
| press_sc_hpf | SC HPF | 20 (off) … 500 | 80 | log | Hz |
| press_makeup | Auto Makeup | off/on | on | toggle | – |
| press_knee | Knee | Hard/Soft | Soft | choice | – |
| press_all_buttons | All Buttons (FET) | off/on | off | toggle | – |

### 5.7 HEAT
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| heat_mode | Mode | Tape/Tube/Transformer/Diode/Fuzz/Crush | Tape | choice | – |
| heat_drive | Drive | 0 … 36 | 6 | lin | dB |
| heat_tone | Tone | −100 … +100 | 0 | lin | % |
| heat_trim | Trim | −24 … +12 | 0 | lin | dB |

### 5.8 SMASH
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| smash_curve | Curve | Smooth/Crunch/Fold/Rectify/Stairs | Smooth | choice | – |
| smash_drive | Drive | 0 … 36 | 0 | lin | dB |
| smash_bias | Bias | −100 … +100 | 0 | lin | % |
| smash_clip_mode | Clip | Soft/Hard | Soft | choice | – |
| smash_ceiling | Ceiling | −24 … 0 | 0 | lin | dB |
| smash_trim | Trim | −24 … +12 | 0 | lin | dB |

### 5.9 MASTER
| ID | Name | Range | Default | Taper | Unit |
|----|------|-------|---------|-------|------|
| master_low_gain | Low | −12 … +12 | 0 | lin | dB |
| master_low_freq | Low Freq | 30 … 400 | 90 | log | Hz |
| master_mid_gain | Mid | −12 … +12 | 0 | lin | dB |
| master_mid_freq | Mid Freq | 200 … 8000 | 1200 | log | Hz |
| master_high_gain | High | −12 … +12 | 0 | lin | dB |
| master_high_freq | High Freq | 2000 … 16000 | 8000 | log | Hz |
| master_hpf | HPF | 10 (off) … 500 | 10 | log | Hz |
| master_lpf | LPF | 1000 … 22000 (off) | 22000 | log | Hz |
| master_air | Air | 0 … 100 | 0 | lin | % |
| master_attack | Attack | −100 … +100 | 0 | lin | % |
| master_sustain | Sustain | −100 … +100 | 0 | lin | % |
| master_clip | Master Clip | off/on | on | toggle | – |
| master_ceiling | Ceiling | −12 … 0 | −0.3 | lin | dBFS |

### 5.10 Intensity ("Magnitude") scaling — exact definition
Let k = intensity/100 ∈ [0, 2], a = min(k, 1) ("blend"), b = max(k − 1, 0)
("push").

| Target | Scaling |
|--------|---------|
| Every module's mix (all six) | mix_eff = mix · a. At k = 0 all modules null exactly. |
| LAYER sub level, noise level | + 6·b dB |
| WARP pitch/formant | not scaled (scaling a pitch interval is unmusical) |
| ROOM wet level | + 4·b dB |
| PRESS amount | amount_eff = min(1, amount·(1 + 0.5·b)) |
| HEAT drive | + 6·b dB (may exceed the 36 dB knob range, up to 42) |
| SMASH drive | + 6·b dB |
| MASTER EQ gains (low/mid/high) | gain_dB · k (so 0 → flat, 200 % → doubled) |
| MASTER Air | air · k |
| MASTER Attack/Sustain | amount · k |
| Input, Output, Dry/Wet, HPF/LPF, Master Clip, Ceiling, Oversampling | not scaled (utility/safety controls) |

---

## 6. Oversampling, latency and click-free switching

### 6.1 Oversampler
A cascade of linear-phase halfband FIR stages (Kaiser-windowed sinc,
β = 7.86; measured stopband −78.4 dB and passband ripple 0.001 dB **[verified]**), implemented polyphase. One branch of
each halfband is a pure delay, so only (N+1)/2 taps are computed per output
pair.
- Stage A (1×↔2×): 51 taps. Passband edge 0.2·fs₂ₓ (19.2 kHz at 48 kHz),
  stopband from 0.3·fs₂ₓ.
- Stage B (2×↔4×): 27 taps (wider transition).
- Linear phase means the dry path inside each module is a pure integer delay,
  so Mix and Bypass are exactly coherent.
- Round-trip latency: 2× = (51−1)/2 = 25 samples. 4× = 25 + 26/4 = 31.5,
  padded with two 4×-rate samples to an integer 32.

Oversampled stages: PRESS, HEAT, SMASH (whole module) and the LAYER
sub/growl (synthesized at the oversampled rate, decimated only).

### 6.2 Latency reporting
Total = 3·OS_latency + WARP latency. It is the same regardless of order,
bypass or mix, because every slot always delays by its own L.

| SR | OS | WARP mode | Latency (samples) | ms |
|----|----|-----------|-------------------|----|
| 48 k | 2× | HQ | 75 + 1023 = 1098 | 22.9 |
| 48 k | 2× | Lo-Fi | 75 + 255 = 330 | 6.9 |
| 48 k | 1× | Lo-Fi | 255 | 5.3 |
| 96 k | 2× | HQ | 75 + 2047 = 2122 | 22.1 |

The global Dry/Wet and the host bypass both use the same total delay, so host
bypass is latency-compensated. The plugin exposes its own `bypass` parameter
via `getBypassParameter()`.

### 6.3 Click-free changes
| Change | Mechanism |
|--------|-----------|
| Any continuous parameter | 16-sample control rate, per-sample linear ramps, one-pole target smoothing (~20 ms); filter coefficients recomputed per 16-sample chunk (TPT SVFs stay stable under modulation) |
| Module order | **True crossfade.** The four reorderable slots exist twice (bank A / bank B). On a reorder, the active bank's full DSP state is copied into the idle bank (preallocated, no allocation). The idle bank runs the new order and the active bank the old order. The new-order bank first runs silently for Σ(slot latencies) samples, so that each slot's latency-compensation history (which still holds the input from its old position) is flushed. Then the outputs are crossfaded over 40 ms (raised cosine) and the banks swap. Without the flush, a hard step appeared exactly L samples after the reorder (click-test ratio 13.9; below 1 after the fix, 0.83 in the final report). |
| Module bypass | 20 ms ramp of the slot gain (see §3.3) |
| Mode/type within a module | old and new mode processors both run for 20–25 ms with an equal-gain crossfade |
| Oversampling factor / WARP mode (these change latency) | 15 ms global fade-out → reconfigure → 3 ms silent hold (restarted filters settle) → 15 ms fade-in. Reconfiguring keeps all module state that stays valid: only oversampled-rate filter states restart, ROOM and the compressor envelopes continue, WARP resets only when its mode changes, and every compensation delay keeps its history and just reads at the new length. The message-thread timer then calls `setLatencySamples()`. |

### 6.4 Block-size independence
The engine splits every host block at absolute 16-sample boundaries, so control
updates happen at the same sample positions whatever the host block size.
STFT hops and FIR states are streaming. With static parameters, the output is
**bit-identical** across block sizes (**[verified]**, report: `blocksize.*`).

---

## 7. Real-time safety
- All memory is allocated in `prepare()`. The audio thread never allocates,
  locks, does file I/O or throws. The harness overrides global
  `operator new` and fails if any allocation happens inside `process()`
  across all modes, reorders, OS changes and preset switches
  (**[verified]**, report: `rt.alloc`).
- Bank cloning uses copy-assignment of equally sized `std::vector`s, which
  never reallocates when capacity is sufficient. Both banks are prepared
  identically.
- Denormals: FTZ/DAZ is set for the duration of `process()`
  (`juce::ScopedNoDenormals` in the plugin, an equivalent guard in the
  engine). Feedback paths also pass through a tiny-value flush.
- Lock-free SPSC FIFO for the spectrum/waveform display; atomics for meters,
  GR and LEDs.

---

## 8. Presets
- 43 factory presets in 7 categories: Polish, Punch, Glue, Space, Lo-Fi,
  Heavy, Wild. They are authored as `{param id → value}` maps in
  `Source/dsp/FactoryPresets.cpp`; unspecified parameters take their
  defaults.
- The initial/default preset is **"Golden Spread"** (Polish): light sub layer,
  Glue compression, gentle Tape, small Room at a low send, a touch of Air and
  the safety clip.
- User presets are saved as XML (`.dspreset`, the APVTS state plus name and
  category) in `<user app data>/DrumSpread/Presets`.
- Browse with a categorized popup and next/previous buttons, which cycle
  through factory then user presets.
- A/B compare: two full-state slots. Toggling stores the current state in the
  active slot and recalls the other. "Copy" duplicates the current state into
  the other slot.

---

## 9. UI layout sketch (default 1280×760, resizable 960×600 … 2560×1520)

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ DRUMSPREAD   ◀ [ Polish / Golden Spread ▾ ] ▶  [Save]   [A|B] [Copy]    OS [2×▾]  (INTENSITY) ⏻ │
├──────────────┬────────────┬────────────┬────────────┬────────────┬────────────┬──────────────┤
│ LAYER  ●sub  │ ⠿ WARP  ◀▶ │ ⠿ PRESS ◀▶ │ ⠿ HEAT  ◀▶ │ ⠿ ROOM  ◀▶ │   SMASH    │              │
│        ●nz ⏻ │    [HQ▾] ⏻ │ [Glue▾]  ⏻ │ [Tape▾]  ⏻ │ [Room▾]  ⏻ │ [Smooth▾]⏻ │  IN    OUT   │
│ SUB          │  (Pitch)   │ (Amount)   │  (Drive)   │  (Size)    │  (Drive)   │  ▮▮    ▮▮    │
│ (Sens)(Freq) │  (Formant) │ (Atk)(Rel) │  (Tone)    │  (Decay)   │ (Bias)     │  ▮▮    ▮▮    │
│ (Drop)(Decay)│  [Link]    │ (SC HPF)   │  (Trim)    │ (Pre)(Tone)│ (Ceil)(Trim)│ GR ▬▬       │
│ (Growl)(Lvl) │            │ [Mk][Knee] │            │ (Width)    │ [Soft/Hard]│              │
│ NOISE [Pink▾]│            │ [AllBtn]   │            │            │            │              │
│ (Sens)(Tone) │            │            │            │            │            │              │
│ (Decay)(Lvl) │            │            │            │            │            │              │
│ ┄┄┄┄┄┄┄┄┄┄┄┄ │ ┄┄┄┄┄┄┄┄┄┄ │ ┄┄┄┄┄┄┄┄┄┄ │ ┄┄┄┄┄┄┄┄┄┄ │ ┄┄┄┄┄┄┄┄┄┄ │ ┄┄┄┄┄┄┄┄┄┄ │              │
│ lo hi tgt mix│ lo hi t mix│ lo hi t mix│ lo hi t mix│ lo hi t mix│ lo hi t mix│              │
├──────────────┴────────────┴────────────┴────────────┴────────────┴────────────┴──────────────┤
│ MASTER (Atk)(Sus) │ (Low)(Mid)(High) f f f │ (HPF)(LPF) │ (Air) │ [Clip](Ceil) │ (Out)(Dry/Wet) │
│ ╭─ spectrum + waveform of output ───────────────────────────────────────────────────────────╮ │
│ ╰───────────────────────────────────────────────────────────────────────────────────────────╯ │
└──────────────────────────────────────────────────────────────────────────────────────────────┘
```
- Panels follow the chain order. Drag a reorderable panel's grip (⠿) onto
  another position, or use the ◀ ▶ buttons, to move it.
- As built:
  - Each panel header has two rows (title + on/off; mode selector + move
    buttons).
  - **INTENSITY** is the large knob at the left of the MASTER section.
  - Snapshots of the editor at 980×620, 1280×760 and 1920×1140 are written by
    `plugin_tests`; see `docs/`.
- Design language: "ink & signal". The background is a deep blue-black
  (#12151C) with panel cards in #1B202B. Each module has its own signal
  colour: LAYER amber, WARP violet, PRESS coral, HEAT vermilion, ROOM teal,
  SMASH chartreuse. Knobs are thick value arcs on a dark disc with the value
  printed in the centre. Typography is all-caps headers with tabular
  numerals. No skeuomorphic textures.

---

## 10. Decisions log
1. **JUCE-free DSP core** (`dscore` static library, plain C++17). The test
   harness runs the exact DSP the plugin runs, builds fast, and needs no GUI
   or plugin scaffolding. The plugin is a thin APVTS wrapper.
2. **Single parameter table** (`Params.cpp`) drives the APVTS layout, the
   presets and the tests, so parameter IDs cannot drift.
3. **Complementary-subtraction focus split** with 12 dB/oct Butterworth edges
   (§3.1): perfect reconstruction and exact 0 % mix null, at the cost of mild
   band-edge deviation.
4. **Linear-phase FIR halfband oversampling** instead of polyphase IIR: the
   dry/wet paths inside each module stay phase-coherent, the latency is an
   integer and the nulls are exact. Cost: 25 samples per oversampled module
   at 2×.
5. **Oversampling the LAYER sub without latency.** The sub is synthesized at
   the oversampled rate and only decimated; the decimator delay is absorbed
   into the phase-alignment estimate.
6. **WARP uses a phase vocoder.** Granular/PSOLA methods cannot give
   independent formant control on inharmonic drum material without pitch
   marks. Lo-Fi mode is the same engine at a quarter of the frame size (low
   latency, grainy), not a different algorithm. Formant control therefore
   works in both modes.
7. **Reorder by bank cloning with a true crossfade** rather than a dip/fade.
   State continuity (tails, envelopes) is preserved and the 40 ms crossfade
   has no gap.
8. **Bypassed modules stop processing** (CPU), then are reset and warmed up
   for L samples before ramping back in.
9. **Feed-forward for fast compressor types, feedback only for slow
   types** (Opto, Vari-Mu). High-ratio feedback loops with µs attacks are
   unstable in discrete time.
10. **ROOM and LAYER are additive** (Mix = send/return level), so the dry
    level never drops when a wet effect is added. This is the predictable
    gain staging the spec asks for.
11. **Latency changes on OS-factor and WARP-mode changes** are reported (with
    a short global fade) rather than padding every mode to the worst case.
    Users who want low latency get it.
12. **Master Clip is base-rate**, an identity region plus a smooth knee. It is
    a safety stage that is normally idle, so it does not justify another
    oversampler and its latency.
13. **Intensity two-regime law** (§5.10). 0–100 % blends every module from
    null to as-set, so k = 0 is truly neutral. 100–200 % pushes the
    "amount"-type controls beyond their settings.
14. **Bit-identical block-size independence** via absolute 16-sample control
    chunks.
15. **Windows binaries are built in CI** (GitHub Actions, MSVC, windows-latest)
    because the development container is Linux-only (see README).

---

## 11. References
- J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters," JAES
  45(9), 1997. Plate topology, delay lengths and output taps. Tap positions
  were cross-checked against an open-source implementation
  (github.com/el-visio/dattorro-verb).
- J.-M. Jot, A. Chaigne, "Digital delay networks for designing artificial
  reverberators," AES 90th Conv., 1991. FDN with absorption filters.
- V. Välimäki, J. Parker, J. Abel, "Parametric Spring Reverberation Effect,"
  JAES 58(7/8), 2010. Spectral-delay (allpass chain) in a feedback loop.
- A. Röbel, "A new approach to transient processing in the phase vocoder,"
  DAFx-03. C. Duxbury et al., "Improving time-scaling of musical sound
  using phase locking and transient detection," DAFx-02 (phase reset at
  transients).
- J. Laroche, M. Dolson, "New phase-vocoder techniques for pitch-shifting,
  harmonizing and other exotic effects," WASPAA 1999.
- D. Giannoulis, M. Massberg, J. Reiss, "Digital Dynamic Range Compressor
  Design — A Tutorial and Analysis," JAES 60(6), 2012. Soft-knee gain
  computer and log-domain smoothing.
- V. Zavalishin, "The Art of VA Filter Design" (TPT filters).
  A. Simper, "Linear trapezoidal integrated SVF" (Cytomic technical papers).
- P. Kellet, pink-noise filter (music-dsp archive).

## 12. Known limitations (measured)
- **Kick detector with a sustained bass line.** Slow beating between a kick
  tail and a bass note can still trigger the sub layer. On the synthetic
  bass-line confounder: 41/42 kicks hit, 13 false triggers. Lowering Sub Sens
  reduces this. The drum-only patterns score 100 % hits with 0 false
  triggers.
- **Hard-clipping modes at extreme drive** (Tape/Fuzz at +24 dB on a 7.9 kHz
  tone) improve only ~7 dB from 1× to 2× oversampling, because their harmonics
  decay as ~1/k. 4× gives a further ~8–9 dB, so use 4× for extreme settings on
  bright material.
- **WARP HQ latency is 21 ms.** This is the price of 47 Hz frequency
  resolution. Lo-Fi is 5.3 ms.
- **Vintage RT60** is about 10 % shorter than the Decay setting, because the
  chorus modulation adds loss.

## 13. Unverified items (flagged)
- The **spring chirp direction** (low frequencies delayed more, i.e. a
  descending chirp, with a = −0.62) follows my reading of the dispersion
  literature. It was not checked against a measured spring.
- The compressor **time-constant ranges** ("FET 20–800 µs attack", "opto
  ~10 ms attack / two-stage release", "glue stepped 0.1–30 ms attack") come
  from commonly cited descriptions of the generic topologies, not from
  measurements of hardware.
- The **saturation curves** are physically motivated, hand-fitted
  functions, not circuit simulations. The harness measures their harmonic
  signatures but cannot say how close they are to any real device.
- The **"vintage digital"** character (band-limit, 12-bit grain, chorus) is a
  stylistic interpretation.
- The Dattorro **excursion** (±8 samples at 29.761 kHz) follows the commonly
  quoted reading of the paper; secondary sources note the original wording is
  ambiguous (16 range vs 8 peak).
