# The session while locked

Three options, all off by default, each done once the screen is locked and undone once it is
unlocked (after the lock is let go, so nothing here can keep the screen locked). Each runs a program
found in `PATH`, without a shell, killed after `HUSH_TIMEOUT_MS` (1 s); the code is `src/hush.c`.
`--demo` locks nothing, so it does none of them.

| Option | Lock | Unlock | Program |
|---|---|---|---|
| `lock_hint` | `Session.LockedHint` = true on `/org/freedesktop/login1/session/auto` (system bus) | false | `busctl` |
| `pause_players` | `Pause` on each MPRIS player (`org.mpris.MediaPlayer2.*`, session bus) whose `PlaybackStatus` is `Playing` | `Play` on those only | `busctl` |
| `mute_streams` | `set-sink-input-mute 1` on each playback stream that is not muted and not speech-dispatcher's | `0` on those only | `pactl` |

Why per stream and not the device: muting the output would silence the speech too, and speech is
the one thing a person who cannot see the screen needs. A stream is speech-dispatcher's if its
`application.process.binary` starts with `sd_` or its `application.name` with `speech-dispatcher`.
`pactl` is run with `LC_ALL=C` (and without `LANGUAGE`) in its environment, because its words are
translated and paclock sets `LANGUAGE` for its own texts.

`busctl list --acquired` is used so that a player that is only *activatable* is not started by
asking its status.

## Limits

* A stream that starts after the lock (a video, a notification sound) is not muted; that would need
  a watcher (`pactl subscribe`), not done.
* Nothing prevents the volume being changed: a compositor binding that works while locked still
  does. Changing the device volume does not unmute a muted stream.
* If paclock is killed (SIGKILL, a crash, the OOM killer) the screen stays locked, and so does what was done, on purpose: nothing
  undoes it, because the session is still locked and what `privacy` or these options keep from it should stay kept. A paclock
  started afterwards undoes only what it did itself, not what the one that was killed did. By hand: `pactl set-sink-input-mute
  ID 0`, `busctl --user call NAME /org/mpris/MediaPlayer2 org.mpris.MediaPlayer2.Player Play`, and
  `busctl --system call org.freedesktop.login1 /org/freedesktop/login1/session/auto
  org.freedesktop.login1.Session SetLockedHint b false`.
* The programs run while the screen is already locked, one after another, after the first frame is
  drawn; a slow bus delays reading the keyboard by up to a second per program run.
* Paused players are played again even if something else started them meanwhile.
