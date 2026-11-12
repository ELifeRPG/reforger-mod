---
icon: lucide/link
---

# Account linking

How a join links a Game Identity to a web account, and what the mod does with the answer. The
server decides everything; the client only shows the screen it is sent.

Code: `ELIFE_JoinSessionManagerComponent` (decisions), `ELIFE_JoinIdentity` (identity and platform),
`ELIFE_JoinLinkScreen` (link screen), `ELIFE_CharacterIdentity` (character id).

## Flow

- **Hook:** `ELIFE_JoinSpawnLogic.OnPlayerAuditSuccess_S` calls `StartSession(playerId)`. Disconnect
  calls `EndSession`.
- **Identity:** `ELIFE_JoinIdentity.Resolve` at session start. Outside Workbench it is the Bohemia
  identity id. In Workbench it is a fixed low GUID, so no real id leaves the machine.
- **Platform:** `ELIFE_JoinIdentity.ResolvePlatform` at session start, stored per session. It reads
  the engine's `PlatformKind` name at runtime with `typename.EnumToString`, because the enum member
  names are unconfirmed. `MapPlatformKindName` matches by substring (case-insensitive):

  | Engine name contains | Sent as |
  |---|---|
  | `XBOX`, `XBL` | `Xbox` |
  | `PSN`, `PLAYSTATION`, `PS4`, `PS5` | `PlayStation` |
  | `STEAM`, `PC`, `WINDOWS` | `Pc` |
  | anything else | `""` |

  Workbench always gives `""`. The field is always in the request, so unknown means an empty
  string, not a missing field.
- **Connect:** `PlayerConnected` with `gameIdentityId` and `platform`, tracked. The reply comes back
  to `OnPlayerConnectedResult`. A duplicate identity on another player id is disconnected first.

The generated status enum comes back `Unknown` for the backend's string values. The handler then
parses the raw status string from the callback body (`ParseStatusString`). The body also carries
`linkPin`, which is never read out or logged.

## Status handling

| Status | Result |
|---|---|
| `active` | Plays. Link screen if `webAccountLinked` is false, otherwise the character screen. |
| `blocked` | BANNED screen. Rejected. |
| `not_whitelisted` | NOT_WHITELISTED screen with the whitelist URL. Rejected. |
| `unlinked` | Automatic retry after 5 s, then 10 s, then 20 s (`m_fUnlinkedRetryDelaySeconds`, `m_iUnlinkedMaxRetries` = 3). The next unlinked answer shows the ERROR screen. |
| anything else | Admitted as `active`. Logged as a WARNING with the raw value. |

A failed connect (error or timeout) rejects with the ERROR screen. During a recheck from the link
screen it goes back to the link screen with an error hint instead.

## Link screen

Shown when the status is `active`, `webAccountLinked` is false, the player has not skipped, and the
reply has a `linkUrl`. With no `linkUrl` it logs a WARNING and goes to the character screen.

- **URL:** never shown on screen. "Copy link" copies it to the clipboard.
- **Show QR-Code / Hide QR-Code:** opens the QR code in an overlay that blurs the rest of the
  screen. It hides again after 60 s (`LINK_REVEAL_MS`), when the screen is hidden, and when a new
  payload arrives.
- **Code:** masked until hovered.
- **Copy:** works while the URL is hidden. The copy handlers read the URL from memory.
- **Continue without linking** (`RpcAsk_RequestSkipLink`): drops the code and URL, sets
  `m_bLinkSkipped` for the session, and goes to CREATING or SELECTING. The link screen is not shown
  again this session.
- **Check again** (`RpcAsk_RequestRecheck`): sends `PlayerConnected` again through SESSION_PENDING.
  Still unlinked shows the link screen again with a "not yet" hint. Linked goes to the character
  screen. The button has a 30 s cooldown (`m_fRecheckCooldownSeconds`, mirrored by the client as
  `RECHECK_COOLDOWN_S`).
- **Leave:** `AckClosed`, then the kick.
- **Idle:** LINKING counts as idle, so it is kicked after `m_fIdleTimeoutSeconds`.

Logs carry the code length only, never the code or the URL.

## Character id

`ELIFE_CharacterIdentity.GetCharacterId(playerId)` returns the character the player picked and the
backend confirmed:

- Server only.
- Only in `SPAWNING` and `IN_GAME`. `LOADING_CHARACTER` is still waiting, so it returns `""`.
  A failed select goes back to `SELECTING`, and the state decides, not the stored id.
- `""` in every other case.

The phone call sites treat `""` as "no character" and do nothing.

## Design rules

**`webAccountLinked`, not `status`, decides whether to show the link.** A player who has never
touched the portal reads `active` and plays normally. That is the normal join-first state, not an
error.

**`linkUrl` is a bearer credential** for that game identity while the PIN lives (30 minutes). Anyone
who gets the URL can attach that identity to their web account. Show it on demand, offer copy, and
don't leave it on screen or in a log.

**`unlinked` does not mean "new player".** Joining creates the account. `unlinked` means the Bridge
could not resolve the identity and deliberately created nothing. Retry; don't treat it as a first
join.

A Game Identity is per game-platform combination. One player on PC, Xbox and PlayStation has three,
so never cache it as "the player". The identity is read at session start, per connect.

## Open questions

**Is a console Game Identity a UUID?** `gameIdentityId` is `format: uuid` across the whole chain,
proven on PC only. The script API types it as a plain string. If a console identity is not a UUID,
the type has to change in all five repos. Join once from a console before building more on this.

**Are the platform enum names right on console?** The mapping above is by substring of the runtime
name, so it is not tied to exact member names. A console join with an unmatched name logs
`platform enum [...] not recognised`. Check that line before trusting `platform`.

**Does the Bridge accept an empty `platform`?** The request always sends the field, and it is `""`
on every Workbench join and every unrecognised platform. Confirm the Bridge accepts that, or stops
sending the field.

## Engine notes

The generated client's conventions (handler signatures, why handlers must be public, callback
lifetime, bare-array responses, the `1`/`0` bool packing) are in
[Backend API client](backend-api.md). Read that before writing the first call; the arity rule in
particular fails at runtime, not compile time.
