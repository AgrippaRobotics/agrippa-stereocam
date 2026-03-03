# Implementation Agent Workflow

This document defines the required workflow for AI agents operating in the **Implementation Agent** role.

An Implementation Agent executes work defined in a GitHub issue.

The goals are:

- Clean execution
- Traceable reasoning
- Incremental commits
- Meaningful tests
- Accurate documentation

---

# 1. Initial Procedure

Before writing code, the agent MUST:

1. Read the GitHub issue thoroughly.
2. Check for sub-issues.
3. Review all referenced documentation.
4. Inspect relevant source files.

Then respond with:

- A concise summary of the goal.
- A high-level implementation plan.
- A confidence statement:
  - High confidence
  - Moderate confidence
  - Low confidence (with explanation)

If the task is infeasible or underspecified, the agent must state that clearly before proceeding.

---

# 2. Branching Rules

The agent MUST:

- Create a new branch.
- Use descriptive naming:
  - `feature/<short-name>`
  - `fix/<short-name>`
  - `refactor/<short-name>`

Direct commits to `main` are prohibited.

---

# 3. Implementation Discipline

Work must be divided into logical sub-goals.

Each sub-goal should produce:

- A focused commit
- A descriptive commit message
- A reference to the GitHub issue number

Commit messages must explain:

- What changed
- Why it changed
- Tradeoffs (if applicable)

Example:

```
Add Laplacian focus metric computation

Implements focus metric described in #42.
Uses 3x3 kernel and normalized variance metric.
```

If tests are expected to fail temporarily, this must be explicitly stated in the commit message.

---

# 4. GitHub Issue Updates

The agent should comment on the issue when:

- Discovering new constraints
- Deviating from the original plan
- Identifying architectural concerns
- Encountering blockers
- Completing major milestones

Major design deviations require explicit justification.

Silent divergence from the plan is not allowed.

---

# 5. Testing Policy

Testing is mandatory but must be purposeful.

The agent MUST:

1. Detect the repository’s testing framework.
2. Follow existing conventions.
3. Add tests when:
   - New functionality is introduced
   - A bug is fixed
   - Logic is non-trivial
4. Avoid:
   - Redundant tests
   - Cosmetic tests
   - Tests without behavioral verification

Tests must:

- Be deterministic
- Avoid unnecessary external dependencies
- Clearly describe expected behavior

Tests must be executed before committing.

---

# 6. Pull Request Requirements

Once implementation is complete, the agent MUST:

1. Push the branch.
2. Open a Pull Request using `gh`.
3. Reference the issue.
4. Include:

   - Summary of changes
   - Testing performed
   - Relevant logs/output if applicable
   - Known limitations (if any)

PR descriptions must be concise and technical.

---

# 7. Documentation Requirements

Before concluding work, the agent must determine:

- Where documentation resides (README, `docs/`, generated docs, etc.)
- What needs updating

Documentation must:

- Be concise
- Avoid marketing language
- Explain intent, not just usage
- Remain synchronized with behavior

---

# 8. Research and Citations

If external research influenced implementation:

- Cite sources in:
  - Issue comments
  - PR description
  - Commits (if relevant)

Use footnote format.

Avoid inline raw URLs in prose.

---

# 9. Failure and Recovery

If the agent encounters:

- Architectural contradictions
- Incomplete requirements
- Broken test infrastructure
- Missing dependencies

It must:

1. Stop.
2. Explain the issue clearly.
3. Propose possible solutions.
4. Wait for direction.

Proceeding under uncertainty without disclosure is prohibited.

---

# 10. Guiding Principles

- Determinism over speed
- Small commits over large rewrites
- Traceability over intuition
- Tests over assumptions
- Documentation over tribal knowledge

This workflow is mandatory for all Implementation Agents.