# overhead/ — placeholder (spec §10.2, DEFERRED)

The Overhead sky-dashboard firmware currently lives at the **repository root** (`src/`,
`platformio.ini`, `docs/`, …) — it is NOT inside this directory, and the rotor task does not
touch it.

This directory is a placeholder for the monorepo layout in spec §3, where the dashboard would
move under `overhead/` alongside `rotor/`, `radio/`, and `shared/`. That relocation — and the
sender side that broadcasts `shared/telemetry.h` at ~2 Hz — is **deferred to integration
(§10.1, §10.2)**.
