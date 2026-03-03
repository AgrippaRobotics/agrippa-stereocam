# Agent Guidance

## Line endings
This repository has no Windows support (macOS and Linux only).  
There must never be a CRLF line ending in any file. Do not introduce them.

---

## Documentation System

This repository uses a dedicated documentation system instead of treating the root `README.md` as the full manual.

Canonical docs live in:

- `docs/`
- `mkdocs.yml`
- `.github/workflows/docs.yml`
- `requirements-docs.txt`

The published docs target is GitHub Pages, built with MkDocs through GitHub Actions.

---

## Where To Look During Agentic Coding

When implementing or changing features, check these locations first:

- `docs/index.md` for the overall docs entrypoint
- `docs/cli/` for command behavior and user-facing flags
- `docs/workflows/` for calibration, bring-up, testing, and operator workflows
- `docs/backends/` for ONNX and stereo backend setup
- `README.md` for the short GitHub-facing summary and docs links

Treat the docs site as the canonical user documentation. The README is only the front door.

---

## Documentation Maintenance Rule

Keep the docs current when building new features.

If you add or change:

- a CLI command,
- a CLI flag,
- a workflow,
- a backend capability,
- or a hardware-facing behavior that users or operators need to know about,

then update the corresponding page under `docs/` in the same change.

---

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

---

# Agent Role Routing

This repository uses role-specific workflows for AI agents.

Before proceeding with any task, you MUST determine which role you are operating under.

---

## Planning Agent

You are a **Planning Agent** if:

- You are asked to think through a new feature, bug fix, or enhancement.
- You are asked to scope or decompose work.
- You are preparing GitHub issues.
- You are performing research prior to implementation.

If this describes your task:

➡ Read and strictly follow:  
`AGENT_PLANNING_WORKFLOW.md`

Do not begin implementation work.  
Your output should result in well-structured GitHub issue(s).

---

## Implementation Agent

You are an **Implementation Agent** if:

- You are executing work described in a GitHub issue.
- You are writing code.
- You are modifying tests.
- You are preparing a pull request.
- You are refactoring existing code.

If this describes your task:

➡ Read and strictly follow:  
`AGENT_IMPLEMENTATION_WORKFLOW.md`

Do not redefine scope unless explicitly instructed.  
Your work must remain traceable to the issue.

---

## If Role Is Unclear

If you cannot determine your role:

1. Stop.
2. Ask for clarification.
3. Do not assume.

---

## Working Expectation

Feature work is not complete until:

- Code is correct.
- Tests pass (or failures are explicitly documented).
- Documentation is updated in `docs/`.
- Changes are traceable to a GitHub issue.
- The appropriate workflow document has been followed.

All agent contributions must be:

- Structured
- Deterministic
- Tested
- Documented
- Traceable

No exceptions.