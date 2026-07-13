# Credits

## Upstream lineage

FnQL builds on a long id Tech 3 and Quake Live reconstruction lineage:

- [Quake III Arena SDK](https://github.com/id-Software/Quake-III-Arena) for
  the original open-sourced engine code and GPL foundation.
- [ioquake3](https://github.com/ioquake/ioq3) for the long-running
  community-maintained continuation of the Quake III codebase.
- [Quake3e](https://github.com/ec-/Quake3e) for the modernized engine
  foundation that FnQ3 originally built on directly.
- [FnQ3](https://github.com/themuffinator/FnQ3) for the imported modernized
  renderer, audio, platform, package, tooling, and documentation baseline.
- [QL-SRP](https://github.com/themuffinator/QL-SRP) for reconstructed Quake
  Live behavior, reference workflow, symbol evidence, and compatibility
  direction.

## Referenced and adapted work

FnQL also draws on ideas, compatibility references, or adapted implementations
from other projects:

- [CPMADevs CNQ3](https://bitbucket.org/CPMADevs/cnq3) for the rainbow text
  color escape support lineage.
- [Spearmint](https://github.com/zturtleman/spearmint) for the
  `bsp_q3ihv.c`-based Quake 3 IHV / `IBSP v43` map loading reference.
- [WolfcamQL](https://github.com/brugal/wolfcamql) for cross-checking legacy
  demo and Quake Live-adjacent compatibility behavior.
- Luigi Auriemma's [qldec](https://aluigi.altervista.org/papers.htm#qldec) for
  the published Quake Live beta encrypted `.pk3` XOR table reference.
- [DarkMatter-Q2](https://github.com/Paril/DarkMatter-Q2) for cross-checking
  Quake II `.pak` and `.wal` compatibility support.
- Mikko Mononen's [FontStash](https://github.com/memononen/fontstash) and Sean
  Barrett's `stb_truetype` for the pinned historical rasterizer used by the
  retail-compatible host text path.

## Project thanks

Thanks to the broader Quake, Quake III, and Quake Live communities: players,
server admins, mod authors, mappers, tool authors, testers, reverse engineers,
and maintainers who keep these games understandable and playable.

## Trademark note

FnQL is an independent project and is not affiliated with, endorsed by, or
sponsored by id Software, Bethesda, ZeniMax Media, or Valve. Quake, Quake III
Arena, and Quake Live are trademarks of their respective owners.
