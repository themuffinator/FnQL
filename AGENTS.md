# AGENTS.md

## Mission

FnQL is Fappin' Quake Live: a modernized Quake Live engine derived from
FnQuake3 and guided by the reconstructed Quake Live source in QLSRP. The target
is retail Quake Live compatibility under a legitimate Steam installation. This
repository is engine-only; do not reconstruct or ship game code as part of FnQL.

Every change should protect these project constraints:

1. Retail Quake Live Steam compatibility is the compatibility target.
2. Game-code reconstruction is out of scope. Treat `qagame`, `cgame`, and UI
   code as ABI/reference boundaries unless the user explicitly asks otherwise.
3. QL behavior wins over Quake III behavior when QLSRP or retail evidence shows
   a real engine difference.
4. FnQ3 modernization work remains valuable: keep the renderer, audio, platform,
   packaging, tooling, and test improvements unless they conflict with QL.
5. Speed, determinism, and cross-platform viability matter, but not at the cost
   of compatibility-sensitive behavior.
6. Robustness and cross-platform support must be considered in every
   implementation, including narrow fixes. Prefer resilient error handling,
   explicit fallback behavior, and platform-conscious paths over assumptions
   that only hold on the current development machine.
7. The target engine should load retail Quake Live and interoperate in both
   directions: FnQL clients should join retail-operated protocol-91 servers,
   and retail Quake Live clients should join FnQL-hosted servers. Use the
   legitimate Steam session-ticket path when available. Without Steam, retain
   the retail-shaped fallback and report the remote server's authorization
   decision honestly; never synthesize authentication success.
8. Changes must not regress existing behavior in any aspect. Preserve working
   compatibility, features, performance, determinism, diagnostics, build
   configurations, release behavior, and supported-platform paths. If retail
   Quake Live evidence requires intentionally replacing incompatible behavior,
   document the evidence, scope the change narrowly, and add a regression gate
   for both the corrected behavior and unaffected paths.

## Reference Repositories

- `E:\Repositories\FnQuake3`: imported engine baseline. Remote:
  `https://github.com/themuffinator/FnQ3.git`. Import was taken from the local
  working tree on 2026-06-23 at commit
  `91c28d77878302ae67119fc3a29643cc20ce8489`; that source worktree had local
  uncommitted changes at import time.
- `E:\Repositories\QuakeLive-SRP`: QLSRP / Quake Live Source Reconstruction
  Project. Remote: `https://github.com/themuffinator/QL-SRP.git`. Initial
  reference point was commit `94bdd7acdce0c90bf890416e23e704795eac716e`;
  that reference worktree also had local uncommitted changes when this project
  was initialized.
- Retail Quake Live install: use the user's legitimate Steam installation as
  the runtime compatibility target, normally
  `C:\Program Files (x86)\Steam\steamapps\common\Quake Live`.

## Reconstruction Workflow

- Start with static comparison before changing behavior. Compare FnQL against
  QLSRP `src/code/`, the QLSRP reference corpus, and retail-observed behavior.
- Treat QLSRP as behavioral, protocol, file-format, and ABI evidence rather
  than as a source of implementation text. Anything derived from
  `../QuakeLive-SRP/` must be independently rewritten for FnQL: do not perform
  mechanical line-by-line ports, cosmetic renames, or preserve a QLSRP
  implementation structure merely because it already exists there.
- Design each QLSRP-informed rewrite around FnQL's current architecture. Prefer
  small typed interfaces, explicit ownership and bounds, deterministic parsing,
  resilient failure paths, and modern C++ where the surrounding subsystem and
  ABI permit it. Keep C-compatible boundaries where retail modules, legacy
  renderers, or platform APIs require them.
- A rewrite is not complete merely because it matches QLSRP. It must aim to
  gain or preserve compatibility with the legitimate retail Quake Live Steam
  runtime, and it must avoid regressing already-compatible retail behavior.
- Record the retail/QLSRP observation that motivates compatibility-sensitive
  constants and branches, then validate the independently written behavior with
  focused tests, fixture inspection, or a documented retail probe. When QLSRP
  and observed retail behavior disagree, retail behavior is authoritative.
- Keep observed facts separate from inferences in notes, commits, reviews, and
  implementation comments.
- Prefer small compatibility slices: filesystem/search path, Steam install
  discovery, protocol, module ABI, renderer data formats, audio behavior, and
  platform glue should be migrated independently.
- Preserve legacy Quake III license headers and upstream provenance comments.
  Rebrand project-owned code, docs, build outputs, packages, and helper names.
- If a QL feature depends on live online services, keep it explicit and
  default-off until there is a documented open replacement path.
- Never launch the game in fullscreen during automated or investigative runs.
  Use `+set r_fullscreen 0`, and choose the cheapest probe that answers the
  question.

## Project Plan

1. Import and rebrand FnQ3 into FnQL, preserving credits and build metadata.
2. Establish QL runtime identity: Steam app id/path discovery, default basepath
   expectations, executable naming, package naming, and generated docs.
3. Audit QLSRP versus FnQL subsystem by subsystem:
   filesystem and pak loading, QL BSP/material handling, native/VM module ABI,
   protocol and demo paths, server lifecycle, renderer contracts, audio, input,
   and platform services.
