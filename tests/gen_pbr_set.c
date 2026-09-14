/* gen_pbr_set — writes a scanned-style PBR material set into a folder as PNGs,
 * the way a real asset pack ships (separate maps, conventional filenames), so
 * cc_material_load_pbr can be exercised against real files on disk.
 * Produces: basecolor (sRGB-authored color), normal (tangent-space), roughness
 * (single channel), metallic (single channel), ao (single channel). */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv){
    const char* dir = (argc>1)?argv[1]:"/tmp/pbr_set";
    char cmd[512]; snprintf(cmd,sizeof(cmd),"mkdir -p %s", dir); system(cmd);
    const int W=256,H=256;

    uint8_t* base = malloc(W*H*3);
    uint8_t* nrm  = malloc(W*H*3);
    uint8_t* rgh  = malloc(W*H);
    uint8_t* met  = malloc(W*H);
    uint8_t* ao   = malloc(W*H);

    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
        float u=x/(float)W, v=y/(float)H;
        /* tile pattern: grout lines every 1/4, tiles between */
        float gx = fabsf(fmodf(u*4.0f,1.0f)-0.5f);
        float gy = fabsf(fmodf(v*4.0f,1.0f)-0.5f);
        int grout = (gx>0.44f || gy>0.44f);
        int tile = (((int)(u*4)+(int)(v*4))&1);

        /* BASECOLOR — vivid so sRGB vs linear is obvious. Authored in sRGB
         * (these are the byte values a texture artist would paint). */
        int i3=(y*W+x)*3;
        if(grout){ base[i3]=60; base[i3+1]=55; base[i3+2]=50; }
        else if(tile){ base[i3]=200; base[i3+1]=70; base[i3+2]=55; }   /* terracotta */
        else { base[i3]=70; base[i3+1]=110; base[i3+2]=180; }          /* slate blue */

        /* NORMAL — bevel toward grout lines (tangent space, +Z up) */
        float nx = (gx>0.40f)? (fmodf(u*4.0f,1.0f)<0.5f? -0.6f:0.6f):0.0f;
        float ny = (gy>0.40f)? (fmodf(v*4.0f,1.0f)<0.5f? -0.6f:0.6f):0.0f;
        float nz = 1.0f; float l=sqrtf(nx*nx+ny*ny+nz*nz);
        nrm[i3]=(uint8_t)((nx/l*0.5f+0.5f)*255);
        nrm[i3+1]=(uint8_t)((ny/l*0.5f+0.5f)*255);
        nrm[i3+2]=(uint8_t)((nz/l*0.5f+0.5f)*255);

        /* ROUGHNESS — grout rough, tiles polished-ish, plus fine variation */
        int i1=y*W+x;
        float r = grout?0.9f : (tile?0.35f:0.5f);
        r += 0.05f*sinf(u*60.0f)*sinf(v*60.0f);
        if(r<0)r=0; if(r>1)r=1;
        rgh[i1]=(uint8_t)(r*255);

        /* METALLIC — the blue tiles are metallic, rest dielectric */
        met[i1] = (!grout && !tile)? 230 : 10;

        /* AO — darken grout valleys */
        ao[i1] = grout? 110 : 245;
    }

    char p[512];
    snprintf(p,sizeof(p),"%s/tile_basecolor.png",dir); stbi_write_png(p,W,H,3,base,W*3);
    snprintf(p,sizeof(p),"%s/tile_normal.png",dir);    stbi_write_png(p,W,H,3,nrm,W*3);
    snprintf(p,sizeof(p),"%s/tile_roughness.png",dir); stbi_write_png(p,W,H,1,rgh,W);
    snprintf(p,sizeof(p),"%s/tile_metallic.png",dir);  stbi_write_png(p,W,H,1,met,W);
    snprintf(p,sizeof(p),"%s/tile_ao.png",dir);        stbi_write_png(p,W,H,1,ao,W);

    printf("wrote PBR set to %s (basecolor/normal/roughness/metallic/ao)\n", dir);
    free(base);free(nrm);free(rgh);free(met);free(ao);
    return 0;
}
