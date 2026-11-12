# Join flow

The server decides who a joining player is and whether they can spawn. The client only shows
the screen the server sends and sends back requests. Nothing spawns until the server has
validated and loaded a character. There is no fallback character.

Code paths are relative to `addons/ELifeRPG_Core/`.

Related: [Account linking](account-linking.md) (backend side of linking),
[Backend API client](backend-api.md) (client conventions, read before writing a call),
[Design](superpowers/specs/2026-10-30-join-flow-design.md).

## 1. Overview

```text
join ─► OnPlayerAuditSuccess_S ─► StartSession
 │
 ▼
SESSION_PENDING          CONNECTING screen, PlayerConnected, 15 s timeout
 │
 ├─ unlinked ─────────── wait 5 s, 10 s, 20 s, connect again (loop in SESSION_PENDING)
 │                       4th unlinked answer ─► REJECTED
 ├─ blocked, not_whitelisted, error, timeout ─► REJECTED ─ AckClosed or 120 s ─► kick
 │
 └─ active (unknown status is admitted too)
     │
     ├─ link needed: webAccountLinked false, not skipped, linkUrl set
     │      ▼
     │   LINKING ── Check again (30 s cooldown) ─► SESSION_PENDING (recheck)
     │      ├─ Leave ───────────────────────────► kick
     │      └─ Continue without linking ─────┐
     │                                       │
     └─ no link needed ──────────────────────┤
                                             ▼
                 0 characters ─► CREATING ⇄ SELECTING ◄─ 1+ characters
                 ("+" opens CREATING; Back, with a character, returns to SELECTING)

 CREATING ── create ok ───────────────────────► LOADING_CHARACTER
 SELECTING ── Play ───────────────────────────► LOADING_CHARACTER
 LOADING_CHARACTER ── error or timeout ───────► SELECTING
 LOADING_CHARACTER ── select ok ──────────────► SPAWNING (DoSpawn_S, LIFE)
 SPAWNING ── 3 failed attempts ───────────────► REJECTED
 SPAWNING ── OnPlayerSpawned_S ───────────────► IN_GAME
 IN_GAME ── entity lost ──────────────────────► SPAWNING (respawn after 5 s, nearest respawn point)

 Any state ── disconnect or kick ─────────────► ENDED
```

### Transition rules

`ELIFE_JoinSession.IsTransitionAllowed` is the only list of legal moves. Anything else is
refused and logged as a WARNING.

| From | May go to |
|---|---|
| `NONE` | `SESSION_PENDING` |
| `SESSION_PENDING` | `REJECTED`, `CREATING`, `SELECTING`, `LINKING` |
| `LINKING` | `SESSION_PENDING`, `REJECTED`, `CREATING`, `SELECTING` |
| `CREATING` | `SELECTING`, `LOADING_CHARACTER`, `REJECTED` |
| `SELECTING` | `CREATING`, `LOADING_CHARACTER`, `REJECTED` |
| `LOADING_CHARACTER` | `SPAWNING`, `SELECTING`, `REJECTED` |
| `SPAWNING` | `IN_GAME`, `SPAWNING`, `REJECTED` |
| `IN_GAME` | `SPAWNING` |
| `REJECTED` | `ENDED` only |
| any except `ENDED` | `ENDED` |
| `ENDED` | nothing |

Two more guards:

- Client RPCs go through `AcceptRpc`. The session must exist, be in the right state, and have no
  request in flight. Otherwise the RPC is logged and ignored.
- Backend replies carry a token. A reply whose token is no longer current is dropped (WARNING).

## 2. Joining

### Hook

- `ELIFE_JoinSpawnLogic.OnPlayerAuditSuccess_S` calls `ELIFE_JoinSessionManagerComponent.StartSession(playerId)`.
- `ELIFE_JoinSpawnLogic.OnPlayerDisconnected_S` calls `EndSession(playerId, "disconnect")`, then
  `ELIFE_PlayerIdentityRegistry.Unregister(playerId)`.

### StartSession, in order

1. A session already exists for this player id: it is removed (`restart`). Its identity is still
   disconnected at the backend, with a tracked call.
2. A new session is created. Identity and platform come from the identity registry (`Register`,
   which reads the engine once), and the session keeps them as a snapshot. The session id is
   `playerId-tickMs-random`.
3. State goes to `SESSION_PENDING`.
4. Empty identity: ERROR screen `#ELIFE-Join_ErrorIdentity`. Stop.
5. CONNECTING screen is shown.
6. No backend client yet: wait (below). Otherwise connect.

