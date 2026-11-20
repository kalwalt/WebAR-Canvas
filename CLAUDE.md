# Claude Code Instructions

**[AGENTS.md](AGENTS.md)** is the canonical guide for this repository (architecture, build, rules, verification): read it before changing code, and put new guidance there, not here. This file is only the day-to-day fast path.

## Fast Path

- **Build**: `node build-docker.js` (Filament, default) or `node build-docker.js --renderer sokol` (into `build-wasm-sokol/`, no Filament, no host tools); `-j 8`, `--clean`; `--debug` for a `-g` development build (the default is the published one, #23)
- **Tests**: `node build-docker.js --test`; the runners use no renderer, so `--renderer sokol --test` runs them in ~3 min without the Filament host tools. Runner checks: `node --test scripts/build-docker.test.js scripts/check-wasm-debug.test.js .claude/hooks/guard-submodules.test.js`
- **Build times**: first Filament build in a checkout ~15–30 min (host tools, `uberz` alone ~13 min), then ~3–10 min; sokol ~3 min. Run builds in the background; never two builds on one build folder
- **Dry run**: `node build-docker.js [--renderer sokol] --dry-run` prints the container commands after the checks, without Docker
- **Worktrees**: `git -c core.autocrlf=false submodule update --init --reference <main>/third_party/<sub> -- third_party/<sub>`, then `git -C third_party/filament config core.autocrlf false`; built host tools can be copied between checkouts of the same Filament commit (AGENTS.md section 4)
- **Serve**: `python -m http.server 8000 --directory build-wasm` (or `build-wasm-sokol`), open `http://localhost:8000/videoTest.html`; `/verify-wasm` runs the whole check
- **Review**: the `wasm-reviewer` agent (`.claude/agents/`) checks a diff against the AGENTS.md rules before a PR

## Key Rules

1. **Pure C++ engine**: no Embind in engine classes; `-fno-rtti`; no Filament or sokol types in `ARBaseEngine` (AGENTS.md section 2).
2. **Docker build only**: `emscripten/emsdk:4.0.17` through `build-docker.js`.
3. **CMake**: `"--embed-file=<src>@<dst>"` with an equal sign; JS-called functions and runtime methods go in `EXPORTED_FUNCTIONS` / `EXPORTED_RUNTIME_METHODS`.
4. **Conventional Commits** with `feat`, `fix`, `perf`, `doc`, `refactor`, `test`, `style`, `chore`; no `revert` type (a revert gets an allowed type, e.g. `perf(engine): go back to ...`).
5. **Branches from `dev`, PRs to `dev`**; `master` is for releases.
6. **Pages that load `videoTest.js`**: Emscripten defines globals (`run`, `Module`, `FS`); put page code in an IIFE and only set `Module`.

## Browser Checks

- The built-in browser blocks the camera: use a synthetic `captureStream()` video source.
- A hidden or covered window stops `requestAnimationFrame` (Playwright's Chrome drops to 1 Hz): keep it uncovered, discard runs with too few rAF ticks.
- Compare builds in the same browser, alternating runs; read the pose as the mean over a run (single lines jitter by ±4 mm).
- Playwright screenshots take ~4 s and can show a stale frame: confirm a state change over several captures.
