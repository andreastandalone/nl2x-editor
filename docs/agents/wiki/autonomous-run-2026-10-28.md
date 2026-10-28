# Autonomous run, 2026-10-28

On 2026-10-28 Andrea handed the project to a coordinating agent while away (handoff prompt not in the repo). Rules from that handoff:

- Work tickets #2–#15 of spec #1 in dependency order, one branch and one squash-merged PR each.
- #3 (hardware session) needs Andrea: build the probe tool and `docs/hardware-discovery-plan.md`, keep #3 open; tickets blocked by #3 proceed on documented facts and isolated assumptions.
- Issues with pending human checks stay open after merge, with a comment "Implemented, awaiting verification: …" and label `ready-for-human`.
- Do not publish v0.1 (#15), do not edit spec #1, write nothing outside the repo except the session scratchpad.
- Keep `docs/open-questions.md` up to date; finish with an Italian summary at its top.

## Planned order

#2 → #3 (tool + plan) → #4 → #5 → #6 → #9, #10, #11 → #7 → #14 → #8 → #12 → #13 → #15 (prep only).

## Log

- 2026-10-28: foundations written (CODING_STANDARDS.md, this wiki, open-questions.md) on branch `2-walking-skeleton`.
- 2026-10-28: #2 merged as `c371feb` (PR #16). CI on macos-15 (arm64) passed build, ctest, lipo, pluginval and auval; a full run takes 8–25 min. Issue #2 left open with `ready-for-human` (DAW and synth checks).
- 2026-10-28: #3 software part delegated on branch `3-probe-tool` (worktree `.worktrees/3-probe`, based on the pre-squash #2 branch: rebase onto `main` before its PR).
- Lesson, 2026-10-28: write "Implements #N", never "Closes #N", in PR bodies of tickets with pending human checks, or the merge closes the issue.
- Lesson, 2026-10-28: CMake's downloader cannot reach github.com from the agent sandbox; reuse the tarballs in the session scratchpad `deps/` with `-DNL2X_DOWNLOAD_CACHE`.
