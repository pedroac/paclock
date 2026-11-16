# Platform support

paclock (a screen locker) is Linux only. The project started on **Arch Linux** and the author's setup is **Hyprland**, **Sway** and **pacland**; anything not listed here as run is *untested*, not *supported* (principle 14).

## What was run

| Distribution | Compositor | Run by | Date |
|---|---|---|---|
| Arch Linux | the author's setup | the author | not recorded |
| Arch Linux | sway 1.12, headless, nested (`WLR_BACKENDS=headless`), 1280x720 | an AI-assisted session on the author's machine | 2026-11-16: the real lock, keys typed with `wtype`, for the measurements below; no password check, no speech heard, not looked at on a screen |
| Debian, Ubuntu or Fedora | | | **not tried** |
| Alpine (musl) | | | **not tried** |

`make test` runs without a display. `paclock --demo` was run on the author's Hyprland session (Arch Linux) to measure memory, and under AddressSanitizer and UBSan for a few seconds; the lock on a real screen, the keymap re-send, key repeat when a keyboard is removed and the integer scale of a compositor older than `wl_surface` v6 were **not run** on a compositor. The release check of principle 15 (a glibc distribution that is not Arch, and Alpine) has not been done for paclock; `../pacommon/tests/distros.sh` does it for pacommon. Whoever runs paclock somewhere else should add a row.

## What assumes Linux or a setup

