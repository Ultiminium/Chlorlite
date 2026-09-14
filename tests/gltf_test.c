/* gltf_test — verifies glTF 2.0 import for BOTH containers: text .gltf + external
 * .bin (via export→import round-trip) and self-contained binary .glb (via a
 * hand-built minimal file). Pure logic (no renderer); prints "GLTF TEST: all
 * checks passed" and returns 0, else aborts. */
#include "cc/ccmodel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__); failures++; } \
} while (0)

/* build a small known quad (4 verts, 6 indices) */
static CCModel* make_quad(void) {
    CCMVertex v[4]; memset(v, 0, sizeof(v));
    float pos[4][3] = {{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    float uv[4][2]  = {{0,0},{1,0},{1,1},{0,1}};
    for (int i=0;i<4;i++){
        memcpy(v[i].pos, pos[i], 12);
        v[i].normal[2]=1.0f;
        memcpy(v[i].uv, uv[i], 8);
        v[i].color[0]=v[i].color[1]=v[i].color[2]=v[i].color[3]=255;
        v[i].tangent[0]=1; v[i].tangent[3]=1;
    }
    uint32_t idx[6] = {0,1,2, 0,2,3};
    CCModel* m = ccm_model_new("quad");
    ccm_set_geometry(m, v, 4, idx, 6);
    return m;
}

/* Hand-write a minimal valid .glb: one triangle, POSITION + indices only.
 * Layout: [12B header][JSON chunk][BIN chunk]. Returns path written, or NULL. */
static const char* write_min_glb(const char* path) {
    /* binary buffer: 3 positions (float3) then 3 indices (uint16), 4-byte pad */
    float   posv[9] = { 0,0,0,  1,0,0,  0,1,0 };     /* 36 bytes */
    uint16_t indv[3] = { 0,1,2 };                     /* 6 bytes  */
    uint8_t bin[64]; memset(bin, 0, sizeof(bin));
    memcpy(bin, posv, 36);
    memcpy(bin+36, indv, 6);
    uint32_t bin_len = 42;                            /* 36 positions + 6 indices */
    /* pad bin to 4 */
    while (bin_len % 4) bin[bin_len++] = 0;           /* → 44 */

    /* JSON referencing two accessors over one buffer (byteLength = bin_len).
       bufferView 0 = positions (offset 0, len 36), bufferView 1 = indices
       (offset 36, len 6). accessor 0 = VEC3/float count 3, accessor 1 = SCALAR/
       uint16 count 3. mesh 0 primitive: attributes.POSITION=0, indices=1. */
    char json[1024];
    int jn = snprintf(json, sizeof(json),
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"buffers\":[{\"byteLength\":%u}],"
        "\"bufferViews\":["
          "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
          "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":6}],"
        "\"accessors\":["
          "{\"bufferView\":0,\"byteOffset\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
          "{\"bufferView\":1,\"byteOffset\":0,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}]}",
        bin_len);
    /* pad JSON to 4 with spaces */
    while (jn % 4) json[jn++] = ' ';
    json[jn] = 0;

    FILE* f = fopen(path, "wb");
    if (!f) return NULL;
    uint32_t total = 12 + 8 + (uint32_t)jn + 8 + bin_len;
    uint32_t magic=0x46546C67u, version=2;
    fwrite(&magic,4,1,f); fwrite(&version,4,1,f); fwrite(&total,4,1,f);
    uint32_t jlen=(uint32_t)jn, jtype=0x4E4F534Au;   /* "JSON" */
    fwrite(&jlen,4,1,f); fwrite(&jtype,4,1,f); fwrite(json,1,jn,f);
    uint32_t blen=bin_len, btype=0x004E4942u;        /* "BIN\0" */
    fwrite(&blen,4,1,f); fwrite(&btype,4,1,f); fwrite(bin,1,bin_len,f);
    fclose(f);
    return path;
}

