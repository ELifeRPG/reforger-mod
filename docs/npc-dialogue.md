# NPC dialogue

Conversations with NPCs use one shared menu, `ELIFE_DialogueMenu`. A panel along the bottom of the screen shows
the NPC's name, their spoken line as a subtitle, the details under it (names, figures), and the player's numbered
replies. The world stays visible, so the player sees the NPC talking.

The bank clerk is the first NPC that uses it.

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

## Adding an NPC

1. Subclass `ELIFE_DialogueController`. Override:
    - `GetConfig()`: the NPC's tree.
    - `GetSpeaker()`: the NPC entity, so its lines become subtitles.
    - `GetSpeakerName()`: the name shown above the subtitle.
    - `IsAvailable()`: when the conversation must end, for example out of range or dead.
    - `OnEnd()`: cleanup, such as freeing the NPC on the server.
2. Add a line class with the NPC's own line enum: subclass `ELIFE_DialogueLine` and override `Speak()` to ask
   the server to say it. The server must check the line is one the client may ask for.
3. When a line is heard on a client, call `ELIFE_DialogueMenu.OnSpeakerSaid(npcEntity, subtitleText)`. The open
   conversation with that NPC shows it.
4. Add actions and details the NPC needs as subclasses. They reach the NPC through
   `ELIFE_YourController.Cast(menu.GetController())`.
5. Write the tree `.conf` and set `m_sStartNode`.
6. Open it with `ELIFE_DialogueMenu.Open(controller)` once the server has granted the conversation.

A form that opens on top of the conversation (like the clerk's transfer slip) calls `menu.Suspend()` before
opening and `menu.Resume()` plus `menu.ShowNode(...)` when it closes. While waiting for a server answer, use
`menu.SetWaiting(true, notice)`, which greys out the replies.

## Bank clerk

- Controller: `ELIFE_BankClerkDialogueController` in `Scripts/Game/UI/Menu/Banking/ELIFE_BankClerkDialogue.c`,
  with `ELIFE_ClerkDialogueLine`, the transfer slip and open-account actions, and the fee terms, new account
  and receipt details.
- Tree: `Configs/Banking/ELIFE_ClerkDialogue.conf`, set on `ELIFE_BankClerkComponent` (`m_sDialogueConfig`), so
  one clerk can get a different tree.
- Context: when a visit starts, the server looks up the customer's accounts (`ELIFE_EClerkAccountStatus`). He
  greets customers of his bank with "welcome back", and anyone else with the in-game time of day or a plain
  hello. The result also reaches the client for `ELIFE_ClerkDialogueCondition_HasAccount` and
  `ELIFE_ClerkDialogueCondition_OpenedThisVisit`.
- Subtitles are the exact wording he speaks: `ELIFE-ClerkVO_<VOICE>_<LINE>[_<variant>]`, generated with the
  voice tooling outside the repo.
