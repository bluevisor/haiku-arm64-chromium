# Chromium for Haiku arm64

**Status: working on a Haiku arm64 VM (hrev60290, UTM on Apple Silicon),
2026-10-24.** This is Chromium's full `//chrome` browser 154.0.8036.0, the
real Chrome UI with tab strip, omnibox, profiles, settings and extensions.
Google account sign-in works in it, and so do **WebGL 1 and 2**, rendered on
the CPU by SwiftShader through ANGLE's Vulkan backend.

[R Chromium](https://github.com/bluevisor/haiku-arm64-bootstrap/blob/main/docs/chromium.md)
ships `content_shell`, a test shell. Google rejects that shell at sign-in with
"Couldn't sign you in – This browser or app may not be secure", and spoofing
the user agent doesn't get past it. The full browser passes.

The VM itself (building Haiku arm64, UTM setup, system packages) is
[haiku-arm64-bootstrap](https://github.com/bluevisor/haiku-arm64-bootstrap).

The build starts from rainygirl's
[R Chromium port](https://github.com/rainygirl/renkuos-rchromium-arm64): an
`ozone/haiku` backend over BeAPI, plus Haiku code for base, content, media and
V8. The port's scripts target `content_shell`, so `chrome/` needed porting on
top, mostly by treating Haiku as desktop Linux. In src, the port and that work
together come to 534 changed files (317 of them under `chrome/`) and 79 new
ones, plus small changes in 15 sub-repos.

**License.** BSD-3-Clause, the same as Chromium. The port repo has no
top-level LICENSE file, which is why GitHub lists it as unlicensed. Its
sources do carry Chromium's BSD header, though, and its package metadata
declares `BSD (3-clause)`. A binary must ship with Chromium's `LICENSE` and the
third-party notices. Step 8 shows how to generate them.

This repo holds everything referenced below:

| Path | What |
|---|---|
| `patches/` | The complete source delta from upstream 154.0.8036.0, port included: one `.diff` and one `.new-files.tar.gz` per repo (src plus 15 DEPS sub-repos such as v8, angle, perfetto and swiftshader). `REPOS.txt` lists each repo with its base commit. |
| `tools/` | `haikuize.py`, `gn_haikuize.py`, `cond.py` (porting helpers), `aarch64-unknown-haiku.json` (Rust target) |
| `vm/` | The `HaikuChrome` launcher, `ui_is_dark.cpp`, `hshot.cpp` (screenshots), `typepost.cpp` (type into a window over ssh), `hstack.cpp` (live stack dumps), `webgl-probe.html` (WebGL 1/2 check) |

## Overview

1. Set up the build host: an x86_64 Linux container under Rosetta, with about 70 GB of disk.
2. Build the Haiku sysroot and cross gcc, then fix 3 things in the sysroot.
3. Check out Chromium at the tag `154.0.8036.0`.
4. Apply `patches/`.
5. Set up a Rust target JSON for `aarch64-unknown-haiku`.
6. Build the BeAPI shim (`libchromium_haiku.so`) with Haiku's gcc.
7. Run `gn gen` with the args below, then `ninja chrome`. That's about 60k steps, roughly 8 h at -j14.
8. Strip the binary, add the license files, and copy everything to the VM.
9. Add the launcher and the Deskbar entry.

The VM needs the `haiku_rchromium_fixes` system package from
[haiku-arm64-bootstrap: chromium.md §1](https://github.com/bluevisor/haiku-arm64-bootstrap/blob/main/docs/chromium.md#1-the-system-fixes-haiku_rchromium_fixes):
TLSDESC relocations and the `lock_memory_etc` fix. The full browser doesn't
need the RLIM_INFINITY shim, because it's built against hrev60290 headers.

## 1. Build host

All builds here ran on an Apple Silicon Mac with
[colima](https://github.com/abiosoft/colima), with the VM disk on an external
SSD. Chromium's prebuilt clang, rustc, gn and node are x86_64-only, so the
Chromium container runs as `linux/amd64` under Rosetta.

```sh
# Optional: keep the 200 GB VM disk off the internal drive. colima 0.10 ignores
# COLIMA_HOME, so symlink ~/.colima before the first start (don't mv an
# existing one: the sparse disk can expand to full size).
ln -s /Volumes/BACKUP/chromium-haiku/colima ~/.colima

colima start haiku-chromium --vm-type vz --vz-rosetta --arch aarch64 \
  --cpu 14 --memory 40 --disk 200 --runtime docker
docker context use desktop-linux   # colima start silently made itself the default
export DOCKER_CONTEXT=colima-haiku-chromium

docker volume create chromium-build
# Chromium: x86_64 under Rosetta
docker run -d --name chromium-haiku --platform linux/amd64 \
  -v chromium-build:/build ubuntu:24.04 sleep infinity
# Haiku cross tools: native arm64 (the gcc build segfaults under Rosetta)
docker run -d --name haiku-tools --platform linux/arm64 \
  -v chromium-build:/build -v $PWD:/repo:ro debian:bookworm sleep infinity
```

## 2. Haiku sysroot and cross gcc

In `haiku-tools`, build them with
[haiku-arm64-bootstrap](https://github.com/bluevisor/haiku-arm64-bootstrap)'s
`scripts/container/build-bfs-shell.sh` and `build-cross-tools.sh`
([guide](https://github.com/bluevisor/haiku-arm64-bootstrap/blob/main/docs/guide.md)) at **hrev60290**. The sysroot ends up in
`/build/haiku/generated.arm64/cross-tools-arm64/sysroot`.

- Clone Haiku from `https://review.haiku-os.org/haiku`. The GitHub mirror has
  no `hrevNNNNN` tags, and the script then silently clones master.
- Upstream `arm64/current` has moved on, so take the hrev60290 hpkgs from
  `haiku-repository.cdn.haiku-os.org/master/arm64/r1~beta6_hrev60290/packages/`.

Then make three fixes to the sysroot. Each one cost a failed build:

```sh
S=/build/haiku/generated.arm64/cross-tools-arm64/sysroot
G=/build/haiku/generated.arm64/cross-tools-arm64/lib/gcc/aarch64-unknown-haiku/13.3.0

# 1. clang's stdatomic.h #include_next's gcc's copy, whose macros reject _Atomic
#    under clang ("address argument to atomic operation must be a pointer to a
#    trivially-copyable type" in dav1d/ffmpeg/pthreadpool). gcc is unaffected.
mv $S/boot/system/develop/headers/gcc/include/stdatomic.h{,.gcc-hidden-for-clang}

# 2. The chrome link needs gcc's crt objects where clang's Haiku driver looks.
#    Don't symlink the whole gcc dir: clang would then pick up gcc's headers too.
cp $G/crt{begin,beginS,end,endS}.o $S/boot/system/develop/lib/

# 3. ffmpeg links system zlib, and the bootstrap sysroot has none.
#    "unable to find library -lz" also hides every other undefined symbol.
scp haiku:/boot/system/lib/libz.so.1.2.13 $S/boot/system/develop/lib/
ln -s libz.so.1.2.13 $S/boot/system/develop/lib/libz.so.1
ln -s libz.so.1.2.13 $S/boot/system/develop/lib/libz.so
```

## 3. Chromium source

In `chromium-haiku`:

```sh
apt-get update && apt-get install -y git curl python3 python3-venv lsb-release \
  ca-certificates xz-utils file bzip2 zstd sudo locales pkg-config
git clone https://chromium.googlesource.com/chromium/tools/depot_tools /root/depot_tools
export PATH=/root/depot_tools:$PATH

mkdir -p /build/chromium && cd /build/chromium
cat > .gclient <<'EOF'
solutions = [{
  "name": "src",
  "url": "https://chromium.googlesource.com/chromium/src.git@559328f5b68a1573788db579782511be2d427ba9",  # tag 154.0.8036.0
  "managed": False,
  "custom_deps": {},
  "custom_vars": {"checkout_nacl": False, "checkout_pgo_profiles": False},
}]
target_os = ["linux"]
EOF
gclient sync --nohooks --no-history
gclient runhooks
src/build/util/lastchange.py -o src/build/util/LASTCHANGE   # if the hook didn't write it
```

In `haiku-tools` (step 2), the build needs
`git g++ make bison flex zlib1g-dev libzstd-dev python3 nasm attr bzip2 xz-utils wget curl file`.

## 4. Apply the patches

`patches/` is the whole delta, so there's no need to run the port's scripts:

```sh
P=/path/to/haiku-arm64-chromium/patches
cd /build/chromium/src
for d in $P/*.diff; do
  name=$(basename $d .diff)
  repo=$([ $name = src ] && echo . || echo ${name//__//})
  git -C "$repo" apply --whitespace=nowarn "$d"
  tar -C "$repo" -xzf "$P/$name.new-files.tar.gz"
done
```

(`REPOS.txt` has each sub-repo's base commit. They are the ones DEPS pins for
154.0.8036.0.)

To redo the port from scratch instead, see "What the patches change" below.

## 5. Rust

Chromium's bundled rustc (1.99 nightly) can target Haiku through a JSON target
spec. You don't need to rebuild rustc.

```sh
mkdir -p /build/rust-target
cp tools/aarch64-unknown-haiku.json /build/rust-target/
export RUST_TARGET_PATH=/build/rust-target   # before every ninja run
```

The patches already add `-Zunstable-options` (custom targets need it) to
`build/config/haiku:sdk` and `build/rust/gni_impl/rustc_print_cfg.py`.

## 6. The BeAPI shim

`libchromium_haiku.so` wraps BWindow/BView/BApplication. It has to be built
with Haiku's gcc (the one that built libbe), not Chromium's clang. Link it with
**4 KB pages**: with gcc's default 64 KB segments, the arm64 runtime_loader
mis-maps it.

```sh
# in haiku-tools
git clone https://github.com/rainygirl/renkuos-rchromium-arm64 /build/rchromium-port
cd /build/rchromium-port/haiku_shim
aarch64-unknown-haiku-g++ -c -fPIC -O2 haiku_shim.cc -o haiku_shim.o
aarch64-unknown-haiku-g++ -shared haiku_shim.o -o libchromium_haiku.so \
  -Wl,-soname,libchromium_haiku.so -Wl,--hash-style=both \
  -Wl,-z,max-page-size=0x1000 -lbe -lstdc++ -lroot
cp libchromium_haiku.so $S/boot/system/develop/lib/   # ozone links -lchromium_haiku
```

## 7. Configure and build

`out/haiku-arm64/args.gn`:

```gn
target_os = "haiku"
target_cpu = "arm64"
is_debug = false
symbol_level = 0
blink_symbol_level = 0
v8_symbol_level = 0
haiku_sysroot = "/build/haiku/generated.arm64/cross-tools-arm64/sysroot"
target_sysroot = "/build/haiku/generated.arm64/cross-tools-arm64/sysroot"
use_partition_alloc_as_malloc = false
enable_backup_ref_ptr_support = false
use_allocator_shim = false
chrome_pgo_phase = 0
treat_warnings_as_errors = false
use_remoteexec = false
use_v8_context_snapshot = false   # else ~25k extra host-toolchain steps
generate_about_credits = true     # else chrome://credits is a stub

# WebGL: ANGLE's Vulkan backend on SwiftShader ("SwANGLE")
enable_swiftshader = true
angle_enable_vulkan = true
angle_enable_swiftshader = true
angle_use_vulkan_null_display = true   # offscreen only; windows stay software-composited
angle_use_custom_libvulkan = false     # no Khronos loader: volk dlopens libvulkan.so.1
angle_enable_vulkan_validation_layers = false   # DCHECK builds turn them on; no Haiku port
```

The Khronos Vulkan loader has no Haiku port (`DIRECTORY_SYMBOL` and
`loader_platform_dl_handle` are undeclared). Even with
`angle_use_custom_libvulkan = false`, `ui/gl` and ANGLE's
`angle_vulkan_entry_points` still pull it in as a **data_dep**. The patches
gate those with `!is_haiku`. Plain `gn path` reports "No non-data paths" and
hides the dependency, so check with
`gn path --with-data out/haiku-arm64 //chrome //third_party/vulkan-loader/src:libvulkan`.
The build produces `libvk_swiftshader.so` next to `chrome`.

Set `generate_about_credits` before the first build. If an out dir was already
built with it off, ninja's `copy` step left `gen/…/about_credits.html` as a
**hardlink** to `components/resources/sample_credits.html`. Turning the flag on
afterwards makes `licenses.py` write through that link and overwrite the
source file with 8.8 MB of credits. To fix it, run
`git checkout components/resources/sample_credits.html`.

```sh
export RUST_TARGET_PATH=/build/rust-target
buildtools/linux64/gn gen out/haiku-arm64      # depot_tools' gn wrapper fails here
third_party/ninja/ninja -C out/haiku-arm64 chrome
```

- `.gn` must have `root = "//chrome:chrome"` (the patches set it).
- Before going far, check `ls out/haiku-arm64/obj/chrome/browser/browser | wc -l`
  is nonzero. If it's 0, `//chrome/browser` isn't in the build, and the only
  sign is "undefined symbol: ChromeMain" at the very end.
- After a source fix, a rebuild of one file plus the relink takes about 1 min.
- To list every undefined symbol at once:
  `eval "$(ninja -C out/haiku-arm64 -t commands chrome | tail -1) -Wl,--error-limit=0"`.
  Strip ANSI colors before you grep the output.

## 8. Package and deploy

```sh
# container: strip (632 MB -> 373 MB) and collect what chrome loads at runtime.
# The binary is named Chromium because Deskbar labels a running app with its
# executable's file name.
D=/build/deploy/HaikuChrome; mkdir -p $D/lib $D/locales
O=out/haiku-arm64
STRIP=third_party/llvm-build/Release+Asserts/bin/llvm-strip
$STRIP -o $D/Chromium $O/chrome
cp $O/{resources.pak,chrome_100_percent.pak,chrome_200_percent.pak,icudtl.dat,snapshot_blob.bin} $D/
cp $O/locales/en-US.pak $D/locales/
cp $S/boot/system/develop/lib/libchromium_haiku.so $D/lib/
# SwiftShader is the Vulkan driver. ANGLE's volk dlopens "libvulkan.so.1", and
# with no loader in between, the driver itself goes under that name.
$STRIP -o $D/lib/libvulkan.so.1 $O/libvk_swiftshader.so

# licenses: Chromium's BSD LICENSE + notices for all 683 third-party components
cp LICENSE $D/
export PATH=$PWD/buildtools/linux64:$PATH
tools/licenses/licenses.py license_file --gn-target //chrome:chrome \
  --gn-out-dir $O --format txt $D/LICENSES.chromium.txt
cp $O/gen/components/resources/about_credits.html $D/credits.html   # = chrome://credits

# stream to the VM (nothing lands on the host disk)
docker exec chromium-haiku tar -C /build/deploy -cf - HaikuChrome \
  | ssh haiku 'tar -C /boot/home -xf -'
```

When only `chrome` changed, ship just that file. When the target is running,
write to a temp name and `mv` it over: the running process keeps the old inode.
Keep the tar in its own ssh command. `ssh haiku 'tar -xf - && nohup … &'`
backgrounds the whole `&&` list, so tar reads `/dev/null` and fails with
"This does not look like a tar archive".

## 9. Launcher and Deskbar entry

`vm/HaikuChrome` goes into `/boot/home/HaikuChrome/`. It sets
`LIBRARY_PATH=%A/lib`, so the shim and `libvulkan.so.1` resolve, and runs:

```
./Chromium --ozone-platform=haiku --no-sandbox --single-process \
  --in-process-gpu --disable-gpu-compositing \
  --use-angle=swiftshader --enable-unsafe-swiftshader \
  --use-fake-device-for-media-stream \
  --user-data-dir=/boot/home/config/settings/HaikuChrome [--force-dark-mode]
```

The GPU thread runs ANGLE on SwiftShader, and the display compositor stays in
software, so WebGL frames are read back into the window's bitmap. Replace the
ANGLE flags with `--disable-gpu` to turn WebGL off.

`--force-dark-mode` is added when `ui_is_dark` reports that Haiku's panel
color is dark. Build it on the VM with `g++ ui_is_dark.cpp -o ui_is_dark -lbe`.
`--use-fake-device-for-media-stream` gives media a capture device: Haiku has
none, and pages that list devices crash the browser without one.

```sh
# guest
H=/boot/home/HaikuChrome
cp /boot/system/apps/RChromium/rchromium.hvif $H/chrome.hvif     # any HVIF icon
addattr -t mime BEOS:APP_SIG application/x-vnd.bluevisor-HaikuChrome $H/HaikuChrome
addattr -t icon -f $H/chrome.hvif BEOS:ICON $H/HaikuChrome       # menu icon
M=/boot/home/config/non-packaged/data/deskbar/menu/Applications
mkdir -p $M && ln -sf $H/HaikuChrome $M/Chromium   # the link's name is the menu label
```

Deskbar's running-app list shows the **executable's** file name and
`BEOS:ICON` (the `Chromium` binary, not the launcher). It reads the icon once,
when the app starts. Every deploy from Linux strips BFS attributes, so the launcher re-adds
the icon from `chrome.hvif` whenever it's missing.

The Deskbar menu merges `deskbar/menu` from the system, system non-packaged,
user data, user non-packaged and user settings directories
(`/boot/system/data/deskbar/menu_entries`). To drop a package's entry without
rebooting, repackage it without the symlink and `pkgman install` the new
revision. packagefs only reads `BlockedEntries` settings at boot.

## Runtime fixes, and why

Each of these got past compile and link, then broke at runtime. All are in
the patches.

| Symptom | Cause | Fix |
|---|---|---|
| `runtime_loader: Could not map image: Bad data` | lld's arm64 default `max-page-size` is 64 KB, which skews each segment's offset from its vaddr. Haiku arm64 needs them aligned to 4 KB. (The port patches its own clang driver for this; the bundled clang lacks the patch.) | `ldflags += ["-Wl,-z,max-page-size=4096"]` in `build/config/haiku:sdk`. Check that `llvm-readelf -l` shows LOAD align 0x1000. |
| `DCHECK failed: message_center_bridge_` | DCHECKs are on in non-official builds, and `enable_chrome_notifications` is Mac/Win/Linux-only, so there was no notification bridge at all | A Haiku `NotificationPlatformBridge` that reports ready and drops notifications (`chrome/browser/haiku/chrome_browser_haiku.cc`) |
| `Received signal 30 BUS_ADRALN 000000000000` | `-fsanitize=return` is trapping: a non-void function fell off the end of its `#if` chain on Haiku | Add the missing `#else return …`. Find them all with `grep "does not return a value"` in the ninja log. There were 2: `GlobalAcceleratorListener::GetInstance` and supervised_user_navigation_observer. |
| Crash ~30 s after launch on the New Tab page: `wtf_string.cc:243 DCHECK failed: StringView("C") == setlocale(4, nullptr)` | Haiku's libroot always names the C locale `"POSIX"`. Nothing had changed the locale. | Expect `"POSIX"` under `BUILDFLAG(IS_HAIKU)` in `blink::String::Format`'s DCHECK |
| Windows-style min/max/close drawn inside the window, under Haiku's own tab | `BrowserNativeWidgetAura::UseCustomFrame()` returns true, so `OpaqueBrowserFrameView` draws a full custom frame, but the shim's BWindow already has the Haiku decorator | Return false on Haiku. This is the "system title bar" mode: no caption buttons, so close, zoom and minimize (double-click the tab) come from the decorator. |
| Crash on any page using `font-family: system-ui` (e.g. bucaastudio.com): `font_cache.cc:148 DCHECK failed: !family.empty() && family != font_family_names::kSystemUi` | The browser only sends a system font family to Blink on Linux/ChromeOS, so on Haiku it's empty, and `SystemFontPlatformData` DCHECKs instead of returning null | Add `IS_HAIKU` to that function's Linux/Fuchsia branch, so `system-ui` falls back to the default sans font |
| The whole VM freezes after about an hour of browsing (QEMU at 800% CPU, no ping, last syslog line "low resource memory: normal -> note") | Haiku doesn't overcommit, and it ignores `MADV_DONTNEED` (`_user_memory_advice`: "TODO: Implement!"). PartitionAlloc reserved its pools without `MAP_NORESERVE`, so every page it ever made RW stayed charged to RAM+swap. Idle on the New Tab page: 12 GB committed, 1.1 GB used. | On `__HAIKU__`, `page_allocator_internals_posix.h` adds `MAP_NORESERVE` to the reserve and decommit mmaps and discards with `MADV_FREE`. V8's `OS::DiscardSystemPages` also uses `MADV_FREE`. Commit is now about 0.6 GB. |
| No WebGL, even with ANGLE built | `HaikuSurfaceFactory` offered no GL implementations | A `GLOzoneEGL` subclass for `kGLImplementationEGLANGLE` with offscreen pbuffer surfaces only (`CreateViewGLSurface` returns null) |
| A page using WebGL leaves the window unpainted (the desktop shows through) and the browser hangs, with every thread waiting | Two bugs. (1) SwiftShader's `ExecutableMemory.cpp` falls into its generic POSIX branch on Haiku, taking JIT pages from the heap and `mprotect`ing them. libroot's page allocator then segfaults in `PagesAllocator::_Insert`, writing a free-chunk header into those pages. (2) Chrome's `StackDumpSignalHandler` first calls `BeingDebugged()`, which on Haiku was `NOTIMPLEMENTED()`. That allocates, and because the crash happened inside `malloc`, it deadlocks on the heap lock instead of printing anything. | (1) A `__HAIKU__` branch that `mmap`s and `munmap`s JIT pages. (2) `BeingDebugged()` reads `team_info.debugger_nub_thread`, which is signal-safe. |

## Debugging on the VM

- **Logs:** run the launcher from ssh with `--enable-logging=stderr > log 2>&1`.
  A launch from Tracker or Deskbar logs nowhere.
- **Crash traces:** launch with `--disable-in-process-stack-traces` so the
  crash reaches Haiku's debug_server. The kernel then writes
  "debug_server: Thread N entered the debugger: Segment violation" and a
  symbolized stack trace to **`/var/log/syslog`**. Write
  `~/config/settings/system/debug_server/settings` containing
  `executable_actions { /boot/home/HaikuChrome/Chromium report }`, so a crash
  kills the team instead of waiting on a dialog. The `report` action itself
  fails on arm64 ("Failed to start debugger"), because the arm64 image has no
  Debugger app.
- **Hangs:** `vm/hstack.cpp` dumps every thread's stack of a live team as
  `image+offset`. Build it with
  `g++ -I/boot/system/develop/headers/private/debug hstack.cpp -o hstack -ldebug`
  and run `hstack <team> [thread-substring [scan]]`. Chromium has no frame
  pointers, so frame chains through it break off. `scan` instead lists every
  stack word that points into an image, and that's how the signal-handler
  deadlock above showed up. For these offsets, offset = file vaddr: symbolize
  `$((off-4))`, and resolve libroot against the sysroot's
  `boot/system/lib/libroot.so`.
- **Symbolize:** take the image base from the log's
  `image 0x…+… /boot/home/HaikuChrome/chrome` line. Then run
  `third_party/llvm-build/Release+Asserts/bin/llvm-symbolizer --obj=out/haiku-arm64/chrome <addr - base>`
  on the unstripped binary, **one address per call** (a batched pipe came out
  as all `??`). `symbol_level=0` still keeps function names.
- **Screenshots:** the VM has no screenshot tool. `vm/hshot.cpp`
  (`BScreen::GetBitmap` → PPM; `g++ hshot.cpp -o hshot -lbe`), then
  `sips -s format png` on the Mac.
- **Typing over ssh:** `typepost <team> <title-substring> "accounts.google.com\n"`.
  A plain `B_KEY_DOWN` to the window is ignored. The tool retargets a
  BMessenger to `B_PREFERRED_TOKEN` so the keys reach the focus view. Build
  with `-I/boot/system/develop/headers/private/{app,shared} -lbe`.

## What the patches change

The patches cover the port itself, plus these changes on top of it. Use this
section to redo the work on another Chromium version.

1. **The port's pieces it doesn't apply itself.** `port/files-root.gn` →
   `src/.gn`. `build/config/haiku/` and `build/toolchain/haiku/` exist only
   inside `port/haiku-port.diff`. Run `apply-haiku-port.sh`, then
   `port-ozone.py`, then the ~126 hunks of `haiku-port.diff` that the scripts
   skip (classify them with `git apply --check` / `-R --check`). Skip
   `build/rust/std/rules/BUILD.gn`: it's only for the author's replaced rustc.
2. **Turn the browser layer on.** In `chrome/BUILD.gn`, the `chrome`
   executable's main block is `is_linux || is_chromeos`: add `is_haiku`.
   Drop crashpad (no Haiku port) with a no-op `crashpad_haiku.cc`, and skip
   crash init in `chrome_main_delegate.cc`.
3. **Treat Haiku as desktop Linux.** grit already builds the Linux strings for
   Haiku (with no `-t` it falls back to the host platform). The port's GN
   compiles most desktop sources. Only the C++ `#if`s disagree:
   - `tools/haikuize.py` rewrites `BUILDFLAG(IS_LINUX)` →
     `(BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_HAIKU))` in `#if`/`#elif`. Run it on
     **definition files only** (`*features*`, `*switches*`, `*pref_names*`,
     `*_constants*`, `*_prefs*`, about 110 files). On callers it pulls in
     zygote, xdg and breakpad code.
   - `tools/gn_haikuize.py` adds `|| is_haiku` to every GN
     `if (…is_linux…)` that also names win or mac (260 gates), and to the
     `assert`s.
   - `static_assert`s aren't directives, so the script misses them. Fix those
     by hand.
   - Revert truly Linux-only about_flags entries (pulseaudio, CUPS IPP, USB
     detach, BT pairing).
4. **WebUI.** Wherever grit's `<if expr="is_linux">` disagrees with GN, add
   `|| is_haiku` to the GN side (settings captions, version.mojom). Patch
   single mojoms rather than `mojom.gni`, which would rebuild everything.
   Turn the updater WebUI off (`//chrome/updater` has no Haiku build).
5. **Enterprise buildflags** `enterprise_proxy`,
   `enterprise_client_certificates` and `enterprise_cloud_content_analysis`
   are on for Haiku, because chrome code assumes them on desktop.
6. **Haiku implementations,** mostly "not supported" answers:
   - `chrome/browser/haiku/chrome_browser_haiku.cc` covers platform_util via
     `/bin/open`, shell_integration, shortcuts, web app OS integration, the
     notification bridge, StatusTray, IconLoader, MemoryDetails, and the
     printer/network settings (which open the Haiku preflets), plus a few
     more.
   - `browser_desktop_window_tree_host_haiku.cc` and
     `chrome_views_delegate_haiku.cc`.
   - `ui/views` `desktop_window_tree_host_platform.cc`. It was Linux/Fuchsia
     only, but it's the real top-level window host.
   - base: `process_handle_haiku.cc` (`get_next_image_info`),
     `thread_delegate_haiku.cc`, commit charge and hardware info.
7. **Generic implementations added to GN for Haiku:**
   `version_updater_basic`, `menu_config_fuchsia`, `font_fallback_skia`,
   pdfium `fx_linux_impl` (plus Haiku font paths), `policy_path_parser_linux`,
   `window_icon_util_ozone`, `browser_native_widget_factory_aura`,
   `posix_key_provider`, `storage_monitor_dummy`, and
   `external_begin_frame_adapter`.
8. **Link:** `libs += ["network", "bsd", "gnu"]` in
   `build/config/haiku:runtime_library`. That covers sockets, `arc4random_buf`
   and `sched_getaffinity`.
9. **WebGL (SwANGLE),** on top of the args in step 7:
   - `angle.gni`: `angle_shared_libvulkan` is no longer false on Haiku.
   - SwiftShader's `Vulkan/BUILD.gn`: default-visibility `VK_EXPORT` and the
     `vk_swiftshader.lds` version script on Haiku.
   - `!is_haiku` on the vulkan-loader data_deps (`ui/gl/BUILD.gn` ×3, ANGLE
     `src/common/vulkan/BUILD.gn`).
   - ANGLE `system_utils_haiku.cpp` gains GetExecutablePath (from
     `get_next_image_info`), GetExecutableDirectory, GetSharedLibraryExtension,
     and GetCurrentSystemTime(Ns) (`CLOCK_MONOTONIC`, since Haiku has no
     `_RAW`).
   - LLVM 10 `raw_ostream.cpp` doesn't use `lseek64` on Haiku.
   - `haiku_surface_factory.cc` adds `GLOzoneEGLHaiku`, plus the JIT-memory
     and `BeingDebugged` fixes from the runtime table.
10. **Memory:** the PartitionAlloc and V8 `MAP_NORESERVE`/`MADV_FREE` changes
    from the runtime table.

## Limitations

- **Single-process, no sandbox, software compositing,** the same as
  R Chromium. Chrome warns about `--no-sandbox` at the top of the window.
- **WebGL runs on the CPU.** chrome://gpu reports "Hardware accelerated but at
  reduced performance", with renderer "ANGLE (Google, Vulkan 1.3.0
  (SwiftShader Device (LLVM 10.0.0)))". WebGL Aquarium with 500 fish runs at
  about 10 fps across all 8 vCPUs. The UTM `virtio-gpu-gl-pci` device boots
  Haiku in 2D only (the driver negotiates EDID but never VIRGL), so there's
  no host GPU path yet.
- **DCHECKs are on.** Expect the occasional DCHECK crash on Haiku-specific
  paths. Set up the crash-report recipe above, symbolize, and fix.
- **Not implemented:** notifications (dropped), network change notifier, idle
  detection, file watching, battery and memory dumps. These only log errors.
- **No Google API keys,** so Chrome's own account sync is off. Signing in to
  Google websites works.
- The shim's native toolbar (the black bar with back, forward, reload and an
  address field) still sits above Chrome's tab strip. It's left over from
  R Chromium's content_shell, which has no UI of its own.