### Identity

| Build | Identity sent |
|---|---|
| Not Workbench | `SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId)` |
| Workbench (and PeerTool peers) | Low GUID, see below |

Workbench low GUID: `00000000-0000-HHHH-HHHH-PPPPPPPPPPPP`, lowercase. Example:
`00000000-0000-3979-60f2-000000000000`.

- `HHHHHHHH`: eight hex digits of `string.Hash(System.GetMachineName())`. Stable per computer.
  An empty machine name gives `00000000`.
- `PPPPPPPPPPPP`: peer number, decimal, padded to 12 digits. The local Workbench player is `0`.
  Other players use `playerId - 1`. If that is `0`, they use `playerId`. A reconnect gets a new
  player id, so a new peer number.

No real Bohemia identity leaves a Workbench machine.

### Platform

Not Workbench only. `PlayerManager.GetPlatformKind(playerId)`, then `typename.EnumToString` at
runtime, then `MapPlatformKindName` (case-insensitive, substring match, `Xbox` checked first):

| Engine name contains | Sent as |
|---|---|
| `XBOX`, `XBL` | `Xbox` |
| `PSN`, `PLAYSTATION`, `PS4`, `PS5` | `PlayStation` |
| `STEAM`, `PC`, `WINDOWS` | `Pc` |
| anything else, including `NONE` | `""` |

- Workbench always gives `""`.
- An unmatched name logs `platform enum [NAME] not recognised, platform left empty` (NORMAL).
- The value is stored on the session and sent with every `PlayerConnected` of that session.

### Identity registry

`ELIFE_PlayerIdentityRegistry` (`Core/ELIFE_PlayerIdentityRegistry.c`, server only) holds the
playerId mapping. It is filled once per join and read after that:

- `Register(playerId)` reads the identity (`ELIFE_JoinIdentity.Resolve`) and the platform, stores both
  and returns the identity. It is called from `StartSession`. An empty identity stores nothing and
  returns `""`.
- `GetIdentityId`, `GetPlatform`, `Has` and `FindPlayerId` (reverse lookup, `0` when not found) read the
  stored values. Any server feature uses these. They never call the engine.
- `Unregister(playerId)` runs in the disconnect hook, after `EndSession`. It is not called at session
  removal. A restart registers the same player id again, which overwrites the entry.
- The join session keeps its own copy of the identity and platform (`m_sIdentityId`, `m_sPlatform`).
  The final `PlayerDisconnected` and the logs need them after the session is removed.

### Duplicate identity and restart

`ConnectSession` (runs at start, and when a backend wait ends):

1. Another live session has the same identity (another player id). If that player is still
   connected, they are kicked (`duplicate identity`). Their session is removed.
2. If an identity is still counted by the backend (the other session's, or the leftover from a
   restart), a **tracked** `PlayerDisconnected` goes out first. The new `PlayerConnected` waits for
   its reply, or for the 15 s timeout. A failed disconnect still connects (WARNING).
3. Otherwise `PlayerConnected` goes out at once.

### Backend client not ready

The client is created in `ELIFE_GameMode.OnGameStart` (`ELIFE_Api.Initialize()`). That can run
after a player's audit. In that case the session waits in `SESSION_PENDING` with CONNECTING shown.
Each tick checks again. When the client exists, the connect resumes. If 15 s pass, the player gets
ERROR `#ELIFE-Join_ErrorBackend` (reason `no backend`).

### PlayerConnected

- Tracked request with `gameIdentityId` and `platform`. Sets the session in flight with a token
  and a 15 s deadline (`m_fBackendTimeoutSeconds`).
- Reply: `OnPlayerConnectedResult`. Failed or empty reply: `FailConnect` (see section 3).
- No reply in 15 s: `FailConnect("connect timed out")`.

## 3. Connect answer

The reply status is the generated enum `ELIFE_PlayerSessionStatus`:
`Unknown = 0`, `Active = 1`, `Blocked = 2`, `NotWhitelisted = 3`, `Unlinked = 4`.

