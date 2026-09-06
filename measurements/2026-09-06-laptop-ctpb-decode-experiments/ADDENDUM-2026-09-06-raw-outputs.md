# Addendum: the four raw outputs, and how their absence was detected

This file adds to the record and changes nothing already in it.

## What was missing

`README.md` published a sha256 manifest listing eleven files. Only seven were in the tree.
The four absent ones were the raw outputs of every experiment the record reports —
`raw/v0-baseline.out`, `raw/v1-prealloc-dest.out`, `raw/v2-short-payload.out`,
`raw/v3-no-barrier-ctpb.out`. **The evidence for the result was the part that did not land.**

`.gitignore:29` is `*.out`. The record was published with a manifest citing files the
repository did not contain, and the commit's own `8 files changed` should have been read
against the eleven files in the directory and was not.

## Third instance of one trap, and the second fix was still too narrow

- 211 `.log` files in `paper-io-portability`, recovered at `824e35d`
- a cited `build.log` in `paper-quic-cid-routing`, recovered at `4f6d6b71`
- these four `.out` files

The fix applied after the first two whitelisted `!measurements/**/*.log` — **one extension**.
The next campaign wrote `.out` and was eaten hours later. The problem was never the
extension: a record can carry any file type a campaign chooses to write, and the ignore rules
were written for a LaTeX build. The exemption is now scoped to the path, `!measurements/**`,
so nothing under a record directory is ignored regardless of what it is called.

## The manifest is what made this detectable, and that is the point

Without a per-file sha256 list, this directory would have looked complete: a `raw/` folder
with a plausible file in it and no way to know four more were meant to be there. The manifest
turns a missing file into a *visibly* missing file. It was caught in twenty minutes rather
than surviving into the paper.

All four recovered files match their published sha256 exactly, so the manifest also proves
these are the same bytes the record was written from and not a re-run.

A verification pass over every record set this session published found no other discrepancy:
manifests of 11, 13 and 3 entries all resolve, with no missing files, no checksum mismatches
and nothing else on disk but absent from git.

## Note for the next record

`measurements/2026-09-02-laptop/` has **no** checksum manifest. It is complete as far as
`git status` can tell, but that is the same instrument that could not see the ignored files,
so "complete" there rests on a weaker check than the one used here.
