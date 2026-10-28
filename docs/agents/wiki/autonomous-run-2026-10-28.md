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
- 2026-10-28: #3 tool built on branch `3-probe-tool` (SysEx basics, probe library and scripts, Probe window, final `docs/hardware-discovery-plan.md`); the hardware session itself awaits Andrea.
- 2026-10-28: #3 software merged as `fbd5bbc` (PR #17); issue open, `ready-for-human`. #4 delegated on `4-read-slots`, based on pre-squash `a6ce046` (rebase with `git rebase --onto main a6ce046`).
- 2026-10-28: #4 built on branch `4-read-slots` (worktree `.worktrees/4-read-slots`, based on `3-probe-tool`): Program codec, 66-entry parameter table, Synth model and connection with Global Channel probing, Kit recognition, generic Slot view. Awaits the hardware check (open-questions.md, "Andrea needed" 2).
- 2026-10-28: #4 merged as `7ecdeee` (PR #18); issue open, `ready-for-human`. #5 and #9 delegated in parallel on `5-performance` and `9-connection-check`, both based on pre-squash `696e92c` (rebase with `git rebase --onto main 696e92c`).
- Lesson, 2026-10-28: sub-agents editing `docs/open-questions.md` and this log caused a conflict on every rebase. From #5 on, sub-agents report decisions and human checks in their final report and the coordinator writes these files.
- 2026-10-28: #5 merged as `4321c70` (PR #19), #9 merged after it (PR #20); both open with `ready-for-human`. #9 had to be rebased over #5: engine conflicts were plain unions, the editor layout conflict went to a Sonnet sub-agent. #6 and #11 delegated in parallel on `6-all-ccs` and `11-library`, both based on pre-squash `c6a56b0` (rebase with `git rebase --onto main c6a56b0`).
- Lesson, 2026-10-28: parallel tickets that both touch `Engine.cpp`, `Engine.h` and `PluginEditor.*` always conflict; the engine side stays cheap when each ticket keeps its logic in its own files (asked for in the briefs), the editor side does not until #13 gives the GUI a stable structure.
- 2026-10-28: #11 merged as `472c4fd` (PR #21), #6 as `f262dd6` (PR #22); both open with `ready-for-human`. #10 in PR #23 (rebased over #6 by the coordinator). #12 delegated on `12-memory-writer` (based on `7196631`-era main with #11), #7 on `7-morph` (based on pre-squash #10 `0fab638`).
- Lesson, 2026-10-28: a rebase can succeed textually and still break the build: #6 and #10 both added a `slotsReceivingOn` (free function vs Engine member, the member hid the function), and #11's tests called the `setParameter` overload #6 removed. After every rebase, run the full plugin build and ctest before pushing; a sub-agent's green report predates the rebase.
- Lesson, 2026-10-28: sub-agent reports of known gaps are worth reading as bug reports: #10's "unsent Performance edits survive a Performance change" was a real defect, fixed with a test during the merge.
