# GLx Rollback Package Metadata

> Historical record: this rollback format belonged to the retired
> `opengl`/`opengl2` promotion transition. Current releases may contain only the
> `glx`, `vk`, and `rtx` renderer modules; release packaging rejects every other
> renderer module name.

This document records the release metadata that was required before GLx could
replace `opengl`. It remains available for interpreting historical proof
artifacts and does not define a supported renderer alias today.

The metadata is a reviewed JSON file passed to release packaging with
`--glx-rollback-metadata <path>`. It is release-specific, so it should live with
the proof artifacts or release staging notes rather than being committed as a
static package promise.

## Schema

```json
{
  "version": 1,
  "status": "reviewed",
  "promotedRenderer": "glx",
  "aliasRenderer": "opengl",
  "migrationInstructions": "Use cl_renderer opengl for the promoted GLx alias, or cl_renderer glx to select GLx directly.",
  "rollbackInstructions": "Use the rollback package and set cl_renderer opengl followed by vid_restart to force the legacy OpenGL renderer.",
  "requiredArtifacts": {
    "proofCorpus": true,
    "promotionReport": true,
    "releaseProofSummary": true,
    "checksums": true
  },
  "rollbackTriggers": [
    "unexplained demo playback regression",
    "unexplained screenshot parity regression",
    "driver-specific startup or presentation regression",
    "confirmed performance regression outside the approved budget"
  ],
  "rollbackPackages": [
    {
      "id": "fnql-legacy-opengl",
      "type": "rollback",
      "artifactDir": "fnql-legacy-opengl",
      "platforms": ["windows-x86", "linux-x86"],
      "legacyRenderers": ["opengl", "opengl2"],
      "selectionInstructions": "Run this package when the promoted GLx alias regresses; set cl_renderer opengl and restart video."
    }
  ]
}
```

## Validation Rules

- `version` must be `1`.
- `status` must be `ready` or `reviewed`.
- `promotedRenderer` must be `glx`.
- `aliasRenderer` must be `opengl`.
- `migrationInstructions` and `rollbackInstructions` must both be present.
- `requiredArtifacts` must confirm the proof corpus, promotion report, release
  proof summary, and checksums are included in the release record.
- `rollbackTriggers` must cover demo, screenshot, driver, and performance
  regressions.
- `rollbackPackages` must include at least one `type: "rollback"` package that
  contains the legacy `opengl` renderer, names either `artifactDir` or `archive`,
  carries selection instructions, and covers every blocking release platform.

During archive creation, `scripts/release.py` also checks that each named
rollback `artifactDir` or `archive` matches an archive staged from the supplied
artifact root. The generated `.install/release-manifest.json` records the
validated metadata and the matched rollback archive checksums.

## Release Use

Unpromoted releases keep `RENDERER_DEFAULT=opengl`, so rollback metadata is
recorded as not required when omitted. Once maintainers intentionally promote
GLx defaults or alias behavior, `scripts/glx_promotion.py --require-ready` stays
blocked until this metadata validates, and `scripts/release.py --channel release`
will reject the package if the rollback archive is missing.
