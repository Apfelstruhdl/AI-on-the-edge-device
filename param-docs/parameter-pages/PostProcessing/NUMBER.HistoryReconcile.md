# Parameter `HistoryReconcile`
Default Value: `false`

!!! Warning
    This is an **Expert Parameter**! Only change it if you understand what it does!

History-anchored reconciliation of the reading. When enabled, the post-processing no longer
relies on the fixed `MaxRateValue` / `AllowNegativeRates` guards for sequences that have analog
ROIs. Instead it uses the physical properties of a mechanical counter to decide, every cycle,
whether to accept the recognised value or hold the previous one:

- A step across a most-significant analog-dial boundary (a *carry*) is accepted **only** with
  evidence that the dial below it wrapped (9 → 0): either observed directly since a recent cycle,
  or inferred from the reading itself (a genuine carry passes the sub-position through zero, while
  a premature dial flip leaves it unchanged). This is enforced on **both** clean and ambiguous
  frames, because a pointer can be misread part of a step off its true position without landing
  near an integer (so a premature carry could otherwise look "clean"). Otherwise the previous
  value is held. This is what blocks the premature/early dial transitions.
- Motion *within* a dial step is trusted on a clean frame; on an ambiguous frame forward motion is
  allowed while only sub-noise backward jitter is tolerated.
- As recovery, a clean carry is accepted once the value has been held for a sustained stretch
  (genuinely stuck, not merely flickering) — this self-heals a value left behind by a missed carry.
  Recovery is limited to a single dial step: a reading that is further out than that is left held
  instead, because staying clean and stable is no evidence that a reading a whole digit away is
  correct.
- A physically impossible single-cycle jump (see `HistoryMaxJump`) is never accepted.

This is most useful for meters with "early transition" dials, where a pointer (or digit) flips
ahead of the actual rollover. It complements `CheckDigitIncreaseConsistency`, which handles the
digit chain; `HistoryReconcile` adds the equivalent protection on the analog dial chain.

It has no effect on sequences without analog ROIs.

!!! Note "Capture interval"
    The logic assumes the typical consumption between two cycles stays well below one step of the
    most-significant analog dial (e.g. well under 100 L per cycle on a meter whose top dial counts
    ×0.1 m³). This holds comfortably for the usual 1–5 minute intervals. With much longer
    intervals, heavy flow can exceed that per-cycle step: the value then holds during the flow
    burst and catches up shortly after flow pauses — nothing is lost, but readings lag during the
    burst.

!!! Note "Recovery behaviour"
    A held value re-anchors as soon as a cycle is *unambiguous* (no dial near a boundary) or a
    genuine carry is observed, so it does not stay stuck once the dials move on. The thresholds
    (`maxJump`, the backward-jitter tolerance, and the gross-misread ceiling) are derived from the
    meter's decimal scaling, and the "near a boundary" margin is a built-in default sized to the
    analog recognition scatter.

    Recovery deliberately will **not** jump onto a reading that is more than one dial step away,
    even if that reading looks clean and stays steady for hours — a digit that reads one position
    ahead does exactly that while the meter is idle, and re-anchoring onto it would silently put the
    total a whole digit out. Such a value keeps being held, which is visible (the reading stops
    advancing and `value` diverges from `raw`) and harmless to the totals; it is then resolved by the
    normal `PreValueAgeStartup` re-seed or by setting the pre-value from the meter face.

!!! Note
    If you edit the config file manually, you must prefix this parameter with `<NUMBER>` followed by a dot (eg. `main.HistoryReconcile`). The reason is that this parameter is specific for each `<NUMBER>` (`<NUMBER>` is the name of the number sequence defined in the ROI's).
