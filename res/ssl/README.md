# The certificate store

`cacert.pem` is Mozilla's root CA list as published by the curl project.

- Source: https://curl.se/ca/cacert.pem
- Fetched: 2026-08-29
- sha256: `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`,
  matching the `cacert.pem.sha256` curl publishes beside it
- 121 certificates, 189KB

## Why it is here

Every port ships it (`mk/plorpos-files.sh`) and the launcher passes it to curl
for every HTTPS request. The Mini Flip's firmware has no curl at all, so the
Flip carries its own (`mk/flip-cores/build-curl.sh`) beside this file.

It was added for the Brick, which ships `curl 7.54.1` linked against `OpenSSL/1.1.0i`, so it can speak
HTTPS. What it does not ship is anything to trust: there is no `/etc/ssl/certs`,
no `ca-certificates` package, and every TLS handshake fails with
`ssl_verify_result=20` - "unable to get issuer cert locally". Measured on the
device 2026-08-29; the same request with `--cacert` pointed at this file
returns `ssl_verify_result=0` and RetroAchievements answers.

The alternative to shipping this is `curl -k`, which turns off verification.
That is not on the table for requests that carry an account token.

The whole Mozilla list rather than only the roots RetroAchievements happens to
chain to today: pinning one issuer saves 187KB on a card measured in gigabytes
and buys a failure, months from now, that looks like a network problem and is
not.

## Updating

Refetch from the URL above, check the published sha256, and update the three
facts in this file. It is someone else's list; nothing here is edited by hand.