| Status | Result | Screen |
|---|---|---|
| `active` | Link screen if needed (section 4). Otherwise the character screen. | LINKING, CREATING or SELECTING |
| `blocked` | Rejected. The reply has no ban fields yet, so reason, until and appeal are empty (TODO). | BANNED |
| `not_whitelisted` | Rejected. Shows `m_sWhitelistUrl`. An empty URL leaves the field out. | NOT_WHITELISTED |
| `unlinked` | Not a new player. The Bridge created nothing. Retry with no screen change: 5 s, then 10 s, then 20 s. The 4th unlinked answer rejects. | ERROR `#ELIFE-Join_ErrorBackend` after the 4th |
| anything else | Admitted as `active`. WARNING with the raw value. | as `active` |
| no reply, error, timeout | First connect: rejected. Recheck: back to LINKING with an error hint. | ERROR `#ELIFE-Join_ErrorBackend` |

Retry count and delay:

- `m_fUnlinkedRetryDelaySeconds` (5) is the first delay. It doubles each time.
- `m_iUnlinkedMaxRetries` (3) is the number of automatic retries. The counter is per session. Check
  again does not reset it.

### Raw status fallback

The generated enum comes back `Unknown` for the backend's string values. So:

- The handler reads `status` from the raw callback body (`ELIFE_JoinStatusRaw`).
- It maps `active`, `unlinked`, `blocked` and `not_whitelisted` by string.
- It logs `connect reply raw status=…` on every reply.
- The body also carries `linkPin`. The raw read never returns or logs it.

## 4. Account linking

Backend rules are in [Account linking](account-linking.md). This section is the mod side.

- **`webAccountLinked` decides, not `status`.** A player who never used the portal reads `active`.
  That is normal.
- The link screen shows only when all of these hold: status `active`, `webAccountLinked` false,
  not skipped in this session, and `linkUrl` non-empty. With no `linkUrl`: WARNING, character screen.
- When the answer is to a recheck, the hint is `#ELIFE-Join_LinkNotYet`.

### Link screen as it is now

`ELIFE_JoinLinkScreen` (`UI/layouts/Menus/Join/JoinLink.layout`), centred:

- **Code:** one cell per character, showing `*`. Hovering the code shows it. The copy icon copies the
  raw code. With no code, a "missing" text is shown.
- **Copy link:** copies the URL. The URL is never shown on screen. A "Copied" label shows for 2 s.
- **Show QR-Code:** opens an overlay (`QrOverlay`) with a blur behind it (`QrBlur`). It is disabled
  when there is no URL.
- **Hide QR-Code:** closes the overlay.
- The overlay also closes after 60 s, when the screen is hidden, and when a new payload arrives.
- **Check again:** 30 s cooldown. The label counts down, e.g. `Check again (N)`. The server enforces
  the same cooldown from its last accepted press.
- **Continue without linking:** drops the code and URL, sets `m_bLinkSkipped` for the session, and
  goes to the character screen. No cooldown.
- **Leave:** `AckClosed`, then kick.
- **Idle:** LINKING counts as idle. Kicked after 300 s since the last accepted RPC.

### Recheck (Check again)

`RpcAsk_RequestRecheck` is accepted only in LINKING, with no request in flight and outside the
cooldown. The session goes to `SESSION_PENDING` and sends `PlayerConnected`. The answer decides:

| Answer | Result |
|---|---|
| `active`, linked | Character screen. |
| `active`, still not linked | Link screen again, with the "not yet" hint. |
| `unlinked` | Retry as usual. No new screen is pushed until the next answer. |
| error or timeout | Back to LINKING with `#ELIFE-Join_ErrorBackend`. The last code and URL are shown again. |

### Link data

- `linkUrl` is a bearer credential for that identity. Anyone with it can attach the identity to a web
  account.
- It is kept only in the session (`m_sLinkUrl`, `m_sLinkPin`). It is cleared on skip, on reject, and
  when the session ends.
- Logs carry the code length only, never the code or the URL.
- The QR code is drawn only inside the overlay.

### QR code

`ELIFE_QrCode` is pure script: ISO/IEC 18004, byte mode, ECC level M, versions 1 to 20. The longest
URL that fits is 666 bytes (version 20). A longer URL draws no QR code and logs a WARNING.

## 5. Character create and select

### Screens

- **SELECTING:** one card per character, 3 per row. A `+` card opens create. One click highlights a
  card. A second click within 400 ms, or Play, sends the request. Previews always show appearance 0.
- **CREATING:** first name, last name, appearance spinner with a live preview, and Create.
  Back is shown only when the player has a character.

### Create (`RpcAsk_RequestCreate`)

In this order:

