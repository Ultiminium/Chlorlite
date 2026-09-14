/* debug_image.c — PNG diff + regression snapshots (own stb impl unit). */
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "cc/debug.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

double cc_screenshot_diff(const char* ap, const char* bp, const char* out){
    int aw,ah,an,bw,bh,bn;
    unsigned char* a=stbi_load(ap,&aw,&ah,&an,3);
    unsigned char* b=stbi_load(bp,&bw,&bh,&bn,3);
    if(!a||!b||aw!=bw||ah!=bh){ if(a)stbi_image_free(a); if(b)stbi_image_free(b); return -1.0; }
    long n=(long)aw*ah, changed=0;
    unsigned char* hl = out? malloc(n*3):NULL;
    for(long i=0;i<n;i++){
        int dr=abs(a[i*3]-b[i*3]), dg=abs(a[i*3+1]-b[i*3+1]), db=abs(a[i*3+2]-b[i*3+2]);
        int diff=(dr+dg+db)>24;
        if(diff)changed++;
        if(hl){ if(diff){hl[i*3]=255;hl[i*3+1]=0;hl[i*3+2]=255;}
                else {hl[i*3]=b[i*3]/3;hl[i*3+1]=b[i*3+1]/3;hl[i*3+2]=b[i*3+2]/3;} }
    }
    if(hl){ stbi_write_png(out,aw,ah,3,hl,aw*3); free(hl); }
    stbi_image_free(a); stbi_image_free(b);
    return 100.0*changed/n;
}

/* golden compare: if golden missing, screenshot becomes golden (new). else compare. */
int cc_regression_check(CCEngine* e, const char* golden, double tol){
    char cur[600]; snprintf(cur,sizeof(cur),"%s.current.png",golden);
    cc_screenshot(e,cur);
    FILE* g=fopen(golden,"rb");
    if(!g){ /* no golden: adopt current as golden */
        FILE* in=fopen(cur,"rb"); FILE* out=fopen(golden,"wb");
        if(in&&out){ char b[8192]; size_t r; while((r=fread(b,1,sizeof(b),in))>0)fwrite(b,1,r,out); }
        if(in)fclose(in); if(out)fclose(out); remove(cur);
        fprintf(stderr,"[cc:regression] golden created: %s\n",golden); return 0;
    }
    fclose(g);
    char hl[600]; snprintf(hl,sizeof(hl),"%s.diff.png",golden);
    double pct=cc_screenshot_diff(golden,cur,hl);
    remove(cur);
    if(pct<0){ fprintf(stderr,"[cc:regression] size mismatch vs golden\n"); return 1; }
    if(pct>tol){ fprintf(stderr,"[cc:regression] FAIL %.2f%% changed (tol %.2f%%) → %s\n",pct,tol,hl); return 1; }
    remove(hl);
    fprintf(stderr,"[cc:regression] pass (%.3f%% <= %.2f%%)\n",pct,tol); return 0;
}