int main(void) {
    /* 1. text .gltf round-trip: export a known quad, re-import, compare -------- */
    CCModel* orig = make_quad();
    CHECK(orig && orig->geom.vertex_count == 4, "quad model built (4 verts)");

    const char* gpath = "/tmp/cc_gltf_test.gltf";
    bool exported = ccm_export_gltf(orig, gpath);
    CHECK(exported, "ccm_export_gltf wrote .gltf + .bin");

    CCModel* imp = ccm_import_gltf(gpath, "reimport");
    CHECK(imp != NULL, "ccm_import_gltf read the .gltf back");
    if (imp) {
        CHECK(imp->geom.vertex_count == 4, "round-trip preserved vertex count");
        CHECK(imp->geom.index_count == 6, "round-trip preserved index count");
        /* positions should match the originals (order preserved) */
        int pos_ok = 1;
        for (int i=0;i<4 && i<(int)imp->geom.vertex_count;i++)
            for (int k=0;k<3;k++)
                if (fabsf(imp->geom.vertices[i].pos[k] - orig->geom.vertices[i].pos[k]) > 1e-4f)
                    pos_ok = 0;
        CHECK(pos_ok, "round-trip preserved vertex positions");
        /* indices preserved */
        int idx_ok = (imp->geom.index_count==6);
        uint32_t expect[6]={0,1,2,0,2,3};
        for (uint32_t i=0;i<imp->geom.index_count && idx_ok;i++)
            if (imp->geom.indices[i] != expect[i]) idx_ok = 0;
        CHECK(idx_ok, "round-trip preserved indices");
        ccm_model_free(imp);
    }

    /* 2. binary .glb: hand-built triangle imports through the container path --- */
    const char* glb = write_min_glb("/tmp/cc_gltf_test.glb");
    CHECK(glb != NULL, "wrote minimal .glb fixture");
    CCModel* gimp = ccm_import_gltf(glb, "glb_tri");
    CHECK(gimp != NULL, "ccm_import_gltf parsed the binary .glb container");
    if (gimp) {
        CHECK(gimp->geom.vertex_count == 3, ".glb triangle has 3 vertices");
        CHECK(gimp->geom.index_count == 3, ".glb triangle has 3 indices");
        /* second vertex is (1,0,0) per our buffer */
        int v1ok = gimp->geom.vertex_count>=2 &&
                   fabsf(gimp->geom.vertices[1].pos[0]-1.0f)<1e-4f &&
                   fabsf(gimp->geom.vertices[1].pos[1]-0.0f)<1e-4f;
        CHECK(v1ok, ".glb vertex positions decoded from the BIN chunk");
        /* normals were absent → importer should have computed them (nonzero) */
        float nlen = 0;
        for (int k=0;k<3;k++) nlen += gimp->geom.vertices[0].normal[k]*gimp->geom.vertices[0].normal[k];
        CHECK(nlen > 0.5f, "missing normals were auto-computed for the .glb");
        ccm_model_free(gimp);
    }

    /* 3. error handling: nonexistent + garbage files return NULL, no crash ---- */
    CHECK(ccm_import_gltf("/tmp/does_not_exist_xyz.glb", "x") == NULL,
          "missing file returns NULL");
    FILE* jf = fopen("/tmp/cc_gltf_garbage.gltf","wb");
    if (jf){ fputs("not json at all", jf); fclose(jf); }
    CCModel* bad = ccm_import_gltf("/tmp/cc_gltf_garbage.gltf", "bad");
    CHECK(bad == NULL, "garbage .gltf returns NULL (no attributes)");
    if (bad) ccm_model_free(bad);

    ccm_model_free(orig);

    if (failures == 0) {
        printf("GLTF TEST: all checks passed (.gltf+.bin round-trip, binary .glb "
               "container, position/index decode, auto-normals, error handling)\n");
        return 0;
    }
    printf("GLTF TEST: %d check(s) FAILED\n", failures);
    return 1;
}
