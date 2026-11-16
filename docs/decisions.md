# Decisions of paclock

Implementation decisions that are not obvious from the code (principle 52): what was decided, why, what else was considered, and when. Newest last.



## Limits and defaults in one `tunables.h`

- **Decided:** Every numeric limit, timing and default that was a `#define` in a source file moved to `src/tunables.h`, each under `#ifndef` so that `-DNAME=value` overrides it, with a `_Static_assert` that it is positive (principle 12). A name used in more than one file got its file's name as a prefix.
- **Considered:** Leave them where they were (rejected: principle 12 asks for one place); a generated `config.h` (rejected: more machinery for the same effect).
- **When:** 2026-11-15
- **Note:** Many comments in `tunables.h` only say what the value is and its unit; **why** each default is what it is has not been written down yet, and each such entry says so. Whoever knows the reason should write it there.

## Hardened build flags

- **Decided:** PIE, full RELRO, `-z now`, no executable stack, `-fstack-protector-strong`, `-fstack-clash-protection`, `_FORTIFY_SOURCE=3` and `-fcf-protection=full` where the compiler accepts it, added in the Makefile as `ALL_CFLAGS` / `ALL_LDFLAGS` so that overriding `CFLAGS` or `LDFLAGS` cannot drop them (principle 39).
- **Considered:** Putting the flags in `pac.mk` (not done: each tool's Makefile already carries them, and the tools differ in what they link).
- **When:** written down 2026-11-15; the flags were already in the Makefile

## Default flags: `-flto` and no unwind tables

- **Decided:** `CFLAGS` has `-flto -fno-unwind-tables -fno-asynchronous-unwind-tables` and `LDFLAGS` has `-flto`. paclock is C and nothing unwinds the stack. The hardening flags are unchanged.
- **Considered:** Each flag alone saved nothing (93,128 and 93,144 bytes against 92,936); together they saved 8,000 bytes, 8.6% (`SPEECH=none`, GCC, stripped, in a copy of the tree, 2026-11-16). `--build-id=none` and `-z noseparate-code` saved 80 bytes more and were left out.
- **Checked:** `make` has no warnings, and `make test` and `make sanitize` pass (602 checks), 2026-11-16. A crash backtrace from a core file has less to go on without unwind tables. Clang was not tried.
- **When:** 2026-11-16

## `LAYOUTS=0` builds without the choice of keyboard layout

- **Decided:** The Makefile has `LAYOUTS ?= 1`. `LAYOUTS=0` defines `NO_LAYOUTS`: `layout.c` is not compiled, and the picker (Tab, Ctrl+Space, the line under the password, its clicks) and `display_set_layout`, `display_use_group` and the group name functions are left out of `main.c` and `display.c`. Typing uses the compositor's keymap and group. The `layouts` setting is still accepted and ignored, so one config works for both builds.
- **Considered:** Leaving `render.c` alone: it draws only what `View` carries, and `View.layout` stays NULL. The unit tests always build `layout.c`, so they are the same for both builds.
- **Checked:** `make` and `make LAYOUTS=0` build with no warnings (in a copy of the tree, 2026-11-16); the stripped binary is 101,128 and 92,936 bytes. Not run by hand in a compositor.
- **When:** 2026-11-16

## `I18N=0` builds an English-only binary

- **Decided:** The Makefile has `I18N ?= 1`. `I18N=0` defines `NO_I18N`: `_()` becomes the text itself, `i18n.c` compiles to nothing, and no catalogs are built or installed (principle 10). Checked by building with `I18N=0` and finding no gettext symbols in `nm -D`.
- **Considered:** Keeping gettext always on (rejected: libintl is in glibc but not in every libc, and an optional feature must be removable). The unit tests were not run with `I18N=0` (they check translations).
- **Also:** with `I18N=0`, `main` does not call `setlocale`. Any non-C locale makes glibc map the whole locale archive, which was 2 MB resident of 12 MB (measured 2026-11-16, nested sway: RSS 12.0 MB with `setlocale(LC_CTYPE, "")`, 9.8 MB without). The only thing that needed the locale was the compose table, and `xkb_compose_table_new_from_locale()` takes just a name, so `setup_compose` reads it from `LC_ALL`, `LC_CTYPE` or `LANG`. Nothing in `src/` or `pacommon/src/` uses the C library's multibyte, collation or time-format functions. Dead-key typing with this build was not tried by hand.
- **When:** 2026-11-15

## The account is not checked at unlock (`pam_acct_mgmt` is not called)

- **Decided:** `auth.c` calls `pam_authenticate` only. The account stage (`pam_acct_mgmt`: expired or locked account, `pam_faillock`, `pam_access`, `pam_time`) is skipped, so a correct password unlocks even when a login would be refused.
- **Why:** the person is already logged in, and the worst failure of a locker is one that refuses the right password and leaves them at a locked screen. An expired password (`PAM_NEW_AUTHTOK_REQD`) in particular would need a change-password dialog paclock does not have. swaylock and most other Wayland lockers make the same choice.
- **Considered:** calling `pam_acct_mgmt` after a successful `pam_authenticate`, accepting `PAM_NEW_AUTHTOK_REQD` and refusing other account errors. It follows the login rules more closely, but changes when a correct password is refused. Rejected for now by the author.
- **Consequence:** an account that an administrator has locked or expired while its session is still running can still be unlocked. Ending that session is up to the administrator.
- **When:** 2026-11-15

## The session is quieted by running `busctl` and `pactl`, off by default

- **Decided:** three options, `lock_hint`, `pause_players` and `mute_streams` (all `no` unless set), done once the screen is locked and undone after it is unlocked. They run `busctl` (logind `Session.LockedHint`; MPRIS `Pause` and `Play`) and `pactl` (mute and unmute playback streams) with `posix_spawnp`, no shell, killed after `HUSH_TIMEOUT_MS`. Only what paclock changed is undone: players that were playing, streams that were not muted. `--demo` does none of it. Code: `src/hush.c`; how it works: [session.md](session.md).
- **Why streams and not the device:** muting the output also silences speech, and speech is the only way someone who cannot see the screen hears the lock. Streams of speech-dispatcher (binary `sd_*`, name `speech-dispatcher*`) are left alone.
- **Considered:** muting the default sink (rejected: silences speech); `libpulse` and `sd-bus` (rejected for now: a new dependency each, principle 4, where a program run once or twice is enough, principle 3); a watcher with `pactl subscribe` for streams that start while locked (not done: a second process and a loop; a known limit); running the programs in parallel (not done: simplicity, about 50 to 300 ms with the options on). Parsing `pactl list sink-inputs` as text, with `LC_ALL=C`, rather than its JSON: a few lines against a JSON reader.
- **Consequence:** if paclock is killed the screen stays locked and so do the muted streams, the paused players and the hint; session.md says how to undo them. The example `paclock.conf` turns the three on; the program's own default stays off.
- **When:** 2026-11-16

## `hush.c` allocates its read buffer when it needs it, not in bss

- **Decided:** the text that `busctl` and `pactl` print is read into a buffer of `HUSH_OUTPUT_MAX` (256 KiB) that `pause_players` and `mute_streams` `malloc` and free around the parsing; if `malloc` fails that step is skipped, like a program that is missing. The calls whose output is not wanted (`SetLockedHint`, `Pause`/`Play`, `set-sink-input-mute`) read into a 16-byte buffer. Before, it was one `static char output[HUSH_OUTPUT_MAX]`.
- **Why:** bss went from 324,064 to 62,040 bytes, with the limit unchanged (a long `pactl list sink-inputs` is still read whole). The file size does not change (bss is not stored), and the static buffer was mostly not resident, since pages are only touched when the programs print. The aim is to hold nothing while the options are off or between lock and unlock, and to compare fairly with swaylock, whose bss is under 1 KiB (53,900 bytes of text, 5,544 of data, 69,768 as a file, measured 2026-11-16).
- **Considered:** lowering `HUSH_OUTPUT_MAX` (rejected: it caps the number of streams that can be muted, about 3 KiB each); `mmap` (rejected: more code for the same effect); keeping it static (rejected: costs address space and a larger bss for a feature that is off by default).
- **Consequence:** two `malloc`s and `free`s at lock and at unlock, when the options are on; nothing when they are off. Tested with `make test` (602 checks, parsers only); the programs themselves were not run on a real lock after the change. The rest of bss (62 KiB) was not examined; `LIST_MAX` (1 MiB) and `render.c`'s `loaded[MAX_LOADED]` are the first to check.
- **When:** 2026-11-16

## `minimal = yes`: a small card with only the password line and its message

- **Decided:** a setting (`minimal`, off by default; no flag) that draws the card at `MINIMAL_SCALE` (0.7, `src/tunables.h`) times the usual size, with no username (`show_user` is ignored), and no choice of keyboard layout (the layouts are not loaded and the keys that pick one do nothing; the compositor's layout is still followed). The password line and the message under it (denial, Caps Lock, "Checking…") stay. `text_scale` still applies on top. Code: `card_scale` and `fill_view` in `src/main.c`.
- **Why:** the card's buffer is the largest piece of memory paclock owns, and swaylock's is much smaller (measured 2026-11-16, nested sway at 1280x720, idle, `SPEECH=none I18N=0`): the card's shared memory went from 1,007,616 to 258,048 bytes, RssShmem 1,040 to 308 kB, PSS about 3.1 to 2.8 MB, VmRSS about 10.4 to 9.8 MB (swaylock, white background: 60 kB shm, 1.8 MB PSS, 9.6 MB VmRSS). The binary grows by about 130 bytes.
- **Considered:** `text_scale = 0.5` alone (not the same: it keeps the layout row, which is most of the card's height, and the username); a build option that leaves the code out (rejected for now: two builds to test, and the code left out is the layout picker, which `LAYOUTS=0` already removes); dropping the message too (rejected: a refused password would show nothing, and the message is how someone who cannot see colours learns what happened); a smaller scale than 0.7 (the message is under 10 pixels high on a 720p screen).
- **Consequence:** with `minimal` a person on the wrong keyboard layout cannot switch it from paclock, only with the compositor's own keys. Not measured: with the card on more than one screen, or at a screen scale above 1; speech (the build measured has none); the text with a screen reader.
- **When:** 2026-11-16

## `text_size_min` and `text_size_max`: limits on the smallest text, with units

- **Decided:** two settings, each a number with a required unit (`mm`, `in`, `pt`, `px` or `%`), that hold the card's smallest text between them (the message under the password, or the layout row when it is shown); the rest of the card keeps its proportions. The parsing and the conversion are in `pacommon` (`length.[ch]`), and the output's size comes from `wl_output` (`geometry` physical width, `mode` current width; `Output.physical_mm`, `Output.mode_px`). A screen that reports no size, or one that makes under 50 or over 600 dpi, is taken as 96 dpi. Minimum above maximum: the minimum wins. No limit by default. Code: `layout_for` in `src/main.c`.
- **Why:** `text_scale` is relative to the screen's shorter side, so on a small screen, or with `minimal`, text could be unreadable, and nothing said how large it was in the world. A size in mm or pt says it.
- **Considered:** limits on `text_scale` only (what `PAC_TEXT_SCALE_MIN`/`MAX` already do: they do not see the screen, or `minimal`); limiting the password text instead of the smallest (the smallest is what has to stay readable). The existing cap that keeps the card no wider than the screen stays, and applies after these.
- **When:** 2026-11-16
- **Note:** a default (3 mm was suggested) is the author's to choose; none is set.

## The base text size is a real size, not a fraction of the screen

- **Decided:** every size in `src/tunables.h` is a fraction of one reference length: `REFERENCE_MM` (300) millimetres converted with the output's own size from `wl_output`, so `text_scale = 1` is the same real size on every screen (the password is about 12 mm high). An output that does not report a believable size (`pac_px_per_mm_known` is 0) keeps the old rule, the screen's shorter side. The card never grows past `PAC_TEXT_SCALE_MAX` times the shorter side, so it fits a small screen. No setting keeps the old sizing; `text_scale` changes the size. Code: `layout_for` in `src/main.c`.
- **Why:** a fraction of the screen made text a third as large on a laptop as on a large monitor, and made the card's pixels, and its memory (the largest piece paclock owns), grow with the screen. A real size is what is read, and is as large to the eye on every screen. mm and in are also easier to think in.
- **Considered:** keeping relative sizes by default (rejected for those reasons); 96 dpi where the size is unknown (rejected: tiny text on a 4K virtual screen); an option for the old sizing (rejected by the author: "just switch").
- **When:** 2026-11-16
- **Note:** `REFERENCE_MM` was tuned against a 24" monitor only, and is not yet checked on other screens. On a screen under about 120 mm tall the cap applies and text_size_min cannot be reached.

## `resources` levels in paclock

- **Decided:** `resources = normal | low | minimal` (shared with the other pac tools through `pac.conf`). `low` leaves out the username and the choice of keyboard layout, with the card at full size; `minimal` is `low` plus the small card of `minimal = yes`. A level never changes `contrast`, `speech`, `text_scale`, `text_size_min` or `text_size_max`: with a minimum of 3 mm, the minimal card is larger than the level alone makes it. (The separate `minimal` setting was removed in the next entry, and `privacy` is the entry after that.)
- **Why:** the card's buffer is the largest piece of memory paclock owns, and the username and layout list are what else it keeps; the author measured that the card's size changes RAM a lot. See the rules in `pacommon/docs/configuration.md` and principle 58.
- **When:** 2026-11-16

## `minimal` is gone: it is `resources = minimal`

- **Decided:** the `minimal` setting is removed; `resources = minimal` is the small card (the author: it would be the same). The decision "`minimal = yes`" above is kept as the history of the card itself. The `MINIMAL_SCALE` constant stays. There is no more way to ask for the full card at the `minimal` level; use `low`.
- **When:** 2026-11-16

## `privacy` levels in paclock

- **Decided:** `privacy = normal | medium | high | paranoid` (shared through `pac.conf`), each level including the ones before. `medium` pauses the players and mutes other sound while locked: the defaults of `pause_players` and `mute_streams`, which win when set (`settings_pause_players`, `settings_mute_streams`). `high` also leaves out the username and the layout choice (nothing loaded), and draws no password field while it is empty: the label reads "Enter your password" instead of "Password", with no cursor and no line, and the field shows on the first key (`View.hide_field`). Messages about what goes wrong stay. `paranoid` also draws a full row of dots whatever the length (`View.hide_length`), still with a cursor and a line, so it is clear that keys count. Speech, contrast and the size limits do not change.
- **Why:** the author listed these. The field appears on the first key (and not at no point) so that a person can tell the keys are being read: a screen that shows nothing at all would give no sign. The length is the one thing the dots always gave away.
- **Cost:** at `high`, without a layout choice, a person on the wrong layout can only switch it with the compositor's keys (as with `resources = minimal`). A full row of dots still shows whether anything was typed; backspacing to nothing hides the field again.
- **Not decided:** whether `high` should hide the refused-password text or Caps Lock; they stay (the author: minimum messages). Not tested on screen.
- **When:** 2026-11-16

## `resources = minimal` loads one font file

- **Decided:** at `resources = minimal` only the regular font file is loaded and the field text uses it too (`font_light` is ignored): `render_init(..., one_font)`. Why: in a measurement against swaylock the Light file was 212 kB of the card's 1,966 kB PSS, mapped by FreeType, for text that only differs in weight. Cost: the field text is regular weight, not light.
- **Considered:** loading no font file (rejected: the prompt and the messages are text; Cairo's own lookup loads a font too).
- **When:** 2026-11-16

## Less memory: text as outlines at `low` and `minimal`, and the compose table made when it is needed

- **Decided:** (1) at `resources = low` and `minimal`, text is drawn with `cairo_glyph_path` and `cairo_fill` and not `cairo_show_glyphs` (`render_set_text_paths`, `draw_glyphs` in `src/render.c`). Cairo then does not make its pixman glyph cache, a 266 kB allocation made when the first text is drawn. (2) at every level, the xkb compose table is made when the first key that may start a sequence is pressed (`compose_may_start` in `src/compose.h`), not at the start. The table is a 456 kB and a 168 kB allocation in `xkb_compose_table_new_from_locale`.
- **Measured** (the nested headless sway of docs/platform.md, 3 runs, PSS after one key, kB; minimal is `SPEECH=none I18N=0 LAYOUTS=0` with `resources = minimal`): minimal 1,838 (one font) to 1,348; default 2,460 (before this and the one-font change) to 2,305. swaylock with a plain colour: 1,189. A gdb breakpoint on `xkb_compose_table_new_from_locale` showed it not hit at the start or on a plain key, and hit once on a dead key.
- **Why a list of keys:** the 62 Compose files of libX11 hold about 12,400 sequences; all but 20 start with a dead key or Multi_key; the others start with backslash, the ch keys (0xfea0 to 0xfea5) or a Unicode keysym. `compose_may_start` covers all of them, and `test_compose_start` reads the Compose files that are installed and fails if one starts with a key it refuses. Without the table, a password with a dead key or a sequence ("~" then "a") would be typed wrong, so it is not turned off, only delayed.
- **Cost:** the first dead key is a few milliseconds slower. The outlines are not hinted, so small text may look different; not looked at on a screen. On a locale whose Compose file is not installed, a new sequence start would not be known to the test.
- **Not measured:** the outline text on a real screen; a real session with a dead-key layout (only `wtype -k dead_acute` on a headless sway); CPU while drawing outlines (the CPU per key did not change).
- **When:** 2026-11-16

## The RELR probe makes `build/` first

- **Decided:** the probe that decides whether to link with packed relative relocations (`RELR_LDFLAGS` in the `Makefile`) runs `mkdir -p build` first. It runs when the Makefile is read, before any rule, and on a clean tree `build/` was not there: the probe failed, the first build went without RELR, and the next `make` linked it again with RELR. The first build was 92,920 bytes and the next 76,608 (minimal: 76,536 and 64,320).
- **Found by:** comparing the sizes of a clean build and the second `make`, then relinking the same objects.
- **When:** 2026-11-16

## Privacy prevails over resources

- **Decided:** at every `resources` level, what `privacy` asks for is done: `privacy = medium` runs `busctl` and `pactl` (to pause the players and mute the streams) at `resources = minimal` too; `high` and `paranoid` draw the same whatever the level. `settings_pause_players` and `settings_mute_streams` look at `privacy` and what was set, never at `resources`. A test sets each combination. The rule is in `pacommon/docs/configuration.md` and principle 58.
- **Why:** the author: someone who sets a higher privacy level does it because it matters, and it could be a company policy.
- **Cost:** at `resources = minimal` with `privacy = medium`, paclock starts `busctl` and `pactl` and parses their output in a 256 KiB buffer, allocated only while it parses (`src/hush.c`); the figures in `docs/platform.md` are for no `privacy`.
- **When:** 2026-11-16

## Comparisons with other lockers are for reference, not a goal

- **Decided:** the figures against swaylock and hyprlock (`docs/platform.md`) say where paclock stands and what its parts cost. They are not a target, and a change is not made because it lowers a number. The memory savings at `resources = low` and `minimal` were taken only where they cost a person nothing they need, and the costs are listed in `docs/platform.md`. Nothing more is planned to close the remaining gap to swaylock's blank screen.
- **Why:** the author: an app for people should be simple, but not so minimal that it becomes inconvenient for the sake of the machine. Principle 57 says it for the code; this says it for the measurements.
- **When:** 2026-11-16

## Messages: lockout, Caps Lock and the paranoid note

- **Decided:** (1) A locked account (`pam_faillock`) is told from a wrong password by the error message PAM sends while it refuses: `converse` in `src/auth.c` keeps PAM's error messages (`pamtext_add`, `src/pamtext.c`: only `PAM_ERROR_MSG`, control characters as spaces, `PAMTEXT_MAX` bytes), and a refusal (`PAM_AUTH_ERR`) with one is `AUTH_LOCKED`, as `PAM_MAXTRIES` was. The screen then reads "Too many failed attempts." and, on a second line, the part of PAM's text in parentheses, which is where pam_faillock puts the time ("9 minutes left to unlock", in the system's language; `pamtext_hint`), or "Wait some minutes and try again." (it said "a moment" before: the default of pam_faillock is ten minutes). (2) Caps Lock reads "Caps Lock is on:" and "Your password might not be accepted.": the first line is what most people understand, the second says the consequence, briefly. (3) At `privacy = paranoid`, until the first key, "Press Enter when done." and "The length is not shown." (`fill_view` in `src/main.c`), so that a screen with no dots is not taken for broken. Speech says the same words as the screen; a line break is a space aloud (`announce.c`).
- **Why:** the author asked for the lockout to say that there were failed attempts and that a wait of some minutes follows, a Caps Lock message that explains in one short line, and an initial instruction for the paranoid level. A locked account looked like "Authentication failed" and the person kept retrying.
- **Cost, and what is not known:** a refusal with any PAM error message counts as locked: a module that sends an error text on a plain wrong password would show "Too many failed attempts" too. Only `pam_faillock` was read (its strings: "The account is locked due to %u failed logins.", "(%d minutes left to unlock)"); it was **not** run, as that would lock the author's account, so the real lockout screen is untested. The parenthesis rule is a guess that fits that module and not another's text. The wording of (3) is a proposal, not yet confirmed by the author. Portuguese translations are machine-written (see the catalog header).
- **When:** 2026-11-16

## The lockout message has three lines

- **Decided:** "Too many failed attempts.", then PAM's time left if it gave one ("9 minutes left to unlock", on a line of its own), then "Ask the admin to unlock or wait." It is one translated string, `"Too many failed attempts.\n%sAsk the admin to unlock or wait."`, where `%s` is the time with its own line break, or nothing (`locked` in `src/main.c`); this replaces the two strings of the entry above. A message may now have `MESSAGE_LINES` (3) lines (`draw_message`, and the card is as tall for them: `render_layout`), more breaks than that are spaces, and one with no break that is too long still goes on two.
- **Why:** the author's wording: the person is told what happened, how long, and that someone else can unlock it, which is the only way out of a lock longer than the wait.
- **Cost:** the card is one message line taller (about 3% of it) at every size, whether or not a message has three lines. The three-line drawing was not looked at on a screen.
- **When:** 2026-11-16

## A killed paclock does not restore the session

- **Decided:** if paclock dies without unlocking (SIGKILL, a crash, the OOM killer), the muted streams, the paused players and the locked hint are left as they are. There is no watchdog process that undoes them. The recovery commands are in `docs/session.md`.
- **Why:** the author: it should not happen, and if it does it is probably someone with root privileges trying to unstick a session. The compositor keeps the session locked when its locker dies, so restoring the sound and the players then would give out, on a locked screen, what `privacy = medium` (principle 58) and `mute_streams` exist to keep in. A SIGKILL cannot be caught, so the only way would have been a second process that sees the pipe close and runs the same code as an unlock.
- **Considered:** a watchdog that restores always (rejected: the privacy reason); one that restores only below `privacy = medium` (not chosen by the author); recording what was done in a file in `$XDG_RUNTIME_DIR` for the next paclock to undo (not done: paclock writes no file).
- **Cost:** after a kill the desktop stays muted and the players paused until someone runs the commands.
- **When:** 2026-11-16

## `MINIMAL_SCALE` is 0.7

- **Decided:** `resources = minimal` draws the card at 0.7 times the usual size (`MINIMAL_SCALE`, `src/tunables.h`). It was 0.4 for a while, to see how much memory the size saves (a lot: the card is the largest piece paclock owns).
- **Why:** the author tried 0.4: the field was fine, but the messages were too small. Memory at 0.7, measured as in `docs/platform.md`, three runs: PSS 1,355 kB before a key and 1,404 after one, against 1,297 and 1,347 at 0.4; the card's shared-memory buffer 148 kB against 92 kB. About 57 kB for messages that can be read.
- **Considered:** a size per part of the card (the field smaller, the message kept larger): not done; `text_size_min` also keeps the smallest text, the message, from going below a real size, and takes the whole card with it.
- **When:** 2026-11-16

## Catalogs: Spanish, French, German, Italian and Russian

- **Decided:** `po/es.po`, `fr.po`, `de.po`, `it.po` and `ru.po`, with the same strings translated as in `pt.po` (the diagnostics on stderr are left in English). Machine-written, and the header says "machine translation, not reviewed", as for Portuguese; the entries are not marked fuzzy, because gettext ignores a fuzzy entry and the catalog would then do nothing (the header carries the warning). `Plural-Forms` is set for each language though no string has a plural. Left out: right-to-left and complex scripts, which cannot be drawn correctly yet (`docs/platform.md`), and Chinese, Japanese and Korean, which draw but have no line breaking.
- **Wording kept short on purpose:** a line with an explicit break is cut with an ellipsis when it is over the card's width (about 22 em of the message font), so the lockout line 2 is "Ask the admin or wait" in each language, not the full sentence (the Portuguese one, 49 characters, was too long and was shortened too). Checked by measuring the lines in Noto Sans Regular.
- **Not known:** whether the translations are good. They need a native speaker; a catalog that is wrong is worse than English. Layout names come from the system's xkeyboard-config catalogs, not from these.
- **Superseded by:** the next entry, which adds the right-to-left, complex-script and CJK catalogs.
- **When:** 2026-11-16

## Text in every script, and the catalogs for it

- **Decided:** paclock draws text through `pacommon`'s `textlayout` (see its decision of the same day): `run_text` in `src/render.c` takes the glyphs and positions from `pac_text_layout`, groups the glyphs by face, and draws them with Cairo; the ellipsis cut and the wrap of a long message use `pac_text_next_cluster`, `pac_text_prev_cluster` and `pac_text_fit`. The tool's `pick_face` keeps the table of script fonts (Noto), and picks the face of Noto CJK for Han characters by the language (`cjk_face_index`: JP, KR, SC, TC, HK). `render_set_language` gives the layout the language (from `-l`, else `LANGUAGE`, `LC_ALL`, `LC_MESSAGES`, `LANG`), and the paragraph's direction is its language's (`pac_text_language_rtl`). Makefile: `harfbuzz fribidi` in `PKGS` unless `I18N=0`.
- **A prompt too wide for the field is set smaller (to `LABEL_MIN_SHRINK`, 0.6) before it is cut.** Found by looking at the screenshots: "Enter your password" was cut to "Saisissez votre mot…" (French), "Introduza a palavra…" and "Introduzca su contr…" at `privacy = high` and `paranoid`, and the verb of the Bengali and Japanese prompts was lost. Shortening every translation to fit was fragile, and the end of a prompt is what says what to do. The 3% margin is because the shrunk width was exactly the room and rounding tipped one letter over.
- **Ten more catalogs:** Arabic, Persian, Hebrew, Hindi, Bengali, Thai, Japanese, Korean, Chinese (`zh_CN`, `zh_TW`), on top of Portuguese, Spanish, French, German, Italian and Russian. Machine-written and not reviewed, as the header of each says; the entries are not marked fuzzy (gettext would ignore them). The lines with an explicit `\n` were measured with the layout against the card; Japanese caps line 2 was 21.9 em and was shortened to 18.9.
- **Cost:** 8.2 kB of binary and about 80 kB of PSS in the default build (the layout code, and HarfBuzz and FriBidi mapped: FreeType already maps HarfBuzz); the English-only build 4 kB and 40 kB.
- **Not known:** whether the translations are good; whether the shaping is right for every language and font (looked at with Noto Sans fonts on one machine); how it looks on a real screen at other sizes; right-to-left behaviour of the layout picker (the layout names come from xkeyboard-config) and of a username in an RTL language.
- **When:** 2026-11-16
