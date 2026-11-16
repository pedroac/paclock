# AGENTS.md

For AI agents and people working in paclock. The rules for all the pac tools are in
[pacommon/AGENTS.md](pacommon/AGENTS.md); read it first, then [the principles](pacommon/PRINCIPLES.md).

```
make        # build; there must be no warnings
make test   # unit tests, no display needed
make install DESTDIR=/tmp/root PREFIX=/usr   # try the install in a scratch folder
```

- Before writing code against a library or protocol, read [pacommon/docs/apis.md](pacommon/docs/apis.md)
  and the headers of the installed version.
- Shared code is in the `pacommon` submodule; change it there (and commit the new pointer here), and run this tool's tests afterwards.
- Keep README.md lean and true: update it, the example `paclock.conf` and `docs/` with the change.
- Say what you tested and where. Do not commit or push unless asked.
