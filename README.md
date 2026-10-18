# Linux reference traces for the determinism test

Recorded on Linux from branch `claude/project-thread-wpcivw` (commit adeab390), headless, with `releasezone2-copy.ini` as `--base-settings`. Not code: this branch is only for carrying traces between machines and is never merged.

Unzip `traces/*.gz` into an empty directory and verify a build against them:

    python Tools/determinism_test.py --exe <exe> --base-settings releasezone2-copy.ini --verify-only --trace-dir <dir> --runs 1 --updates 2000 --seeds 0 1

That covers Dummy Assault and Decision Day, seeds 0 and 1, 2000 updates.
