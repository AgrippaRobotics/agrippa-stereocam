# Documentation Migration Plan

## Recommendation

Use **GitHub Pages** for hosting, deployed by **GitHub Actions**, with **MkDocs** as the documentation generator.

This is the best fit for this repository because:

- The current documentation is already mostly Markdown, so migration cost is low.
- GitHub Pages can publish any static site built in Actions; it is not limited to Jekyll.
- MkDocs is Python-based, simple to maintain, and optimized for Markdown-heavy product and engineering docs.
- Sphinx is a strong alternative, but it adds more authoring and configuration overhead than this repo currently needs.

## Decision Summary

Adopt this stack:

- Hosting: GitHub Pages
- Build/deploy: GitHub Actions
- Generator: MkDocs
- Theme: Start with the built-in `mkdocs` or `readthedocs` theme; only add heavier theming after the content structure is stable
- Source format: Markdown in a dedicated `docs/` tree

Do **not** keep expanding the root `README.md` into a full manual. Convert it into a short landing page that links into the docs site.

## Why This Option Wins

### Chosen: MkDocs

MkDocs is the lowest-friction path from the repo's current state to a scalable docs site:

- It treats Markdown as the primary authoring format.
- Navigation is explicit and easy to control in `mkdocs.yml`.
- It works cleanly with GitHub Pages via a build workflow.
- It is well suited to feature documentation, bring-up guides, testing docs, calibration workflows, and CLI guides.

### Not Chosen: Sphinx

Sphinx would make sense if the main requirement were:

- generated API reference from source comments,
- complex cross-reference domains,
- PDF/manpage pipelines,
- or a larger multi-version docs program.

That is not the immediate problem here. The immediate problem is that feature and workflow documentation has outgrown the README, and the existing docs are already Markdown-first.

### Not Chosen: Jekyll

Jekyll is tightly associated with GitHub Pages, but it is not the best fit here:

- it is Ruby-based rather than Python-based,
- it is less aligned with the repo's existing Markdown-docs-as-product-docs workflow,
- and it offers less value than MkDocs for a straightforward engineering manual.

## Current Documentation Situation

The repo already has documentation content that should become first-class docs pages:

- `README.md`
- `TESTING.md`
- `bringup_guide.md`
- `focus_audio_interface.md`
- `backends/IGEV_SETUP.md`
- `CIRCUITS.md`
- `binning_bug_fix.md`

The root `README.md` is currently doing too many jobs at once:

- project overview,
- install/build instructions,
- CLI reference,
- feature guide,
- calibration notes,
- backend notes.

That is the right signal to move to a docs site.

## Target Information Architecture

Create a dedicated `docs/` directory and move toward this structure:

```text
docs/
  index.md
  getting-started/
    overview.md
    installation.md
    quickstart.md
  cli/
    overview.md
    list.md
    connect.md
    capture.md
    stream.md
    focus.md
    calibration-capture.md
    depth-preview-classical.md
    depth-preview-neural.md
    calibration-stash.md
  workflows/
    bring-up.md
    calibration.md
    testing.md
    focus-audio.md
  backends/
    overview.md
    igev-setup.md
  hardware/
    circuits.md
  development/
    architecture.md
    changelog-notes.md
```

## What Stays in `README.md`

Reduce `README.md` to:

- project purpose,
- one-screen build/install summary,
- minimal quick start,
- link to hosted docs,
- link to contribution/testing sections,
- optionally one compact command summary table.

Keep the README useful on GitHub, but stop treating it as the canonical manual.

## Migration Plan

### Phase 1: Establish the docs skeleton

1. Add `docs/` and `mkdocs.yml`.
2. Create top-level sections for Getting Started, CLI, Workflows, Backends, Hardware, and Development.
3. Add a landing page at `docs/index.md` that explains what the project does and links to major tasks.
4. Keep all original Markdown files in place until their content has been migrated and linked.

### Phase 2: Split the README by responsibility

Move content out of `README.md` into docs pages in this order:

1. Build/install content to `docs/getting-started/installation.md`
2. Command overview to `docs/cli/overview.md`
3. Per-command usage blocks to one page per command under `docs/cli/`
4. Calibration and backend notes to `docs/workflows/calibration.md` and `docs/backends/`
5. Testing content to `docs/workflows/testing.md`

After each move, replace the removed README section with a short summary and a link to the docs page.

### Phase 3: Migrate existing standalone docs

Map current files into the new site:

- `TESTING.md` -> `docs/workflows/testing.md`
- `bringup_guide.md` -> `docs/workflows/bring-up.md`
- `focus_audio_interface.md` -> `docs/workflows/focus-audio.md`
- `backends/IGEV_SETUP.md` -> `docs/backends/igev-setup.md`
- `CIRCUITS.md` -> `docs/hardware/circuits.md`
- `binning_bug_fix.md` -> either `docs/development/changelog-notes.md` or a narrower troubleshooting page

