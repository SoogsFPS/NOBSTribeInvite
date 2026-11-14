# NOBS Tribe Invite 1.0.0 — UNTESTED SOURCE BUILD

A standalone server-side ARK: Survival Ascended ASA API 2.11 plugin providing a chat-based alternative when the player radial interaction wheel is broken.

## Commands

- `/tribeup ExactCharacterName` — tribe owner or admin invites a currently online survivor **on the same map**. Character names with spaces are supported; exact match avoids selecting an unexpected player.
- `/tribeaccept` — accept invitation if the receiving survivor has no different tribe.
- `/tribeaccept confirm` — explicitly acknowledge leaving an existing **solo** tribe. **Old structures and dinos are not merged into the destination tribe**. Back up before testing this behavior.
- `/tribedecline` — refuse the pending invitation.

## Safety / behavior

- Two-minute expiry; 15-second inviter cooldown; in-memory pending invitations only.
- Six-member cap; rechecks owner/admin permission, inviter online status, tribe ID, and cap on acceptance.
- Rejects invitations to players currently in a different tribe with more than one member. If the recipient owns a solo tribe, explicit confirmation is required.
- Uses `AShooterPlayerState::AddToTribe(&tribe, false, false, true, inviterPC)` — merge=false, force=false, invite=true.
- Uses `AShooterGameMode::AllowAddToTribe` before join; no forced joins, no server admin commands, no direct mutation of `.arktribe` files, no SQL/database, no client mod.
- **Limitations**: does not repair dragging bodies, player inventory access, or radial menus. No cross-map invite support. Existing tribe ownership is NOT transferred. Do not use on a production map until tested with disposable tribes, structures and dinos.

## Build in GitHub Actions

1. Create a new GitHub repository named `NOBSTribeInvite` (private is fine).
2. Upload **the contents of this ZIP** to repo root, preserving `.github/workflows/build.yml`.
3. In GitHub, open **Actions → NOBS Tribe Invite - Windows x64 → Run workflow**.
4. When the green build completes, download artifact `NOBSTribeInvite-Windows-x64-UNTESTED`.
5. It should contain `NOBSTribeInvite.dll` and `PluginInfo.json` (possibly `.pdb` and a dependency). If compile errors occur, send the workflow log for corrections.
6. With the Island test server stopped, install under `ShooterGame/Binaries/Win64/ArkApi/Plugins/NOBSTribeInvite/`.
7. Restart and look for the startup message `[NOBSTribeInvite] v1.0.0 registered`.

## Test sequence

1. Back up the entire Island test save, including `.arktribe` and `.arkprofile` files.
2. With two disposable survivors in separate tribes, have an owner type `/tribeup TargetName`; accept (or `/tribeaccept confirm` for solo-tribe owner).
3. Verify actual target `TribeID`, membership, inventory, old and new structures, and server logs after relog/restart.
4. Test `decline`, expiration, duplicate names, full six-member tribe, unauthorized invitors, offline invitor and multi-member tribe rejection.
5. Before any production installation, verify server mod/plugin interaction behavior against Lethal Protection and your 6-player limits.

## Technical verification

Source is built against published ASA API 2.11 headers and Windows MSBuild setup; **no Windows x64 DLL has been compiled or game-tested in this environment**. The GitHub Actions workflow is provided for that step.

## References

- ASA API 2.11 public headers: https://github.com/ArkServerApi/AsaApi/tree/2.11/AsaApi/Core/Public
- Plugin template: https://github.com/MolluskARK/ASA-Plugin-Template
