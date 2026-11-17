# Security

* **The lock is the compositor's.** paclock only asks for it (`ext-session-lock`): while locked, the compositor shows nothing but paclock's surfaces and sends input nowhere else. If paclock crashes or is killed, **the screen stays locked** (switch to a VT and log in to run another locker or restart the compositor), and what it did to the audio stays done ([audio](testing-safely.md#the-audio-stopped-working-after-a-failure)). Only the right password lets go of the lock.
* The password is checked by PAM in a separate thread, as the user who runs paclock; it is wiped after use and kept out of core dumps. Only a password prompt can be answered: a one-time code or fingerprint in the PAM stack makes the check fail. The account is not checked (`pam_acct_mgmt`): an expired or locked account with the right password still unlocks, see [decisions.md](decisions.md).
* **Speech says only what is on the screen.** The messages go to the user's speech-dispatcher; the password and the keys typed never do. The connection is opened before the screen is locked, and a missing server only means silence. `speech = no` (or `SPEECH=none`) opens no connection.
* **Optional, off by default:** `lock_hint`, `pause_players` and `mute_streams` (the last two are also turned on by `privacy = medium` and above) run `busctl` and `pactl` (found in `PATH`, no shell, killed after a second) to quiet the rest of the session; nothing is sent anywhere else.
* If another locker is running, or the compositor lacks the protocol, paclock says so and does not pretend to lock.

## What it stores

Nothing. paclock writes no file: it reads its configuration, and keeps the password in memory only until it has been checked, then wipes it.
