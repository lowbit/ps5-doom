# TODO

## Add-ons (PWADs)

The launcher only starts the main games; add-on WADs found in archives are skipped. Order of work:

- [ ] Pick an add-on in the launcher and load it on top of its game (`-file`), import PWADs from
      archives and links, and give each add-on its own saves.
- [ ] *No Rest for the Living* (`nerve.wad`) and the *Master Levels* (`masterlevels.wad`) from the
      2024 re-release: classic-format DOOM II maps, so they should run once loading works. Check
      their level order and secret exits.
- [ ] *SIGIL* and *SIGIL II* (`sigil.wad`, `sigil2.wad`): the re-release versions are episodes 5 and
      6, which need episode, intermission and sky support beyond episode 4.
- [ ] *Legacy of Rust* (`id1.wad` with `id1-*.wad` and `id24res.wad`): needs the id24 extensions
      (new weapons, monsters and the rest of the spec). Largest of these.

## Controller wording

- [ ] Doom's prompts still name keyboard keys: "press y or n", "press y to quit", "press a key",
      "press enter" (the strings in `d_englsh.h` and `d_french.h`: quit, end game, nightmare, quick
      save and load, save while dead, and the rest). Show the DualSense buttons instead (Cross for
      yes, Circle for no, Cross to continue), as text or as button glyphs drawn in the menu font.
- [ ] Help screen: *Read This!* and F1 show the keyboard help pages from the WAD (`HELP1`, `HELP2`,
      `HELP`, `CREDIT`). Replace them with a page that shows the DualSense controls from the README.

## Importer

- [ ] Folder links behind a redirect (for example a shorturl.at link to a folder): the listing's
      relative links are resolved against the typed address, not the address the redirect lands
      on, so the files cannot be opened. Learn the final address (`sceHttpSetRedirectCallback`,
      `CURLINFO_EFFECTIVE_URL` on the PC) and resolve against it. Short links straight to a file
      already work.
