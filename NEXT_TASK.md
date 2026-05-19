# NEXT_TASK.md

## Current Priority
Validate expanded CI on GitHub and decide whether to scale beyond 5M recorded ITCH:
- Push current branch and open/refresh PR so `.github/workflows/ci.yml` runs on GitHub-hosted `ubuntu-latest`.
- Confirm configure/build, parser, phase 2, phase 3, phase 4, phase C, research, phase E, and fuzz smoke all pass at least once.
- Push current branch and capture the first green GitHub Actions run for the expanded `ci.yml`.
- Optional: run full-day recorded ITCH conversion/replay if you want claims beyond the current bounded 5M-event artifact.
- Keep resume wording scoped to "5M recorded NASDAQ ITCH events" unless a larger/full-day artifact is added.

## Strict Scope
- Keep next diffs focused on Linux validation, evidence capture, and claim cleanup.
- Do not promote Phase D, Phase B multicast, or recorded-market-tape claims without artifacts.
- Keep synthetic-versus-recorded wording explicit in resume-facing docs.

## Recommended Next Prompt For Claude
"After this branch is pushed, check the first `ci.yml` run; if green, update CLAIMS_MATRIX.md with CI evidence and tighten README wording only where evidence changed."
