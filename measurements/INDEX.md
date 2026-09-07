# Measurement record index

One row per record set, in the commit that deposits it, and changed in the commit that
supersedes it. An index maintained separately from the records it indexes goes stale, and a
stale index is worse than none because it is believed.

| set | what it measures | status |
|---|---|---|
| `2026-09-04-constexpr-step-limit` | Clang's constexpr step limit and the field count that reaches it | current |
| `2026-09-06-laptop-ctpb-rerun` | first laptop re-run of the paper's figures | **superseded** by `-corrected` |
| `2026-09-06-laptop-ctpb-rerun-corrected` | the same, after the decode-arm asymmetry was corrected | current |
| `2026-09-06-laptop-ctpb-decode-profile` | where libprotobuf-lite's decode time goes; found the 7.37% UTF-8 validation | current |
| `2026-09-06-laptop-ctpb-decode-experiments` | pre-allocated destination, compiler barrier, and what each was worth | current |
| `2026-09-06-laptop-p5-single-host` | P5's figures, one host one sitting | **superseded** by `2026-09-07-laptop-p5-retake` |
| `2026-09-07-laptop-utf8-costing` | what a UTF-8 validator would cost, estimated from a standalone benchmark | **superseded** by `-utf8-measured`; its 10× DFA-vs-fast-path figure still stands |
| `2026-09-07-laptop-utf8-measured` | the validator measured in place: below instrument resolution, ctpb still wins | current |
| `2026-09-07-laptop-p5-retake` | P5 re-taken through the repaired driver, with validation, no UTF-8 caveat | current |

**Absolutes do not transfer between sessions on this host.** Thermal state moves them by
20% or more; only ratios and within-run comparisons carry. Each record says so where it
applies.
