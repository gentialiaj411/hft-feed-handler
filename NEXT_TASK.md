# NEXT_TASK.md

## Current Priority
Decide whether to use synthetic 2M evidence or obtain recorded market-tape evidence. `experiment_runner` now has a 2M synthetic canonical-event artifact with matching deterministic hashes across reruns, but no recorded venue-data experiment artifact exists.

## Strict Scope
- Keep next diffs focused on evidence capture and claim cleanup.
- Do not make the determinism path depend on Phase B multicast or WSL2 cross-process networking.
- Keep any "recorded market data" wording TODO/VERIFY until backed by a recorded-data artifact.

## Recommended Next Prompt For Claude
"If recorded data is available, convert it into a canonical journal and run `experiment_runner` twice. If not, update resume wording to explicitly say synthetic 2M canonical-event journal, using `bench/results/experiment_synth2m_{a,b}.json` as evidence."