1. Name check (`ELIFE_CharacterNameValidator.Validate`). Error: stays CREATING with the key.
2. Appearance index in range. Otherwise `#ELIFE-Join_AppearanceInvalid`.
3. Platform profanity filter on the full name (`ELIFE_ProfanityFilter`, asynchronous). A change
   means `#ELIFE-Join_NameNotAllowed`. If the platform cannot filter, the name passes, as in vanilla chat.
4. `CreateCharacter(gameIdentityId, name)`. Error or timeout: `#ELIFE-Join_CreateFailed`, with the
   typed names echoed back.
5. Success: the character is added, its appearance is remembered, and the character is selected
   (see below).

Name rules:

| Rule | Value |
|---|---|
| Cleaning | Removes `<` and `>`, then trims. |
| Length of each part | 2 to 20 characters. |
| Characters | ASCII letters, `-` and `'`. |
| First character | A letter. |
| Not allowed | `--` and `''` anywhere. |
| Stored form | First letter upper-cased, rest kept (`mcCoy` becomes `McCoy`). Full name `First Last`. |

Errors: `#ELIFE-Join_NameTooShort`, `#ELIFE-Join_NameTooLong`, `#ELIFE-Join_NameInvalid`. The client
caps typing at 20 characters and refuses a longer name before sending. The server validates again.

### Select (`RpcAsk_RequestSelect`)

1. The id must be in this session's list (from the connect reply, or created this session). Otherwise
   `#ELIFE-Join_SelectFailed`, and the player stays in SELECTING.
2. LOADING_CHARACTER, LOADING screen, `CharacterSelected(gameIdentityId, characterId)`.
3. Success: SPAWNING. Error or 15 s timeout: SELECTING with `#ELIFE-Join_SelectFailed`.

Appearance is remembered only for characters created in this session. After a reconnect, every
character uses appearance 0.

## 6. Spawning

### First spawn

After a successful select or create, `ELIFE_JoinSpawnLogic.DoSpawn_S` runs:

1. No session: dropped (WARNING).
2. Faction and loadout: `RequestFaction` for `LIFE`, then `RequestLoadout` for `"LIFE Player"` on the
   player's components. A refusal or a missing piece counts as a spawn failure.
3. Prefab: the appearance prefab for the chosen index. Index 0 is the default. An empty list falls
   back to `Character_LIFE.et`.
4. Position (`ELIFE_SpawnLocator.FindJoinPosition`):
   - One random LIFE `SCR_SpawnPoint` (`GetRandomSpawnPointForFaction("LIFE")`).
   - A clear spot within 5 m of it is used. If there is none, the point's origin is used (WARNING).
   - Yaw is the point's yaw.
   - No LIFE point: `FindRandomPosition` (WARNING).
5. `SCR_FreeSpawnData(prefab, pos, angles)` goes to `SCR_RespawnComponent.RequestSpawn`. A refusal is
   a spawn failure.

`FindRandomPosition` tries up to 30 random points in the world bounds. A point must be above the
ocean level plus 1 m and have a clear spot within 5 m. The yaw is random. If all tries fail, it uses
the origin of a LIFE point. If there is none, the spawn fails.

### Outcome

- `OnPlayerSpawned_S` goes to `OnSpawned`: SPAWNING to IN_GAME, death spot cleared, IN_GAME screen
  (the menu closes).
- `OnPlayerSpawnFailed_S` goes to `OnSpawnFailed`: attempts go up by one. At
  `m_iMaxSpawnAttempts` (3), REJECTED with ERROR `#ELIFE-Join_SpawnFailed`. Otherwise the same
  session retries in 1 s.

### Death and respawn

- `OnPlayerEntityLost_S` goes to `OnEntityLost`. Only from IN_GAME. The state goes to SPAWNING.
- The death position is the live entity origin. If there is no live entity, it is the last position
  the tick cached while IN_GAME.
- The respawn is due after `m_fRespawnDelaySeconds` (5). The 1 s tick triggers it, so it can be a
  little late. No menu is shown; the same character and appearance are used.
- Position (`FindRespawnPosition`): nearest `ELIFE_RespawnPoint` to the death position, then a clear
  spot within 5 m of it. No registered point, or no clear spot: WARNING, then random.

### Respawn points

- Prefab `Prefabs/Spawning/ELIFE_RespawnPoint.et`, property `m_sName` (for example `Hospital`).
- It registers on the server in play mode only. It is removed when deleted.
- The DevWorld has one, `Hospital`, in `SpawnPoints.layer`.

