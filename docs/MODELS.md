# Model mods (port extra)

A monster's body can be replaced by your own model: `mods/models/<Monster>.gltf` (or `.glb`) beside the game, read
when the monster is loaded (`src/modmodel.c`, glTF read by cgltf 1.14, `src/cgltf/`, MIT). Every move of the
monster plays on it as on the game's own, because it keeps the game's skeleton.

## Making one

1. Export the monster as it is (the disc extracted once into `build/vol`: `python tools/iso9660.py <disc.iso>
   build/disc`, `python tools/pvol.py build/disc/RAMPAGE.VOL build/vol`):
   `python tools/anim.py build/vol George build/gltf` writes `George.gltf`, `George.bin`, its textures and all its
   animations.
2. Edit it (Blender: File > Import > glTF): move, add or remove vertices, re-weight them to the bones, keep the
   skeleton (the joints' names are the game's bones: `000Bip01 Head` ...) and the mesh's name
   (`000GEORGE_MESH|George_eyes`). Positions are in the game's own frame: the export's root node `Z up` turns it for
   a viewer; keep that node and export with it.
3. Save as glTF into `mods/models/George.gltf` (the `.bin` beside it) and start the game: its console says
   `model mod: George 000GEORGE_MESH|George_eyes from mods/models/George.gltf (N parts, N vertices)`.

## What is taken from it

- Each skinned mesh of the monster named in the file replaces the game's of that name: positions, normals, uvs, up
  to four bones and weights per vertex (joints by their bones' names; the weights normalised), the triangles.
- Each triangle is turned to face its vertices' normals, as the game's own do, whatever winding the file has.
- Materials by the game's material names (`000George_map`); a primitive whose material is not one of the game's
  takes the game's part of the same number. The pictures stay the game's: retexture with a texture pack
  (docs/TEXTURES.md, `--dumptex`, `mods/textures/`).
- Not taken: the cloth (hair, whiskers: simulated from the game's particles), new textures from the file, a new
  skeleton or new animations (a monster's moves are its MONSTER_DATA's clips on its own bones).

## Checked

- The round trip: George exported and loaded back draws the very same picture as the game's own (the --shot's
  SHA-256 equal). This needed the export's triangles wound as the game winds them (tools/mesh.py `triangles`:
  by the vertex's place in its section, an odd one turning it); before, a third of them faced inward.
- An edit: the vertices on George's head bones scaled 1.6 about the head: a big-headed George, every move playing.