- **Compositor:** needs `ext-session-lock-v1`; also `xdg-shell`, `wp_viewporter`, `wp_single_pixel_buffer_v1`, cursor-shape and tablet protocols where offered. A compositor without `ext-session-lock` cannot be locked: paclock says so and does not pretend to lock.
- **PAM:** the password is checked with PAM (`pam_appl.h`, service `paclock`, installed to `/etc/pam.d/paclock`).
- **Linux APIs:** `memfd_create` for shared memory (`display.c`), Linux evdev key codes.
- **Optional programs** (`lock_hint`, `pause_players`, `mute_streams`, all off by default; [session.md](session.md)): `busctl` (systemd's, for logind's `LockedHint` and for MPRIS players on the session bus) and `pactl` (PulseAudio, or PipeWire's pulse layer). Without them paclock says so once and carries on. **Run only with stand-in programs** that log their arguments, never with the real `busctl`, `pactl` or a player, and never with a lock.
- **System files:** `/usr/share/X11/xkb/rules/evdev.lst` or xkeyboard-config's (layout names).
- **Build:** GNU make, a C compiler, `pkg-config` and `wayland-scanner`.

## Accessibility

Speech (shared code in pacommon's `speech.[ch]`; `SPEECH=speechd`, `speech = yes`) was added with unit tests for the settings and the language code, and checked to build with `SPEECH=none` (no libspeechd in `ldd`) and with the default. **Not done:** the checklist's step 7 (doing the actions with eyes closed against a running speech-dispatcher) and a run on a compositor; add a row here when someone has.

## Size

The tables in this section are from earlier builds; the current figures, and the comparison with swaylock and hyprlock, are in [Compared with swaylock and hyprlock](#compared-with-swaylock-and-hyprlock-for-reference-2026-11-16) below.

Measured 2026-11-16 on Arch Linux with GCC 16.2.1, `make` with the default flags (`-Os`, `-flto`, no unwind tables, stripped,
`--gc-sections`), in a copy of the tree:

| Build | Bytes |
|---|---|
| default (speech and gettext), without packed relocations | 84,728 (83 KiB) |
| default, with packed relocations (what `make` does here) | 68,416 (67 KiB) |
| `SPEECH=none` | 80,632 |
| `I18N=0` | 84,728 |
| `SPEECH=none I18N=0` | 80,632 (79 KiB) |

The difference is relocations, not code: 609 `R_X86_64_RELATIVE` entries (584 point into `.data.rel.ro`, mostly the
generated Wayland protocol tables) take 24 bytes each in `.rela.dyn` (19,536 bytes). `-z pack-relative-relocs` stores
them in `.relr.dyn` (144 bytes). It needs glibc 2.36 or musl 1.2.4 at run time, so the Makefile only adds it
(`RELR_LDFLAGS`) if a trivial program linked with it runs on the build machine; `make RELR_LDFLAGS=` turns it off,
for a binary that is built on a new system but run on an old one. Alpine was not tried. Found by comparing with the
distro's swaylock, whose package build enables the option.

Segments are aligned to 4 KiB, so sizes move in steps of 4,096. The earlier figure (84,680 bytes,
2026-11-15) was before the optional session options (`src/hush.c`), the `wl_output` scale listener
and the other changes since.

Memory: `paclock --demo` left idle for 3 seconds on one screen (the demo is a window, not a lock),
read from `/proc/PID/status`:

| Build (kB, as `/proc` prints them; 1 kB = 1,024 bytes) | VmRSS | file-backed | anonymous | shared memory | VmSize |
|---|---|---|---|---|---|
| default | 13,464 | 10,480 | 1,628 | 1,356 | 95,532 |
| `SPEECH=none I18N=0` | 13,092 | 10,196 | 1,548 | 1,348 | 21,512 |

Most of the resident size is the pages of the shared libraries (Cairo, FreeType, Wayland, ...), which
other programs share. The much larger virtual size with speech is not resident (speech-dispatcher's
library starts threads, with their stacks). `hush.c` reads what `busctl` and `pactl` print into a
256 KiB buffer that it allocates only while it parses (bss is 62,040 bytes, from 324,064 when the buffer
was static; see [decisions.md](decisions.md)); the demo does not run them.

With `resources = minimal` (a real lock in a nested sway at 1280x720, idle, `SPEECH=none I18N=0`, three runs each): the card's
memfd is 258,048 bytes against 1,007,616, RssShmem 308 against 1,040-1,196 kB, PSS 2,750-2,798 against 3,114-3,194 kB,
VmRSS 9,744-9,824 against 10,348-10,492 kB. swaylock with a white background, one run: 60 kB shm, PSS 1,803 kB, VmRSS 9,644 kB.
See [decisions.md](decisions.md).

A real lock in a nested sway, 2026-11-16, `SPEECH=none I18N=0`, read from `/proc/PID/smaps`
(a scratch build without `PR_SET_DUMPABLE`, which otherwise blocks reading it): RSS 9.8 MB without
`setlocale`, against 12.0 MB with it (the locale archive, 2.2 MB); PSS 2.5 MB either way, since the
archive is shared. One run each way twice; RSS moves by up to about 1 MB between runs.

**Not measured:** a real lock on a real screen (one buffer per screen, so more screens use more), memory while a password is being checked, and the
run with `lock_hint`, `pause_players` or `mute_streams` on.

## Compared with swaylock and hyprlock, for reference (2026-11-16)

These figures are for reference, to know where paclock stands and what its parts cost, and not a target: paclock does not try to
have the smallest binary or the lowest memory or CPU. The other two are different programs (swaylock draws nothing until a key is
pressed; hyprlock is drawn on the GPU and themable), each made for its own aims, and paclock's are to be lean and easy to use. The
memory saved at `resources = low` and `minimal` was taken only where it costs a person nothing they need, and each cost is listed
below.

*The size and memory figures below were first taken before the text layout was added; the rows for paclock are now the later ones (default 84,800 bytes and PSS 2,362 kB after a key, the English-only minimal 72,512 bytes and 1,442 kB: about 8 kB and 80 kB more for the scripts, and 4 kB of the minimal's binary is the plain layout code; the 68,416-byte and 1,442 kB figures predate it). The rest of the section is as first written.*

**How.** Each run starts a fresh headless sway (`WLR_BACKENDS=headless`, 1280x720, `WLR_LIBINPUT_NO_DEVICES=1`) with its own
Wayland socket, never the real session, and starts the locker in it. After 5 seconds: CPU over 10 idle seconds from
`utime + stime` in `/proc/PID/stat` (10 ms ticks), and memory from `/proc/PID/status` (VmRSS) and `smaps_rollup` (PSS).
Then one key (`wtype a`) and the memory 1.5 seconds later; then 30 more keys, 0.2 seconds apart, for the CPU per key.
Three runs of each, means. Arch Linux, sway 1.12, swaylock 1.8.6 (`-c 1e1e2e`), hyprlock 0.9.6 (a bare config, and the
author's own, with a clock and labels). paclock "default" is `make` with no config; "minimal" is `SPEECH=none I18N=0
LAYOUTS=0` with only `resources = minimal` and `speech = no`: no speech, no gettext, no layouts, no `pause_players`,
`mute_streams` or `lock_hint`. Checked on that binary: no libspeechd, dbus, systemd or gettext symbol, and no exec, fork or
vfork while running (gdb `catch`; no control run for the `catch`).

| | binary (bytes) | shared libraries |
|---|---|---|
| paclock, default | 84,800 | 36 (with HarfBuzz and FriBidi) |
| paclock, minimal (`I18N=0`) | 72,512 | 28 |
| swaylock | 69,768 | 40 |
| hyprlock | 860,248 | 75 |

| Before a key, then after one key (kB) | RSS | PSS | RSS, 1 key | PSS, 1 key | CPU idle | CPU per key |
|---|---|---|---|---|---|---|
| paclock, default | 13,231 | 2,320 | 13,440 | 2,362 | under 1 tick in 10 s | 1.1 ms |
| paclock, minimal (`MINIMAL_SCALE` 0.7, `I18N=0`) | 8,449 | 1,403 | 8,676 | 1,442 | under 1 tick in 10 s | 0.7 ms |
| swaylock | 9,056 | 1,092 | 9,645 | 1,176 | under 1 tick in 10 s | 1.2 ms |
| hyprlock, bare config | 253,647 | 58,354 | 253,873 | 58,493 | under 1 tick in 10 s | 7.9 ms |
| hyprlock, the author's config | 255,283 | 59,536 | 255,613 | 59,752 | under 1 tick in 10 s | 7.2 ms |

paclock does not grow after the first key (PSS 2,294 and 1,412 after 31). Minimal was first measured at `MINIMAL_SCALE` 0.4 (PSS 1,297 before and 1,347 after one key, shared memory 92 kB), where the messages were too small to read; at 0.7 the card's buffer is 148 kB. swaylock grows from 1,176 to 1,214, and hyprlock by
about 5 MB. Hyprlock's 227 MB of file-backed pages are the graphics stack, and in a headless sway it may be rendering in
software: take its figures as an upper bound for a real GPU session. swaylock draws nothing until a key, so before one it has less to
hold; paclock always shows its prompt. Swaylock's PAM helper process was not counted.

**Where the memory of minimal went** (PSS after one key, kB, from `smaps` and gdb breakpoints on `mmap`):

| Step | Before | After |
|---|---|---|
| the starting point, two font files | 2,039 | |
| one font file at `resources = minimal` (the Light file was 212 kB, mapped by FreeType) | 2,039 | 1,838 |
| text drawn as outlines at `low` and `minimal`: no pixman glyph cache (a 266 kB allocation on the first text) | 1,838 | about 1,540 |
| the compose table made on the first key that may start a sequence (456 kB and 168 kB allocations at start) | about 1,540 | 1,348 (at `MINIMAL_SCALE` 0.4; 1,404 at 0.7) |

Each was measured alone first, with a runtime switch in a scratch build: outlines -295 kB, no compose table -180 kB, both -455 kB.
The compose table is not turned off, only delayed (a password with a dead key would be typed wrong), by `compose_may_start`
in `src/compose.h`: the 62 Compose files of libX11 hold about 12,400 sequences, and all start with a dead key, Multi_key,
backslash, a `ch` key or a Unicode keysym; a test reads the installed files and fails if one does not. A gdb breakpoint showed
the table not made at the start or on a plain key, and made once on `wtype -k dead_acute`.

**Not the cause**, checked: dbus (no dbus or systemd library is mapped, and the only socket is Wayland's), and fontconfig
(breakpoints on `FcInit`, `FcInitLoadConfigAndFonts`, `FcConfigBuildFonts`, `FcDirScan`, `FcFontMatch` and others were never hit,
at any level, where `fc-list` hit them 3,270 times; fonts are found by paclock's own search and handed to FreeType, so
`libfontconfig` is only mapped, about 38 kB PSS, because Cairo links it). The rest of minimal's memory (about 1.3 MB PSS: the
prompt, the font, the text drawing, the keyboard) was not broken down further and nothing more is planned: it is what makes the
lock usable.

**What the savings cost** (so that nobody mistakes them for free):
- one font file at `minimal`: the text in the field is regular weight, not light;
- text as outlines at `low` and `minimal`: the glyphs are not hinted, so small text may look different (not looked at on a screen);
- the compose table made on the first dead key or Multi_key, at every level: that key is a few milliseconds slower, once;
- no username and no layout choice at `low` and `minimal`: a person on the wrong layout switches it with the compositor's keys.
Nothing a person needs to do is taken away at any level, and speech, contrast and the text size limits are never touched.

**A bug found on the way.** The `Makefile` probe for packed relocations (`RELR_LDFLAGS`) wrote to `build/` when the Makefile was
read, and on a clean tree `build/` did not exist: the first build was 92,920 bytes without them and the next `make` 76,608
with them. The probe now makes `build/` first (decisions.md). The memory figures above were taken with the builds with packed
relocations and differ little from the ones without.

**Not measured or not checked.**
- A real lock on a real screen, one resolution, one output; real GPU rendering for hyprlock.
- The outline text on a screen (it is not hinted, so small text may look different), and a real dead-key layout: only
  `wtype -k dead_acute` was sent.
- `resources = minimal` with speech on, the text spoken for the minimal card, and a password check (only keys were pressed, not
  Enter). The default build's speech may have spoken "Enter your password" during its runs; it was not checked.
- Idle CPU is under one 10 ms tick in 10 seconds for all of them, so the real figure is lower than the resolution; the CPU per
  key is from 30 keys, so it moves by about 0.3 ms.
- Other tools that copied the RELR probe may have the same clean-build bug.
- Scripts were run from a scratch folder and are not in the repository.

## Text in other scripts

Text is laid out by `pacommon`'s `textlayout.[ch]` (shaping with HarfBuzz, the order of right-to-left and mixed text with
FriBidi, clusters, line breaks) and drawn by `src/render.c`. Paclock chooses the font of each character: the main font, else
the one of its script (Noto, loaded the first time it is needed), and for Han characters the Japanese, Korean, Simplified or
Traditional face of Noto CJK by the language. The direction of a message is the language's (right to left for Arabic, Persian
and Hebrew, whatever its first letter), not the first letter's. A message has up to three lines at its own `\n`; one without a
break goes on two where its script allows, else after the last letter that fits. A prompt in the field that is too wide is set
smaller (to 60%) before any of it is cut. Built with `I18N=0` (English only) none of this needs HarfBuzz or FriBidi: letters are
drawn one each, from left to right.

**Looked at** (2026-11-16, paclock in a headless sway at 1280x720, `grim` screenshots of `--demo` for each language, with
the prompt, Caps Lock, the lockout with and without the time, and a refused password): the Arabic, Persian and Hebrew
joined and read from the right, with the colons and full stops on the left; the Hindi and Bengali conjuncts and vowel signs, and the
Thai marks, in place; Japanese, Korean and both Chinese; and the Latin and Cyrillic languages. The lockout was faked in a scratch
copy (no real lockout was provoked). What the screenshots found, and was fixed: a message that starts with a Latin word in an
RTL language was laid out left to right; a long prompt was cut with an ellipsis (French, Spanish, Portuguese, Bengali, Japanese).

**Not done:** Thai, Lao, Khmer and Myanmar have no break between words (that needs a dictionary), so they break after the last
letter that fits; a conjunct of an Indic script is two clusters for the ellipsis cut; each wrapped line is its own paragraph; a
line of English inside an RTL message (the time PAM says is left, "9 minutes left to unlock") is placed by the bidirectional
algorithm, so its leading number lands at its end. Vertical text, and scripts nobody translated (Tibetan, Ethiopic, ...), were
not tried. The translations were not read by native speakers.

**The catalogs** (`po/*.po`): Portuguese, Spanish, French, German, Italian, Russian, Arabic, Persian, Hebrew, Hindi, Bengali, Thai,
Japanese, Korean and Chinese (`zh_CN`, `zh_TW`), all machine-written and not reviewed (the header says so). The stderr diagnostics are
not translated, as before. The lines that carry an explicit `\n` were measured with the layout against the card (about 22 em of the
message font): the widest is 21.9 em before it was shortened, and 18.9 em after (Japanese). A translation with a longer line gets an
ellipsis.

## Known limits

- **Only the password is checked, not the account.** paclock calls `pam_authenticate` and not `pam_acct_mgmt`, so a user whose account is expired or locked (`pam_faillock`, `pam_access`, `pam_time`) can still unlock with the right password. Why: [decisions.md](decisions.md).
