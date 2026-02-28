# Agent Guidance

## Line endings
This repository has no Windows support!(just macos and linux) there should never be a CRLF line ending in any file, so dont write them.

## Documentation System

This repository uses a dedicated documentation system instead of treating the root `README.md` as the full manual.

Canonical docs live in:

- `docs/`
- `mkdocs.yml`
- `.github/workflows/docs.yml`
- `requirements-docs.txt`

The published docs target is GitHub Pages, built with MkDocs through GitHub Actions.

## Where To Look During Agentic Coding

When implementing or changing features, check these locations first:

- `docs/index.md` for the overall docs entrypoint
- `docs/cli/` for command behavior and user-facing flags
- `docs/workflows/` for calibration, bring-up, testing, and operator workflows
- `docs/backends/` for ONNX and stereo backend setup
- `README.md` for the short GitHub-facing summary and docs links

Treat the docs site as the canonical user documentation. The README is only the front door.

## Documentation Maintenance Rule

Keep the docs current when building new features.

If you add or change:

- a CLI command,
- a CLI flag,
- a workflow,
- a backend capability,
- or a hardware-facing behavior that users or operators need to know about,

then update the corresponding page under `docs/` in the same change.

## Do Not Scatter Random Markdown Files

Do not drop ad hoc `.md` files into the repo root for new feature documentation.

Prefer:

- extending an existing page in `docs/`,
- adding a new page under the appropriate `docs/` section,
- and linking it in `mkdocs.yml`.

The only Markdown files that should remain at repo root are files with a clear top-level purpose, such as:

- `README.md`
- `AGENTS.md`
- project policy, licensing, or citation files

If a new note is important enough to keep, it should usually become part of the docs system rather than a standalone root-level note.

## Working Expectation

Feature work is not complete until the documentation is in a maintainable place.
