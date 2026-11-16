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
  If the NPC greets (`ELIFE_NpcComponent.GreetsOnVisit`), the conversation stays hidden until the greeting is
  heard, at most 2 s, so the line doesn't appear after the rest. Only lines heard since the player pressed talk count.
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
- For a break, tick `m_bSecondRange` and set a second opening and closing time (e.g. 09:00–15:00 and
  16:00–02:00). During the break he goes off duty like after closing, and "opens" shows the next range's start.
- To give one location different hours, set `m_ScheduleOverride` on its `ELIFE_NpcSpot`.
- The server checks the schedule every 5 s. Once closed, the talk action reads "closed · opens 09:00" and
  conversations are refused with the `CLOSED` reason. Put that line in the voice table as `m_sClosedLine`.
  Closing time is the last moment to start a conversation. One that is already running carries on, and he goes
  off duty after it ends.

## Off-duty routine

While closed, `ELIFE_NpcRoutine` (server) puts the NPC in his own AI group. He walks to a free seat or bed within
20 m of his spot, picking one at random with nearer places more likely and beds a little more likely than seats
(breaks are when NPCs sleep). If nothing is free, he wanders within 20 m and keeps checking. He never despawns. At
opening time he gets up (from a bed, after the get-up has played), walks back, snaps onto his spot and serves again.
Nobody can push him off a seat or bed; if he ends up out of it anyway (ejected), he picks another place. While he
lies on a bed he sleeps (`ELIFE_NpcComponent.IsSleeping`, replicated): the
talk action is hidden and he says nothing.

A walk that hasn't arrived after 90 s counts as stuck, and he picks again. While walking he opens closed doors up to
2 m ahead of him (1.5 m in any direction) fully, so a door that swings towards him is open before he reaches it.
Vanilla AI only opens a door once it stands at it, and gets stuck behind the moving leaf. Doors he opened he closes
again once he is 2.5 m away (or at once when he stops to sit, lie or pause), unless somebody, him included while
resting, is within 1.5 m of the door. When he dies or goes away, his doors close at once.

If he hasn't moved for 2 s and someone is in front of him (two NPCs in a doorway, say), he gives way: he steps 2 m
back and waits until the other is 2 m clear of him and of the spot where he got stuck, at most 10 s, then walks on.
Of two NPCs stuck facing each other only the first to notice gives way. The wait counts towards the 90 s. Still
blocked after giving way twice on one walk, he picks another place (walking home, he just stops giving way).
Back at his spot, if somebody stands on it, he waits beside it until it's free.

The routine needs vanilla AI: an `SCR_AIWorld` with a Soldiers navmesh in the world. Without one he stays at his
spot, closed. On small worlds, set `UseNavmeshStreaming 0` on the navmesh, or AI never walks. After placing new
seats or obstacles, re-bake the navmesh so he can reach them.

## Opening-hours sign

`Prefabs/Props/Signs/SignWall_01_65x45_Hours.et` (`ELifeRPG_Props`). `ELIFE_NpcHoursSignComponent` shows the
hours of one NPC spot (its override first, else the NPC prefab's schedule) on the plate within 20 m, with a second
hours line when the schedule has a second range.

- By name: set "Spot Name" to the spot's entity name (e.g. `BankClerkSpot1`). Works across layers; the sign shows
  nothing if that spot doesn't exist or has no opening hours.
- Linked: place the sign under the spot in the World Editor (as its child, at any depth). It then always shows that
  spot's hours and moves with it, but has to live in the spot's layer.
- Unlinked: place it anywhere within 25 m of a spot with opening hours; it shows the nearest one.

Use one of the first two when several NPCs with different hours stand close together. With none, the plate stays
blank. Change the heading or the range on the component.
Tick "Show name" to put the spot's `m_sMapName` in the heading instead (long names shrink to fit; without a name
the heading stays).

