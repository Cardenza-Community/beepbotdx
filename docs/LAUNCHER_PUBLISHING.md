# 203 Launcher CI

This public fork preserves [bradmcnally/beepbotdx](https://github.com/bradmcnally/beepbotdx)
and adds two commits: Cardenza support, then 203 Launcher CI.

Pushes to `main` build/check Cardenza. Push the same reviewed commits to
`nightly` to build and upload a Nightly; a numeric `vX.Y.Z` tag uploads a Release.
Manual dispatch accepts `release_type`, a Release `tag`, and `delete_previous`.
The server defaults to keeping Releases and replacing previous Nightly files.
The repository never deletes releases or handles CDN/database keys.

## Enable cloud publication

1. Register this App with the Launcher management UI, approve `Cardenza-Community/beepbotdx`
   as a source, and authorize the `cardenza` target.
2. Set repository variable `LAUNCHER_APP_ID` to the registered UUID.
3. Store only that App's permanent/revocable token in Actions secret
   `LAUNCHER_APP_TOKEN`. Cloud provider keys stay on the server.
4. The server publisher needs GitHub upload permission for this fork.

Uploads use `POST https://launcher-api.203.io/publish`, required multipart
`release_type`, raw App BIN, and original license notices. The server validates
the source ref, image, size, hash and download mirrors before advertising it.
Builds run without cloud publication until the App ID is configured.

Before configuring credentials, review [CARDENZA.md](../CARDENZA.md) for license,
required data partitions and hardware limitations. A source build does not prove
device installation, audio, microphone or other peripherals. The source fork
does not grant a new license to the original author's code.