4. Reconstruct required QL engine compatibility in modern C++ while keeping
   FnQ3 modernization where it remains compatible.
5. Define and automate retail-Steam validation gates. Runtime validation should
   mount retail QL assets and prove compatibility without bundling those assets.
6. De-scope, isolate, or remove inherited game-code paths once equivalent QL
   engine/module boundaries are proven.

## Repository Rules

- Treat demo, protocol, asset-loading, filesystem, Steam path, and VM/native
  module behavior as compatibility-sensitive by default.
- Consider robustness and cross-platform behavior for every implementation.
  Windows, Linux, macOS, and legacy build paths may need different probes,
  fallbacks, or validation, even when the immediate test environment is Windows.
- Prefer incremental engine changes over broad rewrites unless a rewrite is the
  only coherent fix.
- Keep release packaging deterministic. `.install/` is the staged distribution
  area, not a scratchpad.
- Use `.tmp/` for temporary outputs, investigation notes, and disposable staging
  work.
- Keep user guidance in `README.md` and deeper maintainer material in linked
  technical docs.
- When versioning changes are required, update the canonical metadata in
  `version/fnql_version.h` first.
- Treat Meson `subprojects/*.wrap` files as the ownership boundary for bundled
  third-party dependencies. Do not restore deleted in-tree vendor directories
  such as `code/libcurl/`, `code/libjpeg/`, `code/libogg/`, `code/libvorbis/`,
  `code/libsdl/`, or `code/openal/`.
- Legacy Makefile builds may use system development packages for dependencies,
  but they must not grow new private copies of third-party source trees. If a
  dependency needs a bundled fallback, wire it through Meson `subprojects/`.

## Local References

- `README.md`: project overview and current user-facing status.
- `BUILD.md`: platform-specific build instructions.
- `CREDITS.md`: upstream lineage, imported baselines, and acknowledgements.
- `docs/fnql/TECHNICAL.md`: maintainer-facing project, release, and repo notes.
- `version/fnql_version.h`: single source of truth for project version metadata.
- `scripts/version.py`: version/channel helper for humans and CI.
- `scripts/generate_docs.py`: refreshes `README.md` and `.install/README.html`
  from templates.
- `scripts/release.py`: stages artifacts through `.install/` and produces
  release archives plus manifests.
- `subprojects/*.wrap`: Meson fallback definitions for third-party dependencies
  such as SDL3, OpenAL Soft, libcurl, libjpeg-turbo, Ogg, and Vorbis.
- `.github/workflows/release.yml`: main-branch build validation and manual
  release publishing.

## Directory Map

- `.install/`: tracked distribution docs plus generated package outputs during
  release staging.
- `.tmp/`: ignored scratch workspace for temporary files and intermediate
  staging.
- `code/`: engine, renderer, platform, VM/module ABI, and inherited reference
  sources.
- `docs/`: maintainer docs, legacy upstream docs, migration notes, and template
  sources.
- `pkg/`: FnQL data-only sidecar package sources; do not put retail assets here.
- `subprojects/`: Meson wrap definitions and fetched fallback dependency source
  directories.
- `version/`: canonical version metadata consumed by code, docs, and CI scripts.
- `scripts/`: repo-local automation for docs, versioning, packaging, and
  verification.

## Release Workflow

1. Update `version/fnql_version.h` for the next tagged release.
2. Run `python scripts/generate_docs.py` to refresh generated user-facing docs.
3. Build platform artifacts.
4. Run `python scripts/release.py --channel manual` or
   `python scripts/release.py --channel release --ref-name <tag>` against the
   downloaded artifact directory.
5. Publish the archives produced under `.install/packages/` with the generated
   manifest and checksums.

## Guardrails

- Every change must aim to gain or maintain retail Quake Live compatibility.
- Write efficient, robust, safe, and modern code, subject to the project's
  compatibility-sensitive boundaries and supported toolchains.
- Seek worthwhile modernizations and improvements, provided they preserve the
  retail compatibility target and do not weaken established behavior.
- Preserve FnQL's existing strengths and improvements. Changes must be
  non-regressive across compatibility, features, performance, determinism,
  diagnostics, build configurations, release behavior, and supported platforms.
- Treat non-regression as a completion requirement, not a follow-up. Before a
  compatibility slice is considered complete, compare its affected behavior
  against the current FnQL baseline and run proportionate build, test, static,
  and retail-fixture checks. Do not remove or silently weaken an existing path
  merely because the QLSRP reference lacks FnQL's modernization.
- If a change touches runtime identity strings, keep compatibility-sensitive
  behavior unchanged unless the user explicitly wants a compatibility break.
- If you have to choose between a cleaner abstraction and a safer
  compatibility-preserving patch, default to compatibility and document the
  tradeoff.
- When release packaging changes, ensure `.install/README.html` remains valid
  and the package still includes `LICENSE` and `docs/fnql/TECHNICAL.md`.
- When build or CI changes need third-party libraries, prefer Meson dependency
  declarations with `dependency(..., fallback: ...)` and the existing
  `subprojects/*.wrap` files. Make and legacy project files should either
  consume system packages or delegate to Meson; they should not point at
  removed `code/lib*` dependency trees.
