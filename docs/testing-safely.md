# Trying it safely

A locker that dies while the screen is locked leaves you locked out, so test changes in this order:

1. **`paclock --demo`**: a window, not a lock (fullscreen; password `demo`; Esc leaves). It checks drawing and typing, but **not the lock itself**: it never creates a lock surface, so lock-only bugs (such as a protocol error that kills the client) do not show up.
2. **A nested compositor**: the real lock, but only on a throwaway window. wlroots compositors (sway, labwc) run as a window inside your session:
   ```bash
   cat > /tmp/lock-test.conf <<'EOF'
   exec sh -c './paclock > /tmp/lock-test.out 2>&1; echo "exit=$?" >> /tmp/lock-test.out'
   EOF
   WLR_BACKENDS=wayland timeout 20 sway -c /tmp/lock-test.conf
   cat /tmp/lock-test.out
   ```
   Run `./paclock` from the build directory, not the installed one. If it is still running after a few seconds with nothing in the output, it locked. A compositor error such as `session lock surface is committed with a null buffer` appears here instead of locking your real screen. (`exit=1` with no message after `timeout` ends sway is normal: the compositor went away.)
3. **The real session, with a way out ready**: before the first run, know how to recover (below), and keep a TTY in mind (Ctrl+Alt+F3).

## If you are locked out (a red screen)

The compositor is holding the lock for a locker that is gone. Do not reboot:

* **Hyprland**: from a TTY,
  ```bash
  hyprctl --instance 0 'keyword misc:allow_session_lock_restore 1'
  hyprctl --instance 0 'dispatch exec hyprlock'   # or: paclock
  ```
  If `hyprctl` cannot find the instance, set `HYPRLAND_INSTANCE_SIGNATURE=$(ls /run/user/$UID/hypr | head -1)` first.
* **Other compositors**: from a TTY, start another locker with `WAYLAND_DISPLAY` set (usually `wayland-1`), or `pkill` the compositor to end the session.

## The audio stopped working after a failure

If `mute_streams` (or `privacy = medium` or higher) was on and paclock was killed or crashed, the sound it muted stays muted, and the players it paused stay paused: nothing undoes it while the session is locked, on purpose. Once you are back in the session, open the Volume Controller (first check that one is installed: `pavucontrol` for PulseAudio and PipeWire, for example), go to **Playback** and unmute the app. Press play in a player that was paused. Without a volume controller, `pactl set-sink-input-mute ID 0` does the same; the other commands are in [session.md](session.md).
