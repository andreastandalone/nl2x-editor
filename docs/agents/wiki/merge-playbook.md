# Merge playbook

How the coordinating agent took a ticket from a sub-agent's branch to `main`, as practised on 2026-10-28 (tickets #2–#14). Sources: [autonomous-run-2026-10-28.md](autonomous-run-2026-10-28.md).

1. **Brief**: the sub-agent works in its own worktree under `.worktrees/<n>-<slug>` (gitignored), gets pointers to the issue, CODING_STANDARDS.md, this wiki and the files to read, the exact build/test/pluginval commands with the scratchpad download cache, and the rule to leave `docs/open-questions.md` and this wiki to the coordinator. Its report lists decisions as "question / choice / alternative / cost" and the human checks.
2. **Rebase before the first push** onto `main` (or onto a sibling branch still in review, to pre-resolve conflicts). Engine conflicts were almost always unions; editor layout and simulator conflicts needed the sub-agent who wrote the branch (resume it with SendMessage).
3. **After every rebase**: engine ctest, full Release plugin build, pluginval. A textually clean rebase broke the build twice (2026-10-28: a removed overload, a member hiding a free function).
4. **Docs**: the coordinator adds the decisions to the table and the human checks to "Andrea needed" in `docs/open-questions.md`, in one commit on the branch.
5. **PR** body says "Implements #N" (never "Closes #N" when human checks remain), push, wait for CI (macos-15, 8–25 min), squash-merge.
6. **Issue**: comment "Implemented in #PR, awaiting verification: …" pointing at the open-questions item, label `ready-for-human`, remove `ready-for-agent`, leave it open.
7. **Never force-push** (refused by the permission rules on 2026-10-28). A branch with an open PR gets `git merge main`, or is republished under a new name (as PR #27 replaced #26).
8. Read the "not done" list of each report as a bug list: on 2026-10-28 it surfaced a real defect in #10 and a race in #8, both fixed before merge.
