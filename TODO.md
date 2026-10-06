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

## Found in console testing (2026-10-06)

- [ ] A crash on the console with no trace: The Ultimate DOOM (the 2024 `doom.wad`) on its attract
      demos with the main menu open, within a second after frame 3300 (about 95 s after the
      launcher). ShadowMountPlus saw an exception stop (`flags=0x008a4102`); `doom.log` ends at the
      capture of frame 3300, and the kernel log's crash lines were read past before they were kept.
      The same run again went 9000 frames without a crash, and the PC build (with AddressSanitizer,
      with and without audio) plays the same demos cleanly. Keep a kernel log recording during
      console runs (klogsrv, or polling the helper's `/api/ps5/klog`) to catch the next one.
- [ ] Games already installed count as added: sending the collection 7Z again reports "Added
      DOOM.WAD DOOM2.WAD PLUTONIA.WAD TNT.WAD" and the game list "Added 5 games" when nothing new
      arrived. Report only new games, and name them by title.
- [ ] The page's *On the console* list waits for a running upload (the server takes one request at a
      time), so a WAD added before a long archive shows up only after the archive is sent.
