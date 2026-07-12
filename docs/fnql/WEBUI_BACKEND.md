# WebUI runtime backend boundary

FnQL presents the retail Quake Live WebUI on Windows x86 by adapting the
Awesomium runtime from the user's legitimate retail installation. FnQL does
not bundle Awesomium, copy its runtime into packages, import proprietary SDK
headers, or reproduce C++ object layouts. Other platforms and x64 builds keep
the deterministic native-UI fallback.

The Windows x86 default is enabled because retail QL ships only 32-bit game
modules and Awesomium. `cl_webuiEnable 0` remains an explicit opt-out. Runtime
discovery is rooted in the configured QL base/home and executable paths and
fails closed if `awesomium.dll`, `awesomium_process.exe`, `web.pak`, or a
required generated C export is unavailable.

## Verified retail runtime observations

The following facts were observed in windowed 2026-07-10/11 probes against the
user's legitimate Steam installation; they are not inferred from QLSRP:

- `awesomium.dll` reports Awesomium 1.7.4.2 and starts the retail
  `awesomium_process.exe` helper.
- The local retail `web.pak` is Chromium DataPack v4 with 1,024 resources.
- A `QL` DataPak source resolves `asset://ql/index.html` into a live offscreen
  view and exposes a non-power-of-two 1280×720 software surface.
- The engine-owned pre-document bridge makes `qz_instance` available during
  page bootstrap. The live page subsequently issued native bridge requests.
- Engine renderer screenshots showed the complete retail menu surface under
  OpenGL, OpenGL2, GLx, and Vulkan,
  including Play, Statistics, Steam Workshop, Steam Community, Settings,
  friends/lobby, and the retail background.
- A Vulkan `vid_restart` rebuilt the renderer while retaining the browser
  document and reproduced the identical full-menu screenshot without an engine
  or Awesomium diagnostic.
- Starting the legacy DataPak loader after Vulkan initialization made the same
  verified absolute `web.pak` path fail with `DataPak.cc(102)` in the 32-bit
  process. Starting WebCore and the DataPak source during the existing
  pre-renderer client bootstrap loaded the same package successfully; the
  provisional 1280x720 view then resized to the renderer dimensions.

QLSRP was used separately as behavioral and ABI evidence for the expected
core/session/view lifecycle and generated export names. The FnQL adapter is an
independent typed implementation. Retail behavior remains authoritative.

## Implemented architecture

`code/client/webui_backend.hpp` defines a versioned browser-neutral C++17
contract with explicit lifecycle, surface, input, navigation, script, resource,
status, and ownership rules. `awesomium_backend_win32.cpp` implements the
Windows x86 adapter around the runtime's generated stdcall C exports:

- absolute-path DLL loading with dependency search rooted beside the selected
  runtime and a legacy-Windows fallback that still uses an absolute path;
- exact required-export validation before any runtime object is created;
- owned UTF-8/UTF-16 conversion and copied diagnostics;
- reverse-order cleanup of partial or complete startup;
- WebCore, bitmap factory, WebSession, `QL` DataPak source, and offscreen
  WebView ownership;
- pre-renderer DataPak startup with a bounded provisional surface, followed by
  normal renderer-driven resize, so the 32-bit Vulkan path does not contend
  with the legacy package loader during its initial reservation;
- pre-document and post-navigation qz bridge injection;
- bounded script results and software-surface copies;
- resize, focus, pause, mouse, wheel, keyboard, cache, reload, crash, and
  loading-state operations; and
- no Steamworks success emulation. Unavailable social/online operations remain
  explicit and do not prevent the offline retail menu from rendering.

The legacy native-UI `web_stopRefresh` verb is deliberately non-destructive
when the live browser owns the document. Retail does not register it as an
Awesomium navigation-stop command, and aborting an in-flight `index.html`
load can leave Chromium's `chrome://chromewebdata/` error document active.
Bridge retries use a private lightweight qz synchronizer rather than calling a
page-owned `main_hook_v2` repeatedly after startup.

The renderer API owns a dedicated arbitrary-size RGBA WebUI image and shader in
OpenGL, OpenGL2, GLx, and Vulkan. It does not consume cinematic handles or
inherit the cinematic power-of-two restriction. Native UI ownership transfers
only after a live Awesomium bitmap surface and renderer presenter both exist.

## Safety and non-regression

- Privileged navigation remains locked to `asset://ql/`.
- Browser resources remain bounded by the checked WebPak/launcher resolver.
- The retail runtime and assets remain external and are never release inputs.
- x64, Linux, and macOS retain the null backend and native UI fallback.
- A missing, incompatible, or crashed browser yields to the native UI instead
  of making the engine unusable.
- `web_status` reports bounded document/backend state and `web_dumpSurface`
  captures the copied CPU bitmap for renderer-independent diagnosis.
- `FNQL_WEBUI_VERBOSE_LOG=1` enables the retail runtime's verbose log level for
  an explicit diagnostic launch; normal launches retain the runtime default.
- The typed fake-backend suite continues to cover interface mismatch, lifecycle,
  crash/retry, navigation, scripts, input, surface bounds, and balanced resource
  ownership without requiring the proprietary runtime.

## Runtime validation

Promotion still requires the proportionate non-regression matrix: strict x86
and x64 builds, all source/contract tests, native fallback probes, and windowed
retail launches. Full-menu screenshots now cover OpenGL, OpenGL2, GLx, and
Vulkan, including a Vulkan `vid_restart` survival probe. Resize,
cross-renderer switching, device-loss, simultaneous cinematic, and forced
child-process failure probes remain useful follow-up stress gates.
