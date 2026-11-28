# Archipelago online client: wss:// and certificate trust

Ship2Ship's Archipelago client (`combo/net/ComboArchipelago.cpp`) is Ship2Ship's own code. This file records the one
vendored library it changes, and how its `wss://` connections are secured, so a later library update does not undo it.
Nothing in `soh/` or `mm/` is involved.

## wswrap is vendored, with three changes

`combo/net/wswrap/` is [black-sliver/wswrap](https://github.com/black-sliver/wswrap) at `d0505e0ec53a26743f11051949a0dc66bcf44951`
(MIT; its `LICENSE` is beside it), the commit junkx420's clients pin. It used to be fetched at configure time. It is
vendored because stock wswrap, for a `wss://` server that is not on this machine, checks that the certificate chain
leads to a trusted root and nothing else: **any** valid certificate, for any host, passed. The changes in
`wswrap_websocketpp.hpp` (each marked `Ship2Ship`):

1. The TLS setup (protocol floor, CA roots, verification) is `ComboApTls::ConfigureContext` (`combo/net/ComboApTls.h`)
   instead of the inline block, so the client and the certificate probe run the same code. The server's host name is
   passed in, and the loopback exemption (`localhost`, `127.0.0.1`, `::1`: no verification) stays in wswrap.
2. The verification callback also checks that the certificate is valid for the host name (or IP address) that was
   asked for (`asio::ssl::host_name_verification`, RFC 6125). SNI is set by websocketpp already.
3. A refused certificate is reported by name ("certificate could not be verified: ...") instead of websocketpp's
   "TLS handshake failed". `ComboArchipelago.cpp` shows that text on the status line and in the console, and **stops**
   the attempt (phase "refused"): apclientpp's next try for an address typed without a scheme is plain `ws://`, which
   would send the room password in the clear to whoever answered.

`wswrap.hpp` lost its Emscripten branch. `combo/net/wswrap/.clang-format` turns formatting off, so the file stays
comparable with upstream. When wswrap is updated, reapply the three changes.

## Where the CA roots come from

Order: `SSL_CERT_FILE` / `SSL_CERT_DIR` if set and readable, else the system's; then the bundled `cacert.pem` is
**added** (the trusted set is their union).

| System | OpenSSL | System roots | Bundled `cacert.pem` |
|---|---|---|---|
| Windows | vcpkg `openssl` (static, with the dynamic CRT) | the "ROOT" store, read with `CertEnumCertificatesInStore` | beside `Ship2Ship.exe` |
| Linux (zip and AppImage) | the distribution's `libssl-dev`, **static** (`libssl.a`, `libcrypto.a`) | `/etc/ssl/certs/ca-certificates.crt`, `/etc/pki/tls/certs/ca-bundle.crt`, `/etc/ssl/cert.pem`, `/etc/ssl/ca-bundle.pem` (first that loads), else `/etc/ssl/certs` as a directory | beside `Ship2Ship` (`usr/bin` in the AppImage) |
| macOS (arm64 app) | Homebrew `openssl@3`, **static** | none: OpenSSL cannot read the Keychain | `Contents/Resources/cacert.pem`; it is the whole trust |

A statically linked OpenSSL looks for roots in the directory it was built with, which is the build machine's, so Linux
probes the host's usual places itself. Static linking keeps the Linux zip and the AppImage independent of the host's
libssl version (SteamOS included), and the macOS app independent of Homebrew.

`combo/net/cacert/cacert.pem` is curl's extract of Mozilla's CA list (MPL-2.0), unchanged; its date and hash are in
`combo/net/cacert/README.md` and `CREDITS.md`. Update it by replacing the file, never by editing it.

## What is tested

- `scripts/ap/tls_probe.py` runs the certificate probe (`comboApClientHarness --tls-probe host:port`, or
  `Ship2Ship --ap-tls-probe host:port`): one handshake with archipelago.gg through the client's own TLS setup. It must
  verify, must be refused when the certificate has to be valid for example.org, and, for the harness, must verify with the
  bundle alone and be refused with an unrelated root. With no network it skips with a warning. CI runs it on Windows,
  Linux (harness and AppImage) and macOS.
- `scripts/ap/client_e2e.py --wss` runs the real client against Archipelago's own MultiServer on this machine: wss:// to
  `127.0.0.1`, and the certificate checks on `127.0.0.2` (a trusted certificate for that address connects; an untrusted
  one and a trusted one for another address are refused with their reasons). CI runs it on Linux.

## Left as it was

- A server on `localhost`, `127.0.0.1` or `::1` is not verified: a self-hosted room has a certificate of its own making.
- Certificate revocation (CRL, OCSP) is not checked, as in stock wswrap and most clients.
