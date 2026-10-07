# reverse_proxy

`reverse_proxy` is a small, always-on supervisor that keeps a set of SSH
**reverse** forwardings (reverse tunnels) alive. It runs a guard loop (a `while`
loop that never returns). On every iteration it checks whether each configured
SSH forwarding process is still running; when one has died, it starts it again
by re-running the configured command.

This replaces the manual workflow of
[`../start_ssh_forwarding_in_background.sh`](../start_ssh_forwarding_in_background.sh),
which starts a single forwarding and then exits. Because that script exits, it
cannot bring the forwarding back after a crash or a network drop; `reverse_proxy`
stays resident and does exactly that.

## Settings file

The program reads its configuration from:

```
~/.reverse_proxy/settings.json
```

`~` is the value of the `HOME` environment variable. The file is a JSON object
with the following fields:

| Field | Type | Required | Description |
|-|-|-|-|
| `sshForwardingCommands` | array of strings | yes | The SSH forwarding commands to keep alive. Each entry is run through `/bin/sh -c`. The array must contain at least one non-empty string. |
| `checkIntervalSeconds` | integer > 0 | no | How many seconds to wait between two liveness checks. Defaults to `5`. |

Example `~/.reverse_proxy/settings.json` (this is the command used by
`start_ssh_forwarding_in_background.sh`, without the `nohup`/`stdbuf` wrapper,
which `reverse_proxy` handles itself):

```json
{
  "sshForwardingCommands": [
    "ssh -N -o ServerAliveInterval=30 -o ServerAliveCountMax=3 -o TCPKeepAlive=yes -o ExitOnForwardFailure=yes -R 127.0.0.1:60666:127.0.0.1:60666 user@example.com"
  ],
  "checkIntervalSeconds": 5
}
```

A ready-to-copy example also lives next to this file as
[`settings.example.json`](settings.example.json):

```shell
$ mkdir -p ~/.reverse_proxy
$ cp settings.example.json ~/.reverse_proxy/settings.json
# then edit ~/.reverse_proxy/settings.json if needed
```

### The forwarding command

Use the same `ssh` invocation that the existing script uses. The relevant flags
are:

- `-N` — do not run a remote command; only forward ports.
- `-o ServerAliveInterval=30 -o ServerAliveCountMax=3` — let SSH detect a dead
  connection (~90 s) instead of hanging forever.
- `-o TCPKeepAlive=yes` — keep the TCP connection alive.
- `-o ExitOnForwardFailure=yes` — make SSH exit instead of running without a
  usable forwarding, which lets the guard loop notice and restart it.
- `-R <bind_address>:<port>:<host>:<port>` — set up the reverse forwarding.
- `user@host` — the SSH destination.

Do **not** add `nohup`, `stdbuf`, output redirection, or a trailing `&`; the
program starts the command itself.

## Build

The project is built with CMake and only depends on `nlohmann/json`, which is
provided by the same prebuilt dependency folder used by the rest of
`agent_and_server`.

On Linux (matching the other build scripts in this repository):

```shell
$ cd reverse_proxy
$ ./build.sh
```

Or manually:

```shell
$ cd reverse_proxy
$ cmake -S . -B build
$ cmake --build build -j 4
```

The binary is produced at `reverse_proxy/build/reverse_proxy`.

To use a prebuilt dependency folder other than the default
(`/jiaolong_external/prebuilt`), point CMake at it:

```shell
$ JIAOLONG_PREBUILTS_PATH_ENV=/path/to/prebuilt cmake -S . -B build
```

On macOS, the prebuilts directory is typically
`$HOME/.jiaolong/jiaolong_external/prebuilt`:

```shell
$ JIAOLONG_PREBUILTS_PATH_ENV="$HOME/.jiaolong/jiaolong_external/prebuilt" \
    cmake -S . -B build
$ cmake --build build -j 4
```

## Run

Run it in the foreground (useful the first time, so you can read the log
lines):

```shell
$ ./build/reverse_proxy
```

Run it in the background so it survives the terminal session:

```shell
$ nohup ./build/reverse_proxy > ~/.reverse_proxy/reverse_proxy.log 2>&1 &
```

By default the settings file is `~/.reverse_proxy/settings.json`. Another path
can be given with `--settings`:

```shell
$ ./build/reverse_proxy --settings /path/to/settings.json
```

Stop the program with `Ctrl-C` (SIGINT) or `kill <pid>` (SIGTERM).

### Output

The program logs one line to stdout every time it starts or restarts a
forwarding, for example:

```
[reverse_proxy] Loaded 1 SSH forwarding command(s) from /home/user/.reverse_proxy/settings.json.
[reverse_proxy] Started: ssh -N -o ServerAliveInterval=30 ... user@example.com
[reverse_proxy] Forwarding is down, restarting: ssh -N -o ServerAliveInterval=30 ... user@example.com
```

The `ssh` processes inherit the program's stdout/stderr, so their own warnings
(e.g. a remote port that is already in use) appear in the same place.

## How it works

1. On startup the program loads and validates `~/.reverse_proxy/settings.json`.
2. It starts one child process per command. Each command is executed with
   `/bin/sh -c "<command>"`. The child is placed in its own session (`setsid`),
   so it is independent of the guard program's process group, and its standard
   input is redirected from `/dev/null`.
3. The guard loop then runs forever. Every `checkIntervalSeconds` seconds it
   checks each child with `waitpid(..., WNOHANG)`:
   - still running → nothing to do;
   - exited → print a message and start the command again.
4. `SIGINT`/`SIGTERM` break the loop and the program exits cleanly.

## Migrating from `start_ssh_forwarding_in_background.sh`

The old script does `nohup ... ssh -N ... &` and then tails the log. To move to
`reverse_proxy`:

1. Copy the `ssh ...` command (everything after `stdbuf -i0 -o0 -e0`, without
   `nohup`, `> ... 2>&1`, `</dev/null`, and `&`) into
   `sshForwardingCommands` in `~/.reverse_proxy/settings.json`.
2. Make sure no forwarding started by the old script is still running
   (`pkill -f "ssh -N .*-R 127.0.0.1:60666"`), otherwise the new SSH process
   will fail because the remote port is already bound.
3. Run `reverse_proxy` as described above.

## Notes and limitations

- `reverse_proxy` only tracks the processes it starts itself. Do not also run
  the forwarding command by hand (or via the old shell script) for the same
  tunnel, or you will end up with two processes competing for the same remote
  port.
- Every forwarding command is restarted independently; one failing command does
  not affect the others.
- The check interval is a trade-off: a shorter interval restarts a dead tunnel
  faster but checks more often. The default of 5 seconds is well below the
  ~90 seconds the SSH `ServerAlive*` options need to notice a dead link.