### Gear and state

The character spawns with the prefab's default loadout. Nothing is loaded from or saved to the
backend yet. TODOs are listed in section 12.

## 7. Character identity

`ELIFE_CharacterIdentity.GetCharacterId(playerId)` returns the character id the backend confirmed.

- Server only. A client gets `""`.
- Only in SPAWNING and IN_GAME. In LOADING_CHARACTER it is `""`, and after a failed select (back in
  SELECTING) it is `""`, even though the id is still stored.
- Any other state, or no session: `""`. Callers must treat `""` as "no character".

`GetCharacterName(playerId)` returns the player's profile name, not a character name. The code says
this stands in until characters are used elsewhere.

## 8. Disconnect, leave and kicks

| Event | Session result | Backend call |
|---|---|---|
| Player disconnects, any state | `EndSession("disconnect")`, ENDED | Untracked `PlayerDisconnected`. Reply dropped. Context held 30 s. |
| Rejected screen closed (ERROR, BANNED, NOT_WHITELISTED) | `AckClosed` in REJECTED, then kick | Untracked, from `EndSession` |
| Leave on link screen | `AckClosed` in LINKING, then kick | Untracked |
| Leave during a recheck | Kick at once | Untracked |
| 120 s in REJECTED with no `AckClosed` | Kick | Untracked |
| Idle 300 s in CREATING, SELECTING or LINKING | Kick (`idle`) | Untracked |
| Duplicate identity | Other session kicked and removed. This session waits for the tracked disconnect. | Tracked |
| Restart with same player id | Old session removed | Tracked |

- A kicked player's own disconnect finds no session and does nothing.
- An untracked disconnect is sent only when the backend still counts the identity. An empty
  identity sends nothing.

### Client side

- Closing a rejected screen or pressing Leave sends `AckClosed`. The client then leaves gameplay
  after 0.5 s.
- After a rejected screen, the client leaves on its own after 125 s if the server has not kicked.
  This fallback is armed only for ERROR, BANNED and NOT_WHITELISTED.
- IN_GAME closes the menu. Escape and the pause menu are swallowed while the menu is open.
- If no screen arrives, `ELIFE_PlayerController` shows CONNECTING once, locally.

## 9. Reference

### States (`ELIFE_EJoinState`, server)

| State | Meaning |
|---|---|
| `NONE` | Not yet started. |
| `SESSION_PENDING` | Connecting. Also waits for a retry or the backend client. |
| `LINKING` | Link screen up. Optional. |
| `REJECTED` | Rejection screen up. Waits for `AckClosed` or 120 s. |
| `CREATING` | Create screen up. |
| `SELECTING` | Select screen up. |
| `LOADING_CHARACTER` | Waiting for `CharacterSelected`. |
| `SPAWNING` | Spawn requested. Also the death-to-respawn wait. |
| `IN_GAME` | Character in the world. |
| `ENDED` | Session gone. Nothing is sent to the client. |

### Screens (`ELIFE_EJoinScreen`, client)

| Screen | Sent when | Payload (JSON) |
|---|---|---|
| `CONNECTING` | Start of connect, and while it waits | `{}` |
| `LINKING` | Link screen needed | `{url, code, hint}` (`ELIFE_JoinLinkPayload`) |
| `SELECTING` | Character screen with characters. Also after Back from create, or a failed select | `{characters: [{id, name}], error}` |
| `CREATING` | Character screen without characters, `+` card, create or timeout errors | `{canGoBack, error, firstName, lastName, appearanceCount}` |
| `LOADING` | Character chosen | `{}` |
| `ERROR` | Connect failed, spawn failed, backend problem | `{messageKey}` |
| `BANNED` | `blocked` | `{reason, until, appealUrl}` (all empty today) |
| `NOT_WHITELISTED` | `not_whitelisted` | `{url}` |
| `IN_GAME` | Spawned | `{}` |

The client shows ERROR, BANNED and NOT_WHITELISTED as a dialog. On the ERROR screen, a key without
the `ELIFE-Join_` prefix shows the generic backend error. On the create and select screens, such a
key shows nothing.

### Client to server RPCs (`RpcAsk_*`)

All are reliable, server-side, in `Spawning/ELIFE_SpawningPlayerController.c`.

