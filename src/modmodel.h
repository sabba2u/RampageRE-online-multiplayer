/* modmodel.h - port extra (docs/TODO.md "Model import"): a monster's look from mods/models/<Monster>.gltf (or .glb).
 *
 * The glTF is the one tools/anim.py writes (python tools/anim.py build/vol George build/gltf), edited and saved back:
 * the same skeleton (its joints named as the game's bones), its skinned meshes named as the game's entries
 * ("000GEORGE_MESH|George_eyes"), positions in the game's own frame (the export's "Z up" root node turns it for a
 * viewer; the mesh itself is not turned). A mesh of that name replaces the game's: every vertex with its normal, uv
 * and up to four bones and weights, the triangles, each primitive's material by the game's material name (else the
 * game's part of the same number). All the monster's moves play on it as on the game's own. Textures stay the game's
 * (change them with a texture pack, docs/TEXTURES.md); the cloth (hair, whiskers) stays the game's. */
#ifndef RAMPAGE_MODMODEL_H
#define RAMPAGE_MODMODEL_H
#include "data.h"

/* the mesh entry `entry` (bdp_name) of `monster` from mods/models when there is one: *m (as mesh_read made it) freed
 * and replaced, 1; else 0 and *m as it was. tree: the monster's skeleton (its node names). */
int modmodel_mesh(const char *monster, const char *entry, const Bundle *mb, const Tree *tree, Mesh *m);

#endif
