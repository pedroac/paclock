# paclock
**Lean screen locker, but with the right features for humans.**

![Screenshot](docs/screenshot.png)

- **Keyboard layout:** choose another layout on the lock screen, in case you got into the session with the wrong one. Dead keys and compose sequences work in the password.
- **Localization:** all texts go through gettext, and are drawn right in every script: shaped, and in the right order for right-to-left text.
- **Accessibility for the visually impaired:** speech through speech-dispatcher, high contrast, and text you can scale or limit by real size.
- **Useful feedback:** it says what to do and what happened, in words and by speech.
- **Less information leakage:** `privacy` levels hide the username, the layout and the password field, and at `paranoid` the length of the password; from `medium` they also pause the media players and mute other sound while locked.
- **Light, and only what you use:** C, Cairo, FreeType, xkbcommon and PAM; no GTK or Qt. Optional features can be turned off in the settings, and speech, translations and the layout choice can be left out of the build.
- **No setup:** it works out of the box, and looks like [pacgreet](https://github.com/pedroac/pacgreet).

## Quick start

```bash
git clone --recurse-submodules https://github.com/pedroac/paclock
cd paclock
make && sudo make install
paclock
```

Needs a Wayland compositor with `ext-session-lock-v1` (run on sway and Hyprland). To try it without locking anything: `paclock --demo` (a window; password `demo`; Esc leaves).

**Before your first real lock, read [Trying it safely](docs/testing-safely.md)**: if a locker dies while locked, you need to know the way out.

Everything works without configuration. To change something, copy [`paclock.conf`](paclock.conf) to `~/.config/pac/paclock.conf`.

## Docs

- [Building and installing](docs/building.md): PAM setup, build options, size
- [Configuration](docs/configuration.md): every option, keys, translations
- [Trying it safely](docs/testing-safely.md): demo, nested compositor, getting out when locked, audio left muted
- [Security](docs/security.md): what the lock is, the password, what it stores
- [Messages](docs/messages.md): what the card says and what is said aloud
- [The session while locked](docs/session.md): pausing players and muting sound
- [Platform](docs/platform.md): what assumes Linux, and what was run
- [Decisions](docs/decisions.md): why it is the way it is
- [Contributing](CONTRIBUTING.md)

## AI Assistance Disclosure

The tools were Claude (code generation), Dropstone (review and improvement), and Gemini and ChatGPT (investigation), as [the principles](pacommon/PRINCIPLES.md#ai-assisted-human-owned) describe. They were used during the development of paclock for:

- Architecture & Ideation: Discussing design trade-offs (e.g., C-first philosophy, avoiding heavy dependencies, and leveraging Wayland protocols).
- Quality & Security Auditing: Reviewing code for correctness, safety, and reliability.
- Code Refinement: Structuring low-level Wayland, Cairo and input-handling calls.
- Documentation & Localization: Drafting documentation and initial translation catalogs (po/).

All code was reviewed, tested, and curated by the human author to ensure performance, security, and minimal overhead.

## License

MIT, see [LICENSE](LICENSE).