| RPC | Accepted in | Notes |
|---|---|---|
| `RpcAsk_RequestCreate(firstName, lastName, appearanceIndex)` | CREATING | Strings over 64 chars are dropped (WARNING). |
| `RpcAsk_RequestSelect(characterId)` | SELECTING | Id must be in the session's list. |
| `RpcAsk_RequestOpenCreate()` | SELECTING | `+` card. |
| `RpcAsk_RequestBackToSelect()` | CREATING | Needs at least one character. |
| `RpcAsk_RequestRecheck()` | LINKING | Not in flight. 30 s cooldown. |
| `RpcAsk_RequestSkipLink()` | LINKING | Continue without linking. |
| `RpcAsk_AckClosed()` | REJECTED, LINKING, or SESSION_PENDING during a recheck | Kicks. |

A refused RPC is logged (WARNING) and gets no reply.

### Server to client

`RpcDo_ShowSession(screen, payloadJson)`: reliable, owner only. A listen-server host applies it
directly. An identical repeat within 250 ms is dropped.

### Backend calls

| Call | Body | Reply (used fields) |
|---|---|---|
| `PlayerConnected` | `gameIdentityId`, `platform` | `accountId`, `status`, `webAccountLinked`, `linkPin`, `linkUrl`, `characters[]` |
| `PlayerDisconnected` | `gameIdentityId` | status only |
| `CharacterSelected` | `gameIdentityId`, `characterId` | status only |
| `CreateCharacter` | `gameIdentityId`, `name` | `characterId`, `name` |

Methods are in `Core/Api/ELIFE_Api_Session.c` and `Core/Api/ELIFE_Api_Character.c`. Routes and
conventions are in [Backend API client](backend-api.md).

### Component attributes

On `ELIFE_JoinSessionManagerComponent`. Defaults are from the `[Attribute]` declarations.
`ELIFE_GameMode.et` overrides the last three rows.

| Attribute | Code default | Set in `ELIFE_GameMode.et` | Meaning |
|---|---|---|---|
| `m_fBackendTimeoutSeconds` | 15 | no | Each backend request, and the wait for the backend client. |
| `m_fIdleTimeoutSeconds` | 300 | no | Idle kick in CREATING, SELECTING, LINKING. |
| `m_fRejectedKickSeconds` | 120 | no | REJECTED before kick. |
| `m_fRespawnDelaySeconds` | 5 | no | Death to respawn. |
| `m_fRecheckCooldownSeconds` | 30 | no | Cooldown for Check again. The client mirrors it. |
| `m_iMaxSpawnAttempts` | 3 | no | Spawn attempts before REJECTED. |
| `m_iUnlinkedMaxRetries` | 3 | no | Automatic retries for `unlinked`. |
| `m_fUnlinkedRetryDelaySeconds` | 5 | no | First retry delay. Doubles. |
| `m_sWhitelistUrl` | `""` | no | Link on the NOT_WHITELISTED screen. |
| `m_aAppearancePrefabs` | empty; `Character_LIFE.et` is used when empty | 11 prefabs: `Character_LIFE.et` and `Character_LIFE_Face_01.et` to `Character_LIFE_Face_10.et` | Create-screen appearances. Index 0 is the default. |
| `m_vMenuCameraPosition` | `0 0 0` | `8 25 5` | Declared and set, but no code reads it yet (TODO, `JoinMenu.c:235`). |
| `m_vMenuCameraAngles` | `0 0 0` | `0 -30 0` | Same as above. |

Other settings in `ELIFE_GameMode.et`:

- Game mode component `ELIFE_JoinSessionManagerComponent`.
- `m_SpawnLogic ELIFE_JoinSpawnLogic`, with `m_eDisconnectPlayerControllerBehaviour DELETE` and
  `m_eDisconnectCharacterBehaviour DELETE`.
- `PlayerControllerPrefab` `Prefabs/Characters/Core/ELIFE_PlayerController.et`.
- `m_bUseSpawnPreload 1`.
- No own `SCR_FreeSpawnHandlerComponent`: the one inherited from `GameMode_Base.et` is used.

### Fixed values

