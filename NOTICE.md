# Notices

## Modification notice (GPLv3, section 5a)

This is a **modified version** of melonDS Android (© Rafael Caetano and contributors), which is based on melonDS
(© Arisotura and the melonDS team). Both are licensed under the GNU General Public License v3; see [LICENSE](LICENSE).

- Modified by: RicksCodes (the author of this fork).
- Date of the modifications: October 2026 (last change 10 October 2026).
- Based on `rafaelvcaetano/melonDS-android` commit `c42995caff0144f37ad9791dc9bc96d82e8a6f65` and
  `rafaelvcaetano/melonDS-android-lib` commit `431ab4bd0003c4356e25fce89640ae1004579b9b`.
- What changed: see [README.md](README.md), especially the "Summary of changes" and "File map" sections. Diff against
  the commits above for the exact changes.

This fork is distributed under the same GPLv3 license, with the complete corresponding source code in this repository.
It comes with **no warranty**, as stated in the license.

## Third-party components added by this fork

| Component | Used for | License | Notice |
|---|---|---|---|
| Lua 5.1.5, © 1994-2012 Lua.org, PUC-Rio | Script engine (`app/src/main/cpp/lua51/`) | MIT | [third_party_licenses/Lua-5.1-MIT.txt](third_party_licenses/Lua-5.1-MIT.txt); the notice is also kept in `lua51/lua.h` |
| Roboto font, © Google | Button labels (A, B, X, Y, L, R, START, SELECT) **converted to vector outlines** in `app/src/main/res/drawable/*.xml` | Apache License 2.0 | [third_party_licenses/Apache-2.0.txt](third_party_licenses/Apache-2.0.txt). Modification: glyph outlines extracted with fontTools and embedded as path data; the font file itself is not distributed |
| X11 "misc-fixed" 5×7 font | Text drawn by Lua scripts (`app/src/main/cpp/lua/LuaFont.h`) | Public domain | — |

Third-party components that were already part of melonDS Android keep their own licenses, unchanged:

- melonDS-android-lib (GPLv3), see `melonDS-android-lib/LICENSE`;
- oboe (Apache 2.0), see `app/src/main/cpp/oboe/LICENSE`;
- enet (MIT), see `app/src/main/cpp/enet/LICENSE`;
- FAAD2 (GPLv2+), see `app/src/main/cpp/faad2/COPYING`;
- and the others listed in [README-upstream.md](README-upstream.md).

## Interfaces and ideas from other projects

- **DeSmuME** (GPLv2+, © the DeSmuME team): the Lua API (`memory`, `gui`, `input`, `joypad`, `emu`...) is
  **reimplemented** from its public documentation and behaviour, so that DeSmuME scripts run unchanged.
  No DeSmuME source code was copied.
- **LuaBitOp** (MIT, © Mike Pall): the `bit` library reproduces its semantics. No LuaBitOp code was copied.
- **FCEUX**: the `AND`/`OR`/`XOR`/`SHIFT`/`BIT` helper globals mirror the ones some DeSmuME/FCEUX scripts expect.

## Example scripts

`tools/lua/Pt_Natura_Strumento_DeSmuME.lua`, `tools/lua/platinum-item-and-nature.lua` and `tools/lua/Pt_Autoplay.lua` are derived from
**`Pt_RNG_DeSmuMe.lua`**, a Pokémon Platinum RNG script from Real96's **PokeLua** collection
(https://github.com/Real96/PokeLua/tree/main/Gen%204), the one recommended by https://www.pokemonrng.com. They were
written for testing and reuse its pointer offsets, block-order table and decryption logic, and its English item list.
All credit for that research goes to its authors, and these files will be changed or removed on request.

`tools/lua/Pt_Autoplay.lua` also uses Platinum memory addresses (battle indicator, foe and party pointers, language
offsets) and the "touch RUN" flee position documented by **pokebot-nds** (wyanido and contributors,
https://github.com/wyanido/pokebot-nds). Only those facts were reused; the script's code was written from scratch.

## Trademarks and content

- Nintendo DS and Nintendo DSi are trademarks of Nintendo. Pokémon is a trademark of Nintendo, Creatures Inc. and
  GAME FREAK inc.
- This project is **not affiliated with or endorsed by** Nintendo, The Pokémon Company, the melonDS team, the
  melonDS Android project, or the DeSmuME team.
- No ROMs, BIOS, firmware or other copyrighted Nintendo content are included. Use only dumps of games and hardware you
  own.
