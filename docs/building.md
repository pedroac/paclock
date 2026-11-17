# Building and installing

```bash
make && sudo make install
```

The shared code is the `pacommon` submodule: clone with `--recurse-submodules` (or run `git submodule update --init`), or set `PACOMMON=` to another checkout. `make test` runs the unit tests.

`make install` also puts `/etc/pam.d/paclock` in place, which says what the password is checked against (`auth include system-auth` on Arch and Fedora, `common-auth` on Debian and Ubuntu, chosen at install time from what is in `/etc/pam.d`; `make install PAM_STACK=name` overrides it: the same as logging in). Edit it if your distribution calls that differently. **paclock refuses to lock if the file, or the stack it includes, is missing**, because PAM would then refuse every password.

Needs a compositor with `ext-session-lock-v1`, viewporter and single-pixel-buffer. It was run on sway and Hyprland (the author's); others that have the protocols should work, but are untested ([platform](platform.md)).

## Build options

| Option | Default | |
|---|---|---|
| `I18N=1` | on | translated texts (gettext). `I18N=0` builds an English-only binary and installs no catalogs. |
| `LAYOUTS=1` | on | the choice of keyboard layout under the password. `LAYOUTS=0` builds without it: only the compositor's keymap is used, and `layouts` in the config is ignored. |
| `SPEECH=speechd` | on | speaks the messages through speech-dispatcher (libspeechd). `SPEECH=none` links no speech library. `atspi` is not supported. The speech code is shared, in pacommon's `speech.[ch]`. |

## Size

The binary is 83 KiB stripped with the default options (`make`, GCC `-Os -flto`, packed relocations where the system supports them; it links HarfBuzz and FriBidi for the scripts) and 71 KiB with `SPEECH=none I18N=0 LAYOUTS=0` (English only, neither library). A real lock at `resources = minimal` in that build uses about 1.4 MB of proportional memory (PSS), and about 2.4 MB by default; for reference, swaylock's blank screen uses about 1.2 MB and hyprlock about 58 MB. The numbers are not a goal: paclock is made to be easy to use first. How they were measured, and what was not, in [platform](platform.md#compared-with-swaylock-and-hyprlock-for-reference-2026-11-16).