| Name | Value | Where |
|---|---|---|
| Tick | 1 s | `Tick`, server |
| Untracked disconnect hold | 30 s | `FIRE_AND_FORGET_MS` |
| Spawn retry delay | 1 s | `OnSpawnFailed` |
| Spawn clear-spot radius | 5 m | `ELIFE_SpawnLocator.CLEAR_RADIUS` |
| Random position tries | 30 | `ELIFE_SpawnLocator.RANDOM_TRIES` |
| Name length | 2 to 20 per part | `ELIFE_CharacterNameValidator` |
| Create name cap (client) | 20 | `ELIFE_JoinCreateScreen.NAME_MAX` |
| RPC string cap | 64 | `JOIN_STRING_MAX` |
| Double-click window | 400 ms | `ELIFE_JoinSelectScreen` |
| Link overlay auto-hide | 60 s | `LINK_REVEAL_MS` |
| "Copied" label | 2 s | `COPIED_SHOW_MS` |
| Client leave after reject | 125 s, leave delay 0.5 s | `ELIFE_JoinMenu` |

### Log line format

```text
ELIFE_Join | session=<id> identity=<gameIdentityId> player=<playerId> <message>
```

- State change: `… FROM -> TO (reason)`. Refused: `… FROM -> TO refused (reason)`, WARNING.
- Before a session exists: `ELIFE_Join | session= identity= player=<playerId> …`.
- `identity=` is in every line. Outside Workbench it is the real identity.
- The `DEBUG` request lines print the full `gameIdentityId`. They are slated for removal.
- Link lines print the code length only.
- The spawn `DIAG` lines are temporary (section 12).

## 10. Setup checklist for a world

- [ ] Game mode is `Prefabs/MP/Modes/ELIFE_GameMode.et`. It has the session manager, `ELIFE_JoinSpawnLogic`, the
      appearance list, and `PlayerControllerPrefab` set to `ELIFE_PlayerController.et`.
- [ ] Backend: `ELifeRPG.json` with `serverUrl` for the Bridge. See [Backend API client](backend-api.md).
- [ ] At least one `SCR_SpawnPoint` for faction `LIFE` in the world. `Prefabs/Spawning/SpawnPoint_LIFE.et`
      is the prefab. Without one, the first spawn uses a random position and logs a WARNING.
- [ ] One `ELIFE_RespawnPoint` (`Prefabs/Spawning/ELIFE_RespawnPoint.et`) with `m_sName` set. Without one,
      respawn uses a random position and logs a WARNING.
- [ ] Loadout manager `Prefabs/MP/Managers/Loadouts/ELIFE_LoadoutManager.et` in the world. Its loadout is
      `LIFE Player`. The DevWorld places it in `GameMode.layer`.
- [ ] Faction `LIFE` exists. The repo has `Configs/Factions/LIFE.conf`.
- [ ] Only one `SCR_FreeSpawnHandlerComponent` in the game mode. `GameMode_Base.et` already has one, so
      `ELIFE_GameMode.et` must not add another. Workbench may add it back when it saves the prefab.
- [ ] AIWorld is optional. Without one the engine logs `No AIWorld is present in the map` when the
      player's group is created. Spawning still works.

## 11. Testing

Run in Workbench: **Plugins → ELIFE → Run Join Self-Tests**.

- Plugin: `addons/ELifeRPG_Core/Scripts/WorkbenchGame/ELIFE/ELIFE_JoinSelfTestPlugin.c`. Attribute
  `name: "Run Join Self-Tests"`, `category: "ELIFE"`, module `WorldEditor`.
- Tests: `addons/ELifeRPG_Core/Scripts/Game/Features/Spawning/Tests/ELIFE_JoinSelfTests.c`, all inside
  `#ifdef WORKBENCH`.

Output, one line per check, then a total:

```text
ELIFE_SelfTest | PASS <name>
ELIFE_SelfTest | FAIL <name>: <why>
ELIFE_SelfTest | DONE pass=N fail=M
```

- Pure tests always run: name rules, transition table, select payload round trip, Workbench
  identity format, platform mapping, identity registry (negative test ids only), and QR math (Reed-Solomon, format and version bits, finder and
  timing, version choice and limit, codeword counts).
- Manager tests run only in play mode. Outside it, the log says
  `ELIFE_SelfTest | SKIP manager tests: no game mode (run in play mode)`. They cover: connect answers
  (blocked, not_whitelisted, unlinked retries and exhaustion, unknown status, null data, stale token),
  the backend-client wait and its deadline, recheck, skip, Leave and the link-screen states, create,
  select, the name filter, spawn failures, entity lost, confirmed character, request timeouts,
  idle kick, rejected kick, and clock wrap.
- Test hooks: `SetClockOverrideForTests`, `SetNoSendForTests` and `SetApiMissingForTests`, so the
  tests run without a real backend call.
