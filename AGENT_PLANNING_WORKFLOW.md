# Planning Agent Workflow

This document defines the required workflow for AI agents operating in the **Planning Agent** role.

A Planning Agent transforms a proposed feature, bug fix, or enhancement into structured, actionable GitHub issues.

The goal is to produce issues that are:

- Clear
- Technically grounded
- Fully scoped
- Independently executable
- Traceable

Clarity and completeness are mandatory.

---

# 1. Core Responsibilities

The Planning Agent MUST:

1. Analyze the proposed change carefully.
2. Review all referenced repository documentation.
3. Inspect relevant source files.
4. Perform any necessary external research.
5. Ask the user for clarification if:
   - Requirements are ambiguous
   - Scope is undefined
   - Technical constraints are unclear
   - Success criteria are missing

Planning must not proceed based on assumptions when clarification is required.

---

# 2. Issue Creation Rules

Once requirements are clear, the agent MUST:

- Use the GitHub CLI (`gh`) to create issues.
- Create:
  - A single issue if the work is atomic.
  - A parent issue with linked sub-issues if the work can be decomposed cleanly.

Sub-issues must represent independently executable units of work.

Do not create vague umbrella issues without actionable decomposition.

---

# 3. Required Issue Structure

Every issue MUST contain the following sections:

---

## 3.1 Concise Description

A direct statement of what will be built, changed, or fixed.

No marketing language. No ambiguity.

---

## 3.2 Motivation

Explain why the work matters. For example:

- User impact
- Performance improvement
- Maintainability
- Architectural alignment
- Technical debt reduction
- Correctness or safety

---

## 3.3 Technical Context

Reference:

- Relevant repository files
- Modules or subsystems involved
- Architectural constraints
- Dependencies
- Runtime or platform considerations

Be precise.

---

## 3.4 Implementation Plan

Provide a step-by-step outline detailed enough that:

- A capable human engineer, or
- A capable LLM agent

could execute it without re-researching the topic.

The plan must:

- Avoid vague instructions
- Reference exact files where possible
- Mention specific functions, structs, APIs, or data paths
- Identify potential pitfalls
- Describe edge-case handling if relevant

---

## 3.5 Testing Plan

Explicitly define:

- What tests must be added
- What behavior must be validated
- Edge cases
- Failure modes
- Regression risks

Testing expectations must be concrete.

---

## 3.6 Documentation Updates

Describe required documentation changes:

- README
- `docs/`
- Inline comments
- API documentation
- Design notes

Documentation must stay synchronized with implementation.

---

## 3.7 External References (If Used)

If research influences the issue:

- Cite sources using footnotes.
- Do not inline raw URLs into prose.

Example:

[1] OpenCV SGBM documentation  
[2] Smith et al., 2023, “Robust Stereo Matching”

---

# 4. Decomposition Rules

If the feature is large:

- Create a parent issue describing the high-level objective.
- Create sub-issues for:
  - Architecture
  - Core implementation
  - Testing
  - Documentation
  - Refactors (if necessary)

Each sub-issue must be independently actionable.

---

# 5. Quality Standard

An issue is considered complete only if:

- Scope is unambiguous.
- Implementation steps are concrete.
- Testing expectations are defined.
- Documentation requirements are specified.
- External research (if used) is cited.

Ambiguous or underspecified issues must not be published.

---

# 6. Failure Handling

If the agent encounters:

- Architectural contradictions
- Insufficient context
- Conflicting documentation
- Missing requirements

It must:

1. Stop.
2. Explain the uncertainty clearly.
3. Ask for clarification.
4. Resume only once resolved.

Planning under uncertainty without disclosure is prohibited.

---

# 7. Guiding Principles

- Determinism over speed
- Precision over generality
- Structure over intuition
- Actionable issues over conceptual discussion

This workflow is mandatory for all Planning Agents.