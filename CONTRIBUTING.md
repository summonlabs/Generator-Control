# Contributing to Generator Control

Thank you for your interest in contributing to Generator Control. This project
is published by Summon Software Labs under the Apache License, Version 2.0.

## License and copyright

By contributing, you agree that your contributions will be licensed to the
project under the Apache License, Version 2.0, consistent with the LICENSE and
NOTICE files in this repository. There is no Contributor License Agreement
(CLA) and no copyright assignment: you retain copyright in your contribution and
grant the project a perpetual, worldwide, non-exclusive, royalty-free license to
use it under the terms of the Apache License, Version 2.0.

## Attribution

Please do not remove or alter the copyright notice or the NOTICE file. Any
distribution of derivative works must carry the attribution notices contained in
the NOTICE file, as required by Section 4 of the Apache License.

## Getting started

- Open a pull request against `main`. Keep changes focused.
- Build and test locally before pushing:
  - Release: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`, then
    `cmake --build build`.
  - Tests: `ctest --test-dir build --output-on-failure` or run the test binary
    directly.
  - Debug: `cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
- The suite must pass with zero compiler warnings (`/W4 /WX /permissive-` on MSVC).

## Engineering conventions

- Use the strongly typed identity, generation, epoch and unit models in
  `include/genctl`; do not add raw integers for identities or untyped floating
  point values for electrical, fuel or timing quantities.
- Every state dependent mutation must carry the authority and revision it was
  planned against, and must refuse stale authority rather than merging it.
- Never collapse command acknowledgement into an electrical effect. Actuation is
  only ever proven by an observation with non-command provenance.
- Missing, stale, unsupported and contradictory evidence must fail closed; it is
  never promoted to permission and never silently converted into zero.
- New decision logic must be deterministic: identical inputs must produce
  identical canonical bytes, identical digests and identical primary errors.
- Add a regression test for every defect and every new behavior. Tests are proof
  obligations, not decoration.

## Code of conduct

Be respectful and constructive. Reviews focus on the code and its behavior.