Do not blindly copy large files. Normalize them while migrating:

- remove duplicated context,
- add page titles and short intros,
- standardize headings,
- add cross-links between related pages,
- move incidental implementation notes out of user-facing guides when needed.

### Phase 4: Add GitHub Pages publishing

Set up GitHub Pages using Actions:

1. Add a workflow that installs Python and MkDocs.
2. Build the site from the repo on every push to the default branch.
3. Publish the built static site as the GitHub Pages artifact.
4. Configure the repository Pages source to use GitHub Actions.

This avoids branch-based publishing hacks and keeps generated site output out of version control.

### Phase 5: Add docs quality controls

Add lightweight guardrails:

- local preview command: `mkdocs serve`
- CI build command: `mkdocs build --strict`
- markdown link checking if it remains low-noise
- ownership expectation that new features ship with docs updates

Use a minimal plugin footprint at first. Complexity should follow need, not lead it.

### Phase 6: Define documentation ownership rules

Adopt these working rules:

- Every user-facing feature gets a docs page or an update to an existing page.
- New CLI flags must be documented in the matching command page.
- Workflow docs should prefer task-based guidance over dumping raw option lists.
- The README is a front door, not the full manual.
- Troubleshooting belongs in docs, not scattered issue-style markdown files at repo root.

## Proposed Initial Navigation

Suggested first-pass `mkdocs.yml` nav:

```yaml
site_name: Agrippa Stereo Camera
theme:
  name: readthedocs
nav:
  - Home: index.md
  - Getting Started:
      - Overview: getting-started/overview.md
      - Installation: getting-started/installation.md
      - Quick Start: getting-started/quickstart.md
  - CLI:
      - Overview: cli/overview.md
      - list: cli/list.md
      - connect: cli/connect.md
      - capture: cli/capture.md
      - stream: cli/stream.md
      - focus: cli/focus.md
      - calibration-capture: cli/calibration-capture.md
      - depth-preview-classical: cli/depth-preview-classical.md
      - depth-preview-neural: cli/depth-preview-neural.md
      - calibration-stash: cli/calibration-stash.md
  - Workflows:
      - Bring-Up: workflows/bring-up.md
      - Calibration: workflows/calibration.md
      - Testing: workflows/testing.md
      - Focus Audio: workflows/focus-audio.md
  - Backends:
      - Overview: backends/overview.md
      - IGEV Setup: backends/igev-setup.md
  - Hardware:
      - Circuits: hardware/circuits.md
```

This is intentionally simple. Do not overdesign taxonomy until the first migration pass is complete.

## Repository Changes To Make Later

When implementing this plan, expect to add:

- `mkdocs.yml`
- `docs/`
- `docs/requirements.txt` or `requirements-docs.txt`
- `.github/workflows/docs.yml`

Optional later additions:

- `docs/stylesheets/extra.css`
- `docs/javascripts/`
- `docs/assets/`

## Content Strategy

Structure pages by user task, not by source file location.

Good examples:

- "Install and build"
- "Capture a stereo frame"
- "Run live stream with rectification"
- "Calibrate and load remap tables"
- "Upload calibration to camera"

Avoid docs that mirror internal file names unless the audience is maintainers.

## Risks And Mitigations

### Risk: README and docs diverge

Mitigation:

- Make docs the canonical source.
- Keep README intentionally short.
- Replace duplicated blocks with links.

### Risk: Too many plugins create maintenance drag

Mitigation:

- Start with core MkDocs only.
- Add plugins only for a demonstrated need.

### Risk: Migration stalls halfway

Mitigation:

- Migrate by section, not by whole-site perfection.
- Publish early once the first useful docs slice is live.

### Risk: Engineering notes clutter user docs

Mitigation:

- Separate user workflows from maintainer/development notes.
- Add a `development/` section for internal details.

## Acceptance Criteria

The migration is successful when:

1. GitHub Pages serves a docs site from the default branch via Actions.
2. The root README is short and links to the hosted docs.
3. Each CLI command has its own docs page.
4. Existing standalone guides are migrated into the docs navigation.
5. The docs build passes in CI with strict mode enabled.
6. New feature work has an obvious place to document itself without growing the README.

## Recommended Rollout Order

Follow this order:

1. Add MkDocs skeleton and local build
2. Publish a minimal site through GitHub Pages
3. Move install + quick start docs
4. Split CLI docs into per-command pages
5. Migrate testing, bring-up, calibration, and backend docs
6. Shrink README to a landing page
7. Add strict CI checks

## Source Notes

This recommendation is based on:

- GitHub Pages official documentation for publishing with GitHub Actions and custom static site generators: https://docs.github.com/en/pages
- MkDocs official documentation, including GitHub Pages deployment guidance: https://www.mkdocs.org/

Those sources support the core deployment model: build a static site with a generator such as MkDocs, then publish it to GitHub Pages through GitHub Actions.
