---
name: raidline-cpp-safety-review
description: Review Project Raidline C++ changes for correctness and safety involving RAII, raw resources, unique ownership, move-only types, copy/move operations, stable IDs, vector invalidation, const, noexcept, nodiscard, invariants, state transitions, transactions, and compile or link failures. Use during implementation review or C++ failure diagnosis; keep the review read-only unless a separate fix is requested.
---

# Raidline C++ Safety Review

## Gather evidence

1. Read `AGENTS.md`, `doc/architecture/ARCHITECTURE.md`, `doc/architecture/INVARIANTS.md`, the relevant active ExecPlan, and the full diff.
2. Trace construction, mutation, destruction, error, and test paths. Check the CMake target that compiles each changed source.
3. Compare behavior against existing tests and documented contracts, not naming or comments alone.

## Review in risk order

### Ownership and lifetime

- Identify the owner of each SDL/resource handle and confirm destruction order.
- Confirm move-only objects cannot be accidentally copied and moved-from states are safe and explicit.
- Flag dangling pointers, references, spans, iterators, or views after `std::vector` growth, erase, reordering, or object destruction.
- Prefer stable IDs across container mutation; do not treat an index as identity.

### Invariants and transactions

- Verify constructors establish valid state or reject invalid inputs before dangerous arithmetic.
- Verify queries are side-effect free and commands validate fully before mutation.
- Verify failed placement, pickup, move, or transfer leaves every participant unchanged, including ownership and stable IDs.
- Verify a successful command updates all redundant representations consistently.

### C++ contracts

- Check `const` boundaries, value/reference/pointer return choices, and accidental mutable exposure.
- Check `noexcept` only where every executed operation is non-throwing.
- Check `[[nodiscard]]` results are consumed or intentionally cast to `void`.
- Check copy/move special members, self-move assumptions, and exception-safety claims against actual operations.
- Check enum/state-machine transitions, cancellation, outside/boundary events, and unreachable or sticky states.

### Build and linkage

- Confirm declarations have one matching definition and each test links required implementations.
- Detect duplicate global type names, ambiguous same-named headers, include-order dependence, and hidden transitive includes.
- Distinguish compiler diagnostics from linker errors and runtime failures.

## Report

Lead with actionable findings ordered by severity. For each finding, include a tight file/line location, violated contract, concrete failure scenario, and smallest safe direction. Avoid style-only findings.

If no actionable finding remains, say so and list residual risks such as unrun platforms, untested exception paths, or manual UI validation. Do not edit code unless the user or primary workflow separately authorizes implementation.
