# AI-Assisted Development Guidelines

This document sets expectations for responsible AI use in this repository.

- Edit this file when team standards for AI-assisted workflows evolve.

## Core principle

AI is a productivity tool, not an authority. Humans own requirements, architecture decisions, and final verification.

## Required guardrails

1. **Start from human intent**
   - Define goal and acceptance criteria before asking AI to implement.

2. **Keep changes scoped**
   - Request small, reviewable increments.
   - Avoid broad speculative refactors.

3. **Demand explicit reasoning**
   - Ask why a change is needed and what trade-offs it introduces.

4. **Validate every non-trivial change**
   - Run automated tests for affected components.
   - Perform manual checks for UI behavior.

5. **Document decisions**
   - Update requirements/design/testing docs when behavior or workflow changes.

6. **No blind copy-merge**
   - Review code and documentation as if written by a teammate.

## Anti-patterns to avoid

- "Vibe-coding" without acceptance criteria
- merging code without running tests
- treating AI output as correct by default
- letting prompts replace architecture thinking

## Student-friendly example workflow

1. Write a one-paragraph problem statement.
2. List acceptance criteria.
3. Ask AI for a minimal implementation plan.
4. Implement one slice.
5. Run tests/manual checks.
6. Capture what changed and why.
7. Repeat.

This is the expected standard for "AI dev" in this project.
