# Configuration

Everything works without any. paclock reads `/etc/pac/paclock.conf`, then `~/.config/pac/paclock.conf` (or `$XDG_CONFIG_HOME/pac/paclock.conf`), then `paclock.conf` in the current folder, each overriding the earlier: one `key = value` per line, `#` starts a comment, bad lines are reported on stderr and skipped. [`paclock.conf`](../paclock.conf) lists every option.

```
paclock [-C CONTRAST] [-l LANGUAGE] [--config FILE] [--demo]
```

| Config key | Option | |
|---|---|---|
| `contrast` | `-C` | `normal`, or `high`: black and white, nothing dimmed |
| `text_scale`, `font`, `font_light` | | size (1 is the same real size on every screen: the password is about 12 mm high; a screen that does not report its size gets text in proportion to its height), and font files |
| `text_size_min`, `text_size_max` | | limits on the smallest text, as a number with a unit: `mm`, `in`, `pt`, `px` or `%` of the screen's shorter side, e.g. `3mm`. mm, in and pt use the screen's own size (96 dpi if it does not say); no limit by default |
| `privacy` | | `normal` (default), `medium` (pause the players and mute other sound while locked), `high` (also: no username or layout choice, no password field until the first key, only "Enter your password" and what goes wrong) or `paranoid` (also: a full row of dots whatever the password length). Never changes contrast or speech, and wins over `resources`: `medium` runs `busctl` and `pactl` even at `resources = minimal`. Can be set for every pac tool in `pac.conf` |
| `show_user` | | show the username, small and dim, above the password (off by default: whoever is at the screen can read it) |
| `resources` | | `normal` (default), `low` (no username or layout choice) or `minimal` (also a small card: only the password line and its message, everything 0.7 times the size). Can be set for every pac tool in `pac.conf`, together with `text_size_min`, `text_size_max`, `contrast`, `speech` and `privacy`; paclock's own file wins. Never changes contrast, speech or the size limits. See [pacommon/docs/configuration.md](../pacommon/docs/configuration.md) |
| `speech` | | `yes` (default) or `no`: say the messages of the card aloud, and "Unlocked", for people who cannot see the screen: [messages.md](messages.md) lists them all, including the lockout and Caps Lock ones. Never the password or what is typed. Needs speech-dispatcher running, else paclock says so and stays silent |
| `lock_hint`, `pause_players`, `mute_streams` | | `no` (default) or `yes`, each once the screen is locked, undone on unlocking: tell logind the session is locked (`busctl`, `LockedHint`), pause the playing MPRIS players (`busctl`) and play them again, mute other programs' sound but not speech-dispatcher's (`pactl`). A missing program is said once and skipped; a stream that starts while locked is not muted. Not done by `--demo`. See [session.md](session.md) |
| `language` | `-l` | the language of the texts, e.g. `pt`; overrides the environment's |

## Keys

| Key | |
|---|---|
| Enter | unlock |
| Backspace, Ctrl+Backspace or Ctrl+U | delete a character, clear the field |
| Esc | clear the password and any message |
| Tab or Down | choose another keyboard layout: type to search, Left and Right step, Enter picks, Esc leaves. Ctrl+Space (or Super+Space) goes to the next one. Not there when there is only one, and not at `resources = low` or `privacy = high` |

Dead keys and Compose work. A yellow note appears when Caps Lock is on ("Caps Lock is on:" and, on a second line, "Your password might not be accepted."; the same is spoken). Every screen shows the same field; a screen plugged in while locked is covered at once.

## Translations

They work as in [pacgreet](https://github.com/pedroac/pacgreet): gettext, domain `paclock`, `po/`. Add a language with a `.po` file there. See [platform](platform.md#text-in-other-scripts).
