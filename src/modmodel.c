/* modmodel.c - port extra: a monster's skinned meshes from mods/models/<Monster>.gltf (modmodel.h) */
#include "modmodel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "plat.h"
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

/* the game's names and the export's: compared without case and without the bundles' "000" in front */
static const char *bare(const char *s)
{
    if (!s)
        return "";
    while (*s == '\x13')
        s++;
    return strncmp(s, "000", 3) == 0 ? s + 3 : s;
}

static int same(const char *a, const char *b) { return _stricmp(bare(a), bare(b)) == 0; }

/* a tree node's name ends with the joint's (the trees' names can carry a model prefix) */
static int node_is(const char *node, const char *joint)
{
    const char *n = bare(node), *j = bare(joint);
    size_t ln = strlen(n), lj = strlen(j);
    return lj && ln >= lj && _stricmp(n + ln - lj, j) == 0 && (ln == lj || n[ln - lj - 1] == '|' || n[ln - lj - 1] == ' ');
}

static cgltf_data *load(const char *monster, char *path, size_t cap)
{
    static const char *const EXT[] = { ".gltf", ".glb" };
    for (int i = 0; i < 2; i++) {
        snprintf(path, cap, "mods/models/%s%s", monster, EXT[i]);
        FILE *f = fopen(path, "rb");
        if (!f)
            continue;
        fclose(f);
        cgltf_options o;
        memset(&o, 0, sizeof o);
        cgltf_data *d = NULL;
        if (cgltf_parse_file(&o, path, &d) != cgltf_result_success)
            return printf("model mod: %s: not a glTF\n", path), NULL;
        if (cgltf_load_buffers(&o, d, path) != cgltf_result_success) {
            printf("model mod: %s: its buffers not found\n", path);
            cgltf_free(d);
            return NULL;
        }
        return d;
    }
    return NULL;
}