- Pass means `fail=0`.
- `Test_Qr_OwnerUrlMatrix` checks the QR port against the reference encoder. The header of
  `ELIFE_QrCode.c` says it is verified only by that test.

## 12. Open questions and known gaps

### Open questions

- **Console identity is a UUID?** Unverified. `gameIdentityId` is sent as-is, and the script type is a
  plain string. See [Account linking](account-linking.md#open-questions).
- **Console platform names?** The mapping matches substrings. An unmatched name logs
  `platform enum [...] not recognised`. Check that line on a console join.
- **Does the Bridge accept an empty `platform`?** It is sent on every Workbench join and on any
  unrecognised platform.
- **No timeout in SPAWNING.** If `RequestSpawn` is accepted but neither `OnPlayerSpawned_S` nor
  `OnPlayerSpawnFailed_S` fires, the session waits there until disconnect. The TODO says the
  first-spawn failure is still open.
- **Busy buttons on the link screen.** Check again and Continue without linking set busy. Only the next
  screen clears it. If the server refuses the RPC (wrong state or in flight), no screen comes, and the
  buttons stay disabled. Leave still works. Found by reading the code, not reproduced.
- **Leave during an unlinked retry after a recheck.** The server refuses the `AckClosed` (WARNING). The
  client leaves anyway, and the disconnect ends the session. It works, but logs a warning.

### TODO list

Every `TODO` under `Features/Spawning` and `UI/Menu/Join`. Both folders are under
`addons/ELifeRPG_Core/Scripts/Game/`. Line numbers are as of the working tree.

**Features/Spawning**

| File:line | TODO |
|---|---|
| `ELIFE_JoinSpawnLogic.c:53`, `:66`, `:131`, `:138`, `:192` | **DIAG spawn logs**, temporary. Remove once the first-spawn failure is fixed. The DIAG prints are at `:54`, `:67`, `:136`, `:142`, `:193`. |
| `ELIFE_JoinRequestContext.c:89` | Generated `ELIFE_PlayerConnectedResponse.status` is an int enum. The status is parsed from the string body instead. Fix the generated type. |
| `ELIFE_JoinSessionManagerComponent.c:129` | Save position, gear and state before a restart releases an in-game character. |
| `ELIFE_JoinSessionManagerComponent.c:209` | Save position, gear and state on disconnect. |
| `ELIFE_JoinSessionManagerComponent.c:451` | Use the saved backend position. |
| `ELIFE_JoinSessionManagerComponent.c:462` | Apply gear and state from the backend. The prefab default loadout is used today. |
| `ELIFE_JoinSessionManagerComponent.c:482` | Periodic save of position, gear and state. |
| `ELIFE_JoinSessionManagerComponent.c:563` | Gear on death: lose items or use a default loadout, and save it. |
| `ELIFE_JoinSessionManagerComponent.c:715` | Ban reason, time left and appeal URL. The connect reply has no ban fields. |
| `ELIFE_JoinSessionManagerComponent.c:838` | Map backend validation errors (ProblemDetails) once the callback exposes them. |
| `ELIFE_JoinSessionManagerComponent.c:862` | A distinct "in use" message once the backend reports it. |
| `ELIFE_JoinSessionManagerComponent.c:887` | Load and lock the character (position, gear, state). Today `CharacterSelected` only starts the Core session, which replaces any other. |
| `ELIFE_JoinSessionManagerComponent.c:921`, `:955`, `:1223`, `:1243`, `:1272` | Remove the `DEBUG` request logging once the create-character GUID bug is fixed. |
| `ELIFE_JoinSessionManagerComponent.c:1351` | Remove the link code debug log. |

**UI/Menu/Join**

| File:line | TODO |
|---|---|
| `ELIFE_JoinMenu.c:29`, `:31`, `:259`, `:267`, `:408`, `:426`, `:458`, `:497`, `:520`, `:531` | Remove the widget debug dumps once the join UI visibility bug is fixed. |
| `ELIFE_JoinMenu.c:235` | Local menu camera needs Workbench verification. The attributes exist but nothing reads them. |
| `ELIFE_JoinSelectScreen.c:167` | Per-character appearance or portrait from the backend. Previews show appearance 0 today. |
| `ELIFE_JoinLinkScreen.c:232` | Remove the link code debug log (also at `:237`). |

### Other known gaps

- Appearance is kept only for characters created in this session (section 5).
- Ban screen has no reason, end time or appeal link (TODO, `:715`).
- Gear and position are not loaded or saved (TODOs above).
