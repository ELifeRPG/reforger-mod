# NPC dialogue

Conversations with NPCs use one shared menu, `ELIFE_DialogueMenu`. A panel along the bottom of the screen shows
the NPC's name, their spoken line as a subtitle, the details under it (names, figures), and the player's numbered
replies. The world stays visible, so the player sees the NPC talking.

The bank clerk is the first NPC that uses it. Menu, tree model and controller base live in `ELifeRPG_Core`; each NPC's
subclasses live in its package.

## Parts

| Part | Where | Generic or per NPC |
|---|---|---|
| Menu, input, subtitles | `Scripts/Game/UI/Menu/Dialogue/ELIFE_DialogueMenu.c` | Generic |
| Tree model | `Scripts/Game/UI/Menu/Dialogue/ELIFE_DialogueConfig.c` | Generic |
| Controller base | `Scripts/Game/UI/Menu/Dialogue/ELIFE_DialogueController.c` | Subclass per NPC |
| Layouts | `UI/layouts/Menus/Dialogue/` | Generic |
| Tree | an `ELIFE_DialogueConfig` `.conf` | One per NPC kind |

A tree is a list of nodes. Each node has:

- `m_Line`: what the NPC says on arrival (an `ELIFE_DialogueLine` subclass). Leave it empty where the NPC
  already speaks, for example the greeting or the result of an action.
- `m_Details`: the specifics under the line (an `ELIFE_DialogueDetails` subclass).
- `m_aReplies`: the player's replies. Each has a text and an `m_Action` (an `ELIFE_DialogueAction` subclass).
  `m_sRepliesOf` borrows another node's replies instead, for example after answering a question.

Generic actions: `ELIFE_DialogueAction_GoTo` (move to a node) and `ELIFE_DialogueAction_End`.

A reply can have `m_aConditions` (`ELIFE_DialogueCondition` subclasses). It is offered only when all of them
hold, and `m_bNegate` flips one. To branch on context, give two replies the same text and opposite conditions.
When what a condition reads changes (for example a server answer arrives), the controller calls
`menu.UpdateReplies()`.

## Generic NPC core

Service NPCs share one core in `ELifeRPG_Core` (`Scripts/Game/Features/NPC`). A concrete NPC lives in an optional
package (the bank clerk is in `ELifeRPG_NPC`). Core never references a package, so removing the package removes
the NPC and nothing else.

