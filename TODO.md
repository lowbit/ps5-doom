# TODO

## Add-ons (PWADs)

The launcher only starts the main games; add-on WADs are refused by the importer and the send
screen. What the 2024 re-release add-ons contain (checked 2026-10-04):

| WAD | Maps | Needs beyond loading the file |
| --- | --- | --- |
| `nerve.wad` (*No Rest for the Living*) | MAP01-09, DOOM II format | `UMAPINFO`: level names, music, intermission pictures, MAP08 ends the game, secret exit to MAP09 |
| `masterlevels.wad` (*Master Levels*) | MAP01-21 | `UMAPINFO` as above plus its own skies (`SKYM1`...); its new textures (`PNAMES`, `TEXTURE1`, patches) replace the IWAD's the vanilla way |
| `sigil.wad` (*SIGIL*) | E5M1-9 | `UMAPINFO`, episode 5 (menu entry `M_EPI5`, `SKY5`, intermission), `DEHACKED` (par times only) |
| `sigil2.wad` (*SIGIL II*) | E6M1-9 | As SIGIL for episode 6, plus its own flats (a second `F_START` block, which id's engine cannot merge: it only sees the last block) and Boom's `ANIMATED`/`SWITCHES` |
| `id1.wad` (*Legacy of Rust*) | MAP01-16 | The id24 spec: a 63 KB `DEHACKED` with new things, `DECOHACK`, `SBARDEF`, `SKYDEFS`, new sprites and flats, `id24res.wad`; in effect a modern engine |

Loading itself exists: id's `-file` still works. Order of work:

- [ ] Pick an add-on in the launcher under its game and start it with `-file`; accept PWADs in the
      importer and the send screen into their own folder; give each add-on its own saves.
- [ ] A `UMAPINFO` reader (level names, music, skies, progression, intermission and end screens,
      episode entries). Covers *No Rest for the Living* and the *Master Levels*, and is the base for
      SIGIL and for community WADs that ship `UMAPINFO`.
- [ ] Episodes 5 and 6 for *SIGIL* and *SIGIL II*; flat merging and `ANIMATED`/`SWITCHES` for *SIGIL II*.
- [ ] *Legacy of Rust* and most community WADs (Boom, MBF21, `DEHACKED`) need far more than id's
      engine: decide between extending it and moving the game core to a modern port (for example
      DSDA-Doom or Woof!) behind our PS5 layer.
