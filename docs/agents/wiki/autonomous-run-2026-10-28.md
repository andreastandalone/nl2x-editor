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
