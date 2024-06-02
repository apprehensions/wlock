# wlock

wlock is a itty-bitty simple screenlocker in C99 for Wayland compositors
that support the `ext-session-lock-v1` protocol.

Excerpt from the protocol specifying the behavior:
> The client is responsible for performing authentication and informing the
> compositor when the session should be unlocked. If the client dies while
> the session is locked the session remains locked, possibly permanently
> depending on compositor policy.

## Building

Build prerequisites:

- pkg-config
- wayland
- wayland-protocols
- xkbcommon

After installing the dependencies, you may build and install wlock:
```
make
make install
```

## Usage

Run `wlock`.

See the wlock help page (`wlock -h`) for more details.