| Part | What it does |
|---|---|
| `ELIFE_NpcComponent` | Serves one customer at a time: grant or refuse, visits, AFK and range release, idle, facing the customer, gestures, voice and line SFX. Subclass per NPC kind. |
| `ELIFE_NpcCharacterComponent` | On the player character (`Character_LIFE.et`). Carries open, close, say and generic requests (`kind, a, b, text`) to the server, and grants and messages back. |
| `ELIFE_NpcVoiceTable` | A `.conf` per NPC kind: optional voices, lines with variant count, askable flag, gesture, SFX and repeat cooldown, plus the greeting, goodbye, busy and unavailable lines. |
| `ELIFE_NpcSpot` | Spawns the NPC (the world doesn't replicate placed characters) with its prefab and one voice. Optional `m_Variation` (`ELIFE_NpcSpotVariation`) picks look and voice from the position, for an NPC kind placed in many locations. |
| `ELIFE_NpcLookList`, `Configs/NPC/ELIFE_NpcLooks_Civilian.conf` | Every vanilla civilian head with its matching body (11 looks, by name). A variation swaps head and body at spawn and keeps the prefab's clothing; `m_aLookNames` limits it to a subset, so two NPC kinds can use separate sets and never look alike. |
| `ELIFE_NpcTalkAction` | The "talk" action on the NPC prefab. |
| `ELIFE_NpcDialogueController`, `ELIFE_NpcDialogueLine` | A conversation that only talks: no script needed for a plain NPC. |
| `ELIFE_FormMenu`, `ELIFE_FormContent` | A full-screen form opened on top of a conversation, with the package's own layout, so packages need no entry in `chimeraMenus.conf`. |

Sound event: `<prefix><VOICE>_<LINE>[_<variant>]`. Subtitle id: the same with the subtitle prefix. Lines travel
as their index in the voice table, so the server and the clients load the same file.

## Adding an NPC

1. Put everything for it in its package: prefab (inheriting a character, with your `ELIFE_NpcComponent`
   subclass and `ELIFE_NpcTalkAction`), spot prefab, voice table, dialogue tree, string table (registered in the
   package's `.gproj`), sounds and layouts. Restart Workbench after adding or renaming resources: until then
   prefabs and layouts can't resolve the new GUIDs, even though scripts still compile.
2. Set on the component: `m_sVoiceTable`, `m_sDialogueConfig`, `m_sDisplayName`.
   Sounds: put the voice `.acp` files on the prefab's `SCR_CommunicationSoundComponent`, next to the vanilla
   RadioProtocol ones (the lips only move for sounds played there). Put the SFX `.acp` files on its
   `CharacterSoundComponent`.
3. In the world, place the spot prefab, never the character. The world doesn't replicate placed characters, so
   clients wouldn't see a placed one.
4. For an NPC that only talks, that is all: the default controller opens the tree, and `ELIFE_NpcDialogueLine`
   asks for askable lines by name.
5. For services, subclass `ELIFE_NpcComponent` and override what you need:
    - Server: `IsReady`/`Prepare` (load something before granting, then `AnswerWaiting`), `CheckCustomer`,
      `OnVisitStart`/`OnVisitEnd`, `OnRequest` (check with `CheckRequest`), `GetReasonLineName`. Answer with `Send`.
    - Client: `OnGranted` (open your own controller), `OnMessage`, `GetRefusalText`.
6. Subclass `ELIFE_NpcDialogueController` for your actions, details and conditions. They reach it through
   `YourController.Cast(menu.GetController())` and send requests with `Request(kind, a, b, text)`.
7. A form on top of the conversation: subclass `ELIFE_FormContent`, open it with `ELIFE_FormMenu.Open`, call
   `menu.Suspend()` before and `menu.Resume()` plus `menu.ShowNode(...)` when it closes. While waiting for a server
   answer, use `menu.SetWaiting(true, notice)`, which greys out the replies.

## Opening hours

- Set `m_Schedule` (`ELIFE_NpcSchedule`) on the NPC prefab's `ELIFE_NpcComponent`. It takes an opening and a
  closing time in in-game time. A closing time before the opening time means open over midnight, and the same time
  for both means always open. Without a schedule the NPC never closes.
- To give one location different hours, set `m_ScheduleOverride` on its `ELIFE_NpcSpot`.
- The server checks the schedule every 5 s. Once closed, the talk action reads "closed · opens 08:00" and
  conversations are refused with the `CLOSED` reason. Put that line in the voice table as `m_sClosedLine`.
  Closing time is the last moment to start a conversation. One that is already running carries on, and he goes
  off duty after it ends.

## Off-duty routine

While closed, `ELIFE_NpcRoutine` (server) puts the NPC in his own AI group. He walks to a free seat within 20 m
of his spot, picking one at random with nearer seats more likely. If no seat is free, he wanders within 20 m and
keeps checking. He never despawns. At opening time he gets up, walks back, snaps onto his spot and serves again.

The routine needs vanilla AI: an `SCR_AIWorld` with a Soldiers navmesh in the world. Without one he stays at his
spot, closed. On small worlds, set `UseNavmeshStreaming 0` on the navmesh, or AI never walks. After placing new
seats or obstacles, re-bake the navmesh so he can reach them.

## Opening-hours sign

`Prefabs/Props/Signs/SignWall_01_65x45_Hours.et` (`ELifeRPG_Props`). Place it within 25 m of an NPC spot that
has opening hours. It needs no linking: `ELIFE_NpcHoursSignComponent` reads the hours of the nearest such spot
(override first, else the NPC prefab's schedule) and renders them on the plate within 20 m. With no spot in range
the plate stays blank. Change the heading or range on the component.

## Seats

Players get a "Sit" action on each free seat, shown only from within 1 m in front of it. A taken seat shows it
greyed out. Off-duty NPCs use the same seats (see above). Logic is in `ELifeRPG_Core`
(`Scripts/Game/Features/Seats`), seating props in `ELifeRPG_Props` (`BenchStreet_01_Seats.et` is the example).

To make a new seating prop:

1. Inherit the vanilla prop and add `ELIFE_SeatPropComponent` (tracks who sits where and replicates it).
2. Add one child per seat from `Prefabs/Props/Seating/ELIFE_SeatPoint.et` (Core). Place it on the floor in front of the
   seat with its -Z axis towards the prop, like the bench's (0.65 m out). On its `SCR_AISmartActionSentinelComponent`,
   keep the `ELIFE_Seat` tag and add `ELIFE_Seat_<n>` (0, 1, …). The number is needed because runtime child order
   doesn't follow the prefab. The seat point carries a `Hierarchy` component, so it moves with the prop.
   Keep seats at least 0.75 m apart (the character's collision capsule), or sitters push each other off them.
   The sitting height comes from the animation, not the seat point. To match a seat to the pose, scale the prop
   (`scale` on the prefab root); the seat points scale with it.
3. On the prop's `ActionsManagerComponent`, add one `UserActionContext` per seat, at the seat, and one
   `ELIFE_SitUserAction` per seat with that context and `m_iSmartActionId` = the seat's `<n>`.
4. Restart Workbench if the prefab is new, then re-bake the navmesh where you place it.

## Bank clerk (`ELifeRPG_NPC`)

- Component: `ELIFE_BankClerkComponent` (bank, customer lookup, greeting by context, opening accounts, papers)
  with `ELIFE_BankClerkTransfers`. Requests and messages are `ELIFE_EClerkRequest` and `ELIFE_EClerkMessage`.
- Looks: one prefab, `Character_BankClerk.et` (suit). His spot's variation uses all civilian looks and varies his voice.
- Voice table: `Configs/Banking/ELIFE_ClerkVoices.conf`. Its line names match `ELIFE_EClerkLine` and the voice
  tooling's `clerk_lines.py` (kept outside the repo): new lines need all three.
- Controller: `ELIFE_BankClerkDialogueController` in `Scripts/Game/UI/Menu/Banking/ELIFE_BankClerkDialogue.c`,
  with `ELIFE_ClerkDialogueLine`, the transfer slip (`ELIFE_TransferSlipForm`) and open-account actions, and the
  fee terms, new account and receipt details.
- Tree: `Configs/Banking/ELIFE_ClerkDialogue.conf`, set on the clerk prefab.
- Hours: 08:00–20:00 on the prefab. Closed, he says `CLOSED` and points to the sign.
- Context: when a visit starts, the server looks up the customer's accounts (`ELIFE_EClerkAccountStatus`). He
  greets customers of his bank with "welcome back", and anyone else with the in-game time of day or a plain
  hello. The result also reaches the client for `ELIFE_ClerkDialogueCondition_HasAccount` and
  `ELIFE_ClerkDialogueCondition_OpenedThisVisit`.
- Texts: `Language/ELIFE_NPC_Localization.st` in the package. Subtitles are the exact wording he speaks:
  `ELIFE-ClerkVO_<VOICE>_<LINE>[_<variant>]`, generated with the voice tooling.
