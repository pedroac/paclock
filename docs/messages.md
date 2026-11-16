# Messages

What paclock says on the card, and what is said aloud when `speech` is on (the words are the same,
except where noted; a line break on the screen is a pause aloud). Never the password, and never
what is typed. The texts are translated (`po/`); the code is `src/main.c` and `src/announce.c`.

| When | On the card | Aloud, besides |
|---|---|---|
| Start | the field, labelled "Password" ("Enter your password" at `privacy = high` or `paranoid`, with no field until the first key) | "Enter your password" |
| `privacy = paranoid`, before the first key | "Press Enter when done." / "The length is not shown." | |
| The password is being checked | "Checking…" | |
| The password was refused | "Authentication failed" | the keyboard layout in use (a wrong one may be why), then "Enter your password" |
| The account is locked for a while | "Too many failed attempts." / PAM's time left, if it says ("9 minutes left to unlock") / "Ask the admin to unlock or wait." | |
| PAM could not be used | "Cannot check the password" | |
| Caps Lock is on | "Caps Lock is on:" / "Your password might not be accepted." | |
| The password is as long as it can be | "That is as long as it can be" | |
| The keyboard layout (when there are more than one) | "English keyboard" | where Tab, Down or Up moved to ("Type to change it"), the result of a search ("No keyboard matches"), and "Enter your password" when it comes back |
| A layout was picked with a password typed | "Password read again with this keyboard" | |
| A layout cannot be used | "That keyboard is not available" | |
| Unlocked | (nothing: the screen is gone) | "Unlocked" |

**A locked account.** `pam_faillock` refuses as for a wrong password, and says in an error message that
the account is locked. paclock keeps PAM's error messages during a check, and a refusal with one is
shown as locked, with the text in the last pair of parentheses of PAM's message as the time left. That
text is in the system's language, not paclock's. A module that sends an error message on a plain wrong
password would also show this message. See [decisions.md](decisions.md).
