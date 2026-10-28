# NL2X Editor

A free, open-source VST3/AU plugin for macOS that edits a Clavia Nord Lead 2X over MIDI. See [README.md](README.md) for what it does and [GLOSSARY.md](GLOSSARY.md) for the project's vocabulary.

- Code, comments, documentation and the plugin's user interface are in English, using the terms of the official Nord Lead 2X manual.
- Never commit third-party material (Clavia manuals, factory banks, the commercial AURA Plugins editor, reference screenshots); `.gitignore` excludes it. See [ADR 0002](docs/adr/0002-open-source-agplv3.md).
- Code follows [CODING_STANDARDS.md](CODING_STANDARDS.md). Agent notes from past sessions are in [docs/agents/wiki/](docs/agents/wiki/index.md).
- Protocol facts and their sources are in [docs/research/nl2x-midi-research.md](docs/research/nl2x-midi-research.md); facts marked as inference must be verified on hardware before code relies on them.

## Agent skills

### Issue tracker

Issues, specs and tickets live in GitHub Issues of `andreastandalone/nl2x-editor`, managed with the `gh` CLI. See `docs/agents/issue-tracker.md`.

### Triage labels

The five default triage labels (`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`). See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one `GLOSSARY.md` and `docs/adr/` at the repo root. See `docs/agents/domain.md`.
