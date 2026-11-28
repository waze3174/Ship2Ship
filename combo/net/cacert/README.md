# cacert.pem

curl's extract of Mozilla's CA root list (<https://curl.se/ca/cacert.pem>), MPL-2.0 like the Mozilla data it comes from.

- **Mozilla data as of:** Thu Nov 5 04:12:01 2026 GMT
- **SHA-256:** `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285` (equal to <https://curl.se/ca/cacert.pem.sha256> when it was fetched on 2026-11-28)
- **Used for:** the CA roots the Archipelago client trusts for `wss://` when the system's are missing. It is the whole trust on macOS, and a fallback on Windows (whose root store fills up on demand) and Linux. See `combo/net/ComboApTls.h`.
- **Shipped as:** `cacert.pem` beside the game (Windows zip, Linux zip and AppImage), in `Contents/Resources` (macOS app).
- **Updating it:** replace the file with a newer download, check it against the published `.sha256`, and change the date and hash here and in `CREDITS.md`. The file must stay byte for byte as published (`.gitattributes` keeps git from changing its line endings).
