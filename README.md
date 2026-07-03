# RTP (Red Team Proxy)



> A compact red team tunnel and proxy utility written in pure C.  
> Small footprint, single-binary deployment, mutation-aware builds, and field-friendly command flow.

## Overview

RTP is built for operators who want practical traffic plumbing without dragging in a heavy control plane.

It focuses on:

- small binaries
- minimal dependencies
- direct CLI usage
- portable cross-platform builds
- fast operator-side scenario switching

RTP is not a config-heavy proxy platform.  
It is a compact traffic tool designed to stay simple under pressure.

## Why RTP

- **Pure C**: no OpenSSL, no libuv, no heavyweight runtime stack.
- **Lightweight**: single process, single event loop, single binary.
- **Simple commands**: most scenarios are driven by `-l`, `-r`, `-k`, and `--tls`.
- **Mutation-aware builds**: the GUI builder regenerates a mutation header for each build.
- **Cross-platform output**: Windows, Linux, and macOS builds are managed from one builder entry.
- **Practical scenario coverage**: TCP forward, TCP bridge, SOCKS5, reverse SOCKS, reverse service, and UDP forwarding.

## Scenario Matrix

| Scenario | Description | Implemented |
|---|---|---|
| TCP Forward | Local listener to remote target | Yes |
| TCP Bridge | Dual-listener bridge | Yes |
| SOCKS5 | Local SOCKS5 entry point | Yes |
| Reverse SOCKS5 | Reverse SOCKS5 with browser-friendly multi-connection support | Yes |
| Reverse Service | Expose an internal service through a reverse tunnel | Yes |
| UDP Forward | Local UDP listen to remote UDP target | Yes |

## Evasion Snapshot

RTP does not depend on one noisy trick. Its evasion model comes from reducing stable signals across build output, binary shape, and tunnel behavior:

- **Mutation-aware builds**: each build refreshes key symbol mappings to reduce stable static signatures.
- **Pure C and smaller footprint**: fewer runtime layers mean fewer framework fingerprints.
- **Release-oriented output**: the default build flow favors smaller artifacts with less debug residue.
- **Tunnel-side traffic shaping**: optional `-k` and `--tls` help soften the tunnel-side traffic profile.

The idea is not to hide behind complexity. It is to ship a smaller, cleaner, lower-noise payload.

## Quick Start

### Launch the Builder

```bash
python RTP.py
```

The GUI provides:

- target OS and architecture selection
- custom compiler path support
- bilingual UI
- live command generation
- one-click command copy

### Common Commands

#### TCP Forward

```bash
rtp -l 3389 -r 192.168.1.10:3389
```

#### Local SOCKS5 Entry

```bash
rtp -l 1080
```

#### SOCKS5 with Username/Password

```bash
rtp -l 1080 --user admin --pass 123456
```

#### Reverse SOCKS5

```bash
# Server
rtp -l 9998 -l 7777 --reverse-socks

# Target
rtp -r <SERVER_IP>:9998 --reverse-socks --reconnect

# Operator
# Use <SERVER_IP>:7777 as the SOCKS5 endpoint
```

#### UDP Forward

```bash
rtp -l 53 -r 8.8.8.8:53 -u
```

## Address Semantics

RTP keeps endpoint parsing intentionally simple:

- `9998` expands to `0.0.0.0:9998`
- `:9998` expands to `0.0.0.0:9998`
- `*:9998` means listen on all interfaces
- `@host:port` explicitly marks an RTP tunnel endpoint

Examples:

```bash
rtp -l 9998
rtp -l *:1080
rtp -r 192.168.1.10:3389
rtp -r @1.2.3.4:9998 -k "secret" --tls
```

## Evasion Core Idea

RTP does not chase stealth through complexity.  
Its evasion model is based on reducing stable detection surface.

### 1. Per-build mutation

The builder refreshes `include/mutated.h` and rewrites key symbols at build time.  
The result is that repeated builds do not produce the exact same symbol layout.

### 2. Smaller static footprint

Pure C, fewer dependencies, and a tighter binary reduce obvious framework-level signatures.  
A lot of tooling becomes noisy long before it becomes useful.

### 3. Stripped release output

The default build flow favors release-style output, removing unnecessary debug artifacts and shrinking static identifiers.

### 4. Tunnel-side traffic shaping

- `-k`: RC4 encryption
- `--tls`: TLS AppData-style wrapping
- `@`: explicit RTP tunnel endpoint marker

This is not a standards-compliant TLS implementation.  
It is a practical traffic-shaping layer for RTP tunnel links.

## Lightweight by Design

RTP is valuable not just because it forwards traffic, but because it does so with very little overhead.

- smaller binaries
- fewer moving parts
- simpler deployment
- lower operator error rate
- faster scenario switching in the field

In one line:

> RTP is not a heavy platform. It is a lightweight traffic scalpel.

## Supported Capabilities

### Network

- TCP `L-R`
- TCP `L-L`
- active outbound mode
- SOCKS5 `CONNECT`
- SOCKS5 `NO AUTH`
- SOCKS5 `USERNAME/PASSWORD`
- session-based UDP forwarding
- reverse SOCKS primary pooling

### Build

- Windows Modern / Legacy
- Linux `x64` / `arm64` / `x86`
- macOS Universal
- Zig-first cross-compilation workflow
- GUI command generator

## Option Summary

| Option | Description |
|---|---|
| `-l`, `--local` | Local listen address, may appear twice |
| `-r`, `--remote` | Remote connect address, may appear twice |
| `-k`, `--key` | Enable RC4 and set the key |
| `--user` | SOCKS5 username |
| `--pass` | SOCKS5 password |
| `-u`, `--udp` | Enable UDP forwarding |
| `-q`, `--quiet` | Fully silent mode |
| `--tls` | Enable TLS-style wrapping |
| `--reverse-socks` | Enable reverse SOCKS control handshake |
| `--reconnect` | Enable auto-reconnect for active outbound modes |

## Build Recommendation

Use the GUI builder as the primary release entry:

```bash
python RTP.py
```

If you build Linux targets on Windows, prefer Zig and keep `zig.exe` together with its adjacent `lib/` directory.

## Disclaimer

This project is provided for authorized security research, adversary simulation, and defensive validation only.  
Operators are responsible for ensuring lawful use in their own environment and jurisdiction.

## GitHub

- GitHub: https://github.com/your-org/RTP
- If this project helps you, please consider giving it a Star on GitHub.
