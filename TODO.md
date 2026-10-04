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