A sign also puts its spot on the map (one marker per spot, however many signs): the NPC's `m_sMapIcon`
(`ELIFE_NpcComponent`, a white icon; the bank clerk's is `ELifeRPG_NPC/UI/Textures/Map/ELIFE_MapIcon_Bank`), and
"name · open until 15:00" or "name · closed · opens 16:00" (name: the spot's `m_sMapName`, e.g. the bank or shop, else
the NPC's display name), in the NPC's `m_vMapColor` while open (sRGB; the clerk's is the bank gold, black falls
back to the marker config's green) and grey while closed. Each client works
the state out from the hours and the in-game clock, and refreshes it while the map is open. Logic in
`ELIFE_NpcMapMarker.c` (Core); the marker type is added to the game mode's marker config,
`Configs/Map/ELIFE_MapMarkerConfig.conf`, where the open and closed colours are set.

## Facade lettering

`Prefabs/Props/Signs/FacadeLettering.et` (`ELifeRPG_Props`). `ELIFE_FacadeLetteringComponent` (Core) spells a name
in 3D capitals, e.g. the bank's name on its facade. Every client builds the letters itself (a dedicated server has
none); they also appear in the World Editor, so the sign can be placed by eye, but are never saved into the layer.

Place it with its origin on the wall at the baseline, its front (+Z) facing the street; the text is centred on it.
The text is "Text" if set (may be a string table key), else the `m_sMapName` of the spot named in "Spot Name", else
of a spot it is placed under, else of the nearest named spot within "Spot Range" (40 m).

- "Glyph Set": the font, `Configs/Signs/GlyphSet_Serif.conf` (bank) or `GlyphSet_Sans.conf` (shops).
- "Letter Height": capital height in metres. "Max Width": longer texts get smaller letters (0: no limit).
- "Material": letter colour, e.g. `Assets/Props/Signs/Letters/Data/Letter_Gold.emat`; empty is dark metal.

The fonts have A-Z, Ä, Ö, Ü, 0-9 and `& - . '`. Lowercase is capitalised, ß becomes SS, and any other character
leaves a gap (with a warning in the log). A glyph set is an `ELIFE_GlyphSet` config: per character a mesh (cap
height 1 m, facing +Z, origin at the baseline where reading starts, material slot `Letter`) and its advance in cap
heights.

## Seats and beds

Seats and beds are compartment slots, like vehicle seats: nobody can push a character out of one. Players get "Sit"
or "Lie down" from within 0.9 m of a free slot's entry point. They first turn in place to the slot's facing, then
get in, and the sit-down or lie-down clip plays. "Get up" or jump plays the stand-up clip, then they get out where they
are. While seated or lying they keep the normal action menu and gadgets, and first person follows the upper body under
free look. Dead or unconscious, they drop out of the slot where they are. Off-duty NPCs use the same props (see
above).

- Logic in `ELifeRPG_Core` (`Scripts/Game/Features/Poses`): `ELIFE_PosePropComponent` marks a prop as a seat or bed
  (`m_bBed`; `m_bOneOccupant` for a single bed with a slot per side), runs the turn and get-in, and lists free slots
  for NPCs (`FindFree`, `Reserve`, `GetIn`). `ELIFE_PoseStandUp` is the stand-up and `ELIFE_StandUpAction` the get-out
  action. `ELIFE_PoseCollision` turns the occupant's collision off in the slot and puts a local box around the
  body in the idle. `ELIFE_PoseCamera`, `ELIFE_PoseInteraction` and `ELIFE_PoseAIGroup` adjust vanilla behaviour for
  seats and beds. `ELIFE_Poses` has the character helpers (`GetPoseSlot`).
- Clips: `ELifeRPG_Props/anims/Poses/ELIFE_Pose.agr` (sit and stand-up from `anims/Poses`, the lie clips from
  `anims/Beds`). The slot's `SeatType` picks them: 0 sit, 1 lie on the left side, 2 lie on the right side.
- Props: `ELifeRPG_Props/Prefabs/Props/{Civilian,Furniture,Military,Services}` override the vanilla prefabs at the
  same path and GUID, so every vanilla chair, bench, sofa and bed of these models gets them, colour variants included.

To make a new seat or bed, override the vanilla prefab (same path) and copy the setup from a similar one
(`Chair_01_base.et` for one seat, `BedDouble_01.et` for two slots):

1. `ELIFE_PosePropComponent`, with `m_bBed` on beds.
2. `SCR_BaseCompartmentManagerComponent` with one `CargoCompartmentSlot` per place: `SCR_GetInUserAction` as
   compartment action, `ELIFE_StandUpAction` as get-out action, `PassengerPositionInfo` at the place,
   `ForcedFreeLook 1`, `SkipCharacterDoorAnimationsFromSeat 1`, `EjectUnconsciousAndDeadCharacters 0` and the
   `SeatType`. Each slot gets a door (`FakeDoor 1`, `GetInTeleport 1`, `GetOutTeleport 1`) with an entry point in
   front of the place, facing it. The engine's animated get-in blocks every later get-out.
3. `ActionsManagerComponent`: one `UserActionContext` per slot (the door's `ContextName`) and `elife_seated`.
4. `CompartmentAnimationComponent` with `ELIFE_Pose.agr`, `ELIFE_Pose_prop.asi` and the `ELIFE_Pose_player.asi`
   injection on binding `Vehicle`, and `SCR_VehicleCameraDataComponent`.
5. Don't scale the prop: the occupant is its hierarchy child and takes its scale. Restart Workbench for a new
   prefab and re-bake the navmesh where you place it.

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
- Hours: 09:00–15:00 and 16:00–02:00 (the schedule's default times with `m_bSecondRange` ticked: a break of one
  hour, open into the night). Closed, he says `CLOSED` and points to the sign.
- Context: when a visit starts, the server looks up the customer's accounts (`ELIFE_EClerkAccountStatus`). He
  greets customers of his bank with "welcome back", and anyone else with the in-game time of day or a plain
  hello. The result also reaches the client for `ELIFE_ClerkDialogueCondition_HasAccount` and
  `ELIFE_ClerkDialogueCondition_OpenedThisVisit`.
- Texts: `Language/ELIFE_NPC_Localization.st` in the package. Subtitles are the exact wording he speaks:
  `ELIFE-ClerkVO_<VOICE>_<LINE>[_<variant>]`, generated with the voice tooling.
