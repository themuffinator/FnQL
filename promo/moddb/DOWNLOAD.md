# ModDB file listings

Mirror the two packages most people need. Upload the files exactly as
GitHub published them, so the SHA-256 checksums in the release's
`SHA256SUMS.txt` still match, and quote the checksum in each description.

| Listing | File | Category |
| --- | --- | --- |
| FnQL 0.1.0.158 for Windows | `fnql-0.1.0.158-20261116-28e575d7-windows-msvc-x86.zip` | Full Version |
| FnQL 0.1.0.158 dedicated server for Linux | `fnql-0.1.0.158-20261116-28e575d7-linux-x86.tar.gz` | Server |

The MinGW Windows build stays GitHub-only. Two Windows downloads on ModDB
would only make people wonder which one to pick.

Get the checksums with:

```bash
gh release download 0.1.0.158-20261116-28e575d7 -R themuffinator/FnQL -p SHA256SUMS.txt -O -
```

---

## Windows listing

**Title:** FnQL 0.1.0.158 for Windows

**Summary:** Open-source Quake Live engine, Windows x86 client package.
Extract into your Quake Live folder and run fnql.exe.

**Description:**

FnQL 0.1.0.158 for Windows (Win32/x86, MSVC build), 16 November 2026.

Requires a legitimate Steam copy of Quake Live. Launch the retail game once
first so Steam creates its normal files and your profile.

Install:

1. Extract the zip into your Quake Live folder (in Steam: right-click Quake
   Live → Manage → Browse local files).
2. Keep `FnQL-pkg.fnz`, `fnql-web.pak`, the renderer DLLs and the other
   packaged files beside `fnql.exe`.
3. Leave Steam running and launch `fnql.exe`.

The package contains FnQL's open-source engine, its WebUI overlay, the
open-source FnQL-game modules and a compiled Steam provider
(`fnql_steam.dll`). It contains no retail game content and no Valve
redistributables.

Release notes: https://github.com/themuffinator/FnQL/releases/tag/0.1.0.158-20261116-28e575d7

SHA-256: `<paste from SHA256SUMS.txt>`

---

## Linux server listing

**Title:** FnQL 0.1.0.158 dedicated server for Linux

**Summary:** Open-source Quake Live dedicated server for Linux i686. Retail
clients join it as normal.

**Description:**

FnQL 0.1.0.158 dedicated server for Linux (i686), 16 November 2026.

Requires a legitimate Steam copy of Quake Live for its game files.

Install:

1. Install the 32-bit runtime libraries that `ldd ./fnql.ded` reports.
2. `tar -xzf fnql-*-linux-x86.tar.gz -C "<Quake Live folder>"`
3. From that folder:
   `./fnql.ded +set fs_steampath "$PWD" +set dedicated 2 +map campgrounds ffa`

The i686 build loads retail `qagamei386.so` directly when you set
`vm_gameProvider retail`; by default it hosts the bundled open-source
FnQL-game module.

Release notes: https://github.com/themuffinator/FnQL/releases/tag/0.1.0.158-20261116-28e575d7

SHA-256: `<paste from SHA256SUMS.txt>`