int modmodel_mesh(const char *monster, const char *entry, const Bundle *mb, const Tree *tree, Mesh *m)
{
    char path[256];
    cgltf_data *d = load(monster, path, sizeof path);
    if (!d)
        return 0;
    const cgltf_node *node = NULL;                   /* the node of the mesh named as the entry (its skin with it) */
    for (cgltf_size i = 0; i < d->nodes_count && !node; i++)
        if (d->nodes[i].mesh && same(d->nodes[i].mesh->name, entry))
            node = &d->nodes[i];
    if (!node) {
        cgltf_free(d);
        return 0;
    }
    const cgltf_mesh *gm = node->mesh;
    const cgltf_skin *skin = node->skin ? node->skin : d->skins_count ? &d->skins[0] : NULL;
    int map[256];                                    /* the skin's joint -> the skeleton's node */
    int nj = skin ? (int)(skin->joints_count < 256 ? skin->joints_count : 256) : 0, missing = 0;
    for (int j = 0; j < nj; j++) {
        map[j] = -1;
        for (uint32_t k = 0; tree && k < tree->n && map[j] < 0; k++)
            if (node_is(tree->nodes[k].name, skin->joints[j]->name))
                map[j] = (int)k;
        if (map[j] < 0)
            missing++;
        if (getenv("RAMPAGE_MODMODEL_TRACE"))
            printf("model mod: joint %d %s -> node %d %s\n", j, skin->joints[j]->name, map[j],
                   map[j] >= 0 ? tree->nodes[map[j]].name : "-");
    }
    Mesh out;
    memset(&out, 0, sizeof out);
    out.parts = calloc(gm->primitives_count ? gm->primitives_count : 1, sizeof *out.parts);
    for (cgltf_size pi = 0; pi < gm->primitives_count; pi++) {
        const cgltf_primitive *pr = &gm->primitives[pi];
        const cgltf_accessor *pos = NULL, *nrm = NULL, *uv = NULL, *jo = NULL, *we = NULL;
        for (cgltf_size a = 0; a < pr->attributes_count; a++) {
            const cgltf_attribute *at = &pr->attributes[a];
            if (at->type == cgltf_attribute_type_position) pos = at->data;
            else if (at->type == cgltf_attribute_type_normal) nrm = at->data;
            else if (at->type == cgltf_attribute_type_texcoord && at->index == 0) uv = at->data;
            else if (at->type == cgltf_attribute_type_joints && at->index == 0) jo = at->data;
            else if (at->type == cgltf_attribute_type_weights && at->index == 0) we = at->data;
        }
        if (pr->type != cgltf_primitive_type_triangles || !pos || !pos->count)
            continue;
        MeshPart *p = &out.parts[out.nparts];
        uint32_t n = (uint32_t)pos->count;
        p->nverts = n;
        p->pos = malloc(sizeof(float) * 3 * n);
        for (uint32_t v = 0; v < n; v++)
            cgltf_accessor_read_float(pos, v, p->pos + 3 * v, 3);
        if (nrm) {
            p->nrm = malloc(sizeof(float) * 3 * n);
            for (uint32_t v = 0; v < n; v++)
                cgltf_accessor_read_float(nrm, v, p->nrm + 3 * v, 3);
        }
        if (uv) {
            p->uv = malloc(sizeof(float) * 2 * n);
            for (uint32_t v = 0; v < n; v++)
                cgltf_accessor_read_float(uv, v, p->uv + 2 * v, 2);
        }
        p->bones = calloc(n, sizeof *p->bones);
        p->weights = calloc(n, sizeof *p->weights);
        for (uint32_t v = 0; v < n; v++) {
            cgltf_uint js[4] = { 0, 0, 0, 0 };
            float ws[4] = { 1, 0, 0, 0 };
            if (jo)
                cgltf_accessor_read_uint(jo, v, js, 4);
            if (we)
                cgltf_accessor_read_float(we, v, ws, 4);
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                int b = (int)js[k] < nj ? map[js[k]] : -1;
                p->bones[v][k] = (uint8_t)(b < 0 ? 0 : b);
                p->weights[v][k] = b < 0 ? 0 : ws[k];
                sum += p->weights[v][k];
            }
            if (sum > 0)
                for (int k = 0; k < 4; k++)
                    p->weights[v][k] /= sum;
            else
                p->weights[v][0] = 1;
        }
        if (pr->indices) {
            p->ntris = (uint32_t)(pr->indices->count / 3);
            p->tris = malloc(sizeof(uint32_t) * 3 * (p->ntris ? p->ntris : 1));
            for (uint32_t t = 0; t < 3 * p->ntris; t++)
                p->tris[t] = (uint32_t)cgltf_accessor_read_index(pr->indices, t);
        } else {
            p->ntris = n / 3;
            p->tris = malloc(sizeof(uint32_t) * 3 * (p->ntris ? p->ntris : 1));
            for (uint32_t t = 0; t < 3 * p->ntris; t++)
                p->tris[t] = t;
        }
        /* each triangle facing its vertices' normals, as the game's own do (by the right hand: 99.5 % of George's):
         * whatever winding the file had (glTF's is the same, but an exporter may slip), the culling sees the outside */
        for (uint32_t t = 0; p->nrm && t < p->ntris; t++) {
            uint32_t *x = p->tris + 3 * t;
            if (x[0] >= n || x[1] >= n || x[2] >= n) {
                x[0] = x[1] = x[2] = 0;              /* (a broken index: the triangle dropped) */
                continue;
            }
            const float *p0 = p->pos + 3 * x[0], *p1 = p->pos + 3 * x[1], *p2 = p->pos + 3 * x[2];
            float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
            float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
            float fn[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
            float s = 0;
            for (int k = 0; k < 3; k++)
                s += fn[k] * (p->nrm[3 * x[0] + k] + p->nrm[3 * x[1] + k] + p->nrm[3 * x[2] + k]);
            if (s < 0) {
                uint32_t sw = x[0];
                x[0] = x[1];
                x[1] = sw;
            }
        }
        int mat = pr->material && pr->material->name ? bdp_find_name(mb, bare(pr->material->name)) : -1;
        if (mat < 0 && pr->material && pr->material->name)
            mat = bdp_find_name(mb, pr->material->name);
        if (mat < 0 && out.nparts < m->nparts)       /* (not the game's name: the game's part of that number) */
            mat = (int)m->parts[out.nparts].material;
        p->material = mat < 0 ? (m->nparts ? m->parts[0].material : 0) : (uint32_t)mat;
        if (out.nparts < m->nparts)
            p->blend = m->parts[out.nparts].blend;
        out.nparts++;
    }
    cgltf_free(d);
    if (!out.nparts) {
        mesh_free(&out);
        printf("model mod: %s: %s has no triangles; the game's kept\n", path, entry);
        return 0;
    }
    uint32_t verts = 0;
    for (uint32_t i = 0; i < out.nparts; i++)
        verts += out.parts[i].nverts;
    printf("model mod: %s %s from %s (%u parts, %u vertices%s)\n", monster, entry, path, out.nparts, verts,
           missing ? ", some joints not the game's bones: on the root" : "");
    mesh_free(m);
    *m = out;
    return 1;
}

