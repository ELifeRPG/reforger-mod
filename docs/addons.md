# Addons and publishing

ELifeRPG is published as several Workshop addons instead of one. `ELifeRPG_Core` is the base
framework. Content packages ("DLCs") ship a base setup of ready-made content on top of it. Server
owners use them as they are or replace them with their own, without touching Core.

## Layout

```
ELifeRPG_Dev
   │
   ▼
ELifeRPG ──► ELifeRPG_NPC
   │    ├──► ELifeRPG_Items
   │    ├──► ELifeRPG_Vehicles
   │    ├──► ELifeRPG_Vegetation
   │    └──► ELifeRPG_Props
   ▼
ELifeRPG_Core   ◄── every DLC depends on Core
   │
   └──► ELifeRPG_Assets, …   (base-content packages, art only)
```

Arrows are dependencies.

| Addon | Folder | GUID | Contains | Depends on |
| --- | --- | --- | --- | --- |
| `ELifeRPG_Core` | `addons/` | `66EB6BA548DD28C2` | Base framework: generic systems (scripts, configs, UI, localization, prefabs), base content such as the phone, and the vanilla full-file overrides | vanilla + base-content packages |
| `ELifeRPG_NPC` | `addons/dlcs/` | `97B65A48A10C989D` | DLC: NPCs and their content-specific logic (e.g. the bank clerk) | Core |
| `ELifeRPG_Items` | `addons/dlcs/` | `14E46A965EE53285` | DLC: base setup for items (empty for now) | Core |
| `ELifeRPG_Vehicles` | `addons/dlcs/` | `4B054C78918EAA22` | DLC: base setup for vehicles (empty for now) | Core |
| `ELifeRPG_Vegetation` | `addons/dlcs/` | `12BC1DE093734B8A` | DLC: base setup for vegetation (fruit trees and their fruit: apple, plum) | Core |
| `ELifeRPG_Props` | `addons/dlcs/` | `AF193338B10BF1C5` | DLC: placeable props built on Core systems (seats and beds, opening-hours sign, facade lettering, ATMs) with their art and sounds | Core |
| `ELifeRPG` | `addons/` | `596AFBF40E2150FD` | Sample world addon. Pulls in Core and all DLCs | Core + all DLCs |
| `ELifeRPG_Dev` | `addons/` | `B23160BEBCAB2D32` | `DevWorld` and test layers. Published unlisted for internal servers | `ELifeRPG` |

Servers list a single mod: `ELifeRPG` (`596AFBF40E2150FD`) or their own implementation of it (see
[Building your own server mod](#building-your-own-server-mod)). `596AFBF40E2150FD` is the GUID the
original single addon had, so existing server configs keep working and get the other addons through
dependencies.

**Base-content packages** (`ELifeRPG_Assets`, …) hold the art of Core's base content, such as the
phone's textures and model, apart from the code. Core depends on them, so they are always part of
the stack. They sit in `addons/` next to `ELifeRPG`, not in `addons/dlcs/`. Every addon in `addons/` other
than `ELifeRPG_Core`, `ELifeRPG` and `ELifeRPG_Dev` counts as one.

### Building your own server mod

`ELifeRPG` is only a sample. A server owner builds their own world addon the same way: it depends on
`ELifeRPG_Core`, picks any of the DLCs and replaces the others with their own packages (own items,
own vehicles, …). A third-party Workshop mod, e.g. a vehicle mod, is added as a further dependency
of that world addon, which configures it.

## Rules

1. **Dependencies only point one way:** base-content packages ← `Core` ← DLCs ← `ELifeRPG` ← `Dev`.
   A DLC depends on Core and never on another DLC. Nothing but `ELifeRPG` and `ELifeRPG_Dev`
   depends on a DLC.
2. **Core holds the generic systems.** Scripts, configs, UI, localization and prefabs that every
   server needs, plus base content such as the phone. Core never references DLC content: a
   `ResourceName` attribute that points at content defaults to `""` and is set in the DLC or the
   world addon.
3. **A DLC holds its content and everything specific to it:** prefabs, art, configs, UI,
   localization and the scripts built on top of Core's systems. Removing a DLC must leave no
   trace in Core or any other package, and Core must still compile.
4. **Base-content packages hold art only:** meshes (`.xob`, `.fbx`, `.txo`), materials and
   textures (`.emat`, `.edds`), sounds (`.wav`, `.acp`). They depend only on vanilla.
5. **Keep the internal path when moving a file between addons**, and move its `.meta` with it.
   References are `{GUID}path`, and the GUID lives in the `.meta`. With the same path and the `.meta`
   moved along, every reference still resolves. Move files in Workbench where possible so the
   `.meta` stays in sync.
6. Vanilla full-file overrides (`Configs/System/chimeraMenus.conf`, `chimeraInputCommon.conf`) may
   exist in only one addon: `ELifeRPG_Core`.
7. Source files (`.blend`, `.obj`, …) stay in the repo-level `assets/` folder and are never shipped.

To check rules 1–6, run:

```sh
python3 tools/check_addons.py addons
```

It reports, and exits with code 1 on:

- references to an addon outside the referencing addon's dependencies (wrong direction)
- references whose GUID is unknown but whose path exists in an addon (GUID mismatch)
- duplicate GUIDs
- dependencies against rule 1, and DLCs that don't depend on Core
- logic (`.c`, `.et`, `.conf`, `.layout`, `.ent`, `.layer`) in a base-content package
- vanilla full-file overrides outside Core

It also warns, without failing, about `.meta` files whose stored path differs from the file's real
path. It cannot detect references to files that were deleted, because those look the same as
references to vanilla content.

## Size budget

- The Workshop accepts up to **7 GB per addon** (raised from 5 GB in 1.2.1.189). Keep each addon
  under ~3 GB. Split a DLC or base-content package before it gets there.
- No per-file limit is documented, but big uploads tend to time out, so terrain gets its own addon.
- Consoles have a total mod storage budget (PS5 25 GB, Xbox 50 GB) and download at about 3 MB/s.
  Keep the required stack small.

## Releasing

All ELifeRPG addons share **one version**, taken from `VERSION` at the repository root. Every
release republishes every addon with that version. The Workshop downloads patches between versions,
so players fetch next to nothing for addons whose content didn't change.

1. Bump `VERSION` (`MAJOR` = breaking: removed/renamed prefabs or assets, backend protocol;
   `MINOR` = features and content; `PATCH` = fixes).
2. Run `python3 tools/check_addons.py addons`.
3. In Workbench, publish in dependency order, each addon with the version from `VERSION`:
   base-content packages → `ELifeRPG_Core` → DLCs → `ELifeRPG` → `ELifeRPG_Dev` (unlisted).
   A dependency has to exist on the Workshop before an addon that depends on it can be published.
4. Tag the commit `vX.Y.Z`.

Servers either leave mod versions unpinned (always the latest) or pin every ELifeRPG addon to the
same release number. The Workshop keeps only the last 50 versions of an addon.

## Working in Workbench

Register every addon folder under `addons/` and `addons/dlcs/` as an addon directory, then open the
addon you work in: `ELifeRPG_Dev` to play the test world, `ELifeRPG_Core` for the generic systems,
or the DLC whose content you change. Create new files inside the addon they belong to, so their
`.meta` is created there.
