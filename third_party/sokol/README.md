# sokol (vendored)

`sokol_gfx.h` and `sokol_log.h` from [floooh/sokol](https://github.com/floooh/sokol), used by the sokol rendering backend (`WEBAR_RENDERER=sokol`, `src/core/SokolRenderer.*`). Nothing else from the repository is used, so only these two headers are vendored here; sokol has no release tags to pin a submodule to.

- Upstream: https://github.com/floooh/sokol
- Commit: `93aa187b5d387cc24b7290b3ca3f13f61e139e7c` ("Merge pull request #1648 from floooh/raise-sampler-limit")
- Commit date: 2026-11-10
- Copied: 2026-11-16, unmodified
- License: zlib/libpng, Copyright (c) Andre Weissflog. The full text is at the end of each header.

## Updating

```bash
git clone --depth 1 https://github.com/floooh/sokol.git /tmp/sokol   # any folder outside this repository
cp /tmp/sokol/sokol_gfx.h /tmp/sokol/sokol_log.h third_party/sokol/
git -C /tmp/sokol log -1 --format='%H %cs %s'
```

Then record the new commit and date above, build with `node build-docker.js --renderer sokol --test`, and check the sokol demo in the browser: sokol's API changes between commits (for example the texture views, `sg_view`, of late 2025).
