// Native 3D with the original game's live material identity. Large UV spans
// are clipped in model space, never projected on the CPU. No per-frame malloc.
extern "C" int d2_rdp_t3d_texture(int, int, int, int, int, int, int*, int*);
extern "C" void d2_rdp_invalidate_backend_cache(void);
static int s_live_vertex_cursor;
static unsigned s_live_faces, s_live_parts, s_live_missing;
struct D2LiveVertex { float x, y, z, u, v; };

static int d2_live_clip(const D2LiveVertex* in, int count, D2LiveVertex* out, int axis, float bound, bool keep_greater)
{
    int n = 0;
    for (int i = 0; i < count; i++) {
        const D2LiveVertex& a = in[i];
        const D2LiveVertex& b = in[(i + 1) % count];
        float av = axis ? a.v : a.u, bv = axis ? b.v : b.u;
        bool ia = keep_greater ? av >= bound : av <= bound;
        bool ib = keep_greater ? bv >= bound : bv <= bound;
        if (ia) out[n++] = a;
        if (ia != ib) {
            float f = (bound - av) / (bv - av);
            out[n++] = {a.x + (b.x-a.x)*f, a.y + (b.y-a.y)*f, a.z + (b.z-a.z)*f,
                        a.u + (b.u-a.u)*f, a.v + (b.v-a.v)*f};
        }
    }
    return n;
}

static void d2_live_emit(const D2LiveVertex* v, int count, uint32_t color, int ou, int ov)
{
    int aligned = (count + 1) & ~1;
    const int capacity = D2_T3D_MAX_VERTS / 2;
    if (s_live_vertex_cursor + aligned > capacity) {
        t3d_tri_sync();
        rspq_wait(); // Only the RSP reads these vertices; RDP gets inline triangles.
        s_live_vertex_cursor = 0;
    }
    int first = s_t3d_viewport._bufferIdx * capacity + s_live_vertex_cursor;
    for (int k = 0; k < aligned; k++) {
        const D2LiveVertex& p = v[k < count ? k : 0];
        T3DVertPacked& packed = s_t3d_vtx_pool[(first+k)/2];
        int16_t* pos = ((first+k)&1) ? packed.posB : packed.posA;
        int16_t* st = ((first+k)&1) ? packed.stB : packed.stA;
        pos[0] = (int16_t)lroundf(p.x); pos[1] = (int16_t)lroundf(p.y); pos[2] = (int16_t)lroundf(p.z);
        st[0] = (int16_t)lroundf((p.u-ou)*32.0f); st[1] = (int16_t)lroundf((p.v-ov)*32.0f);
        if ((first+k)&1) { packed.rgbaB=color; packed.normB=0; }
        else { packed.rgbaA=color; packed.normA=0; }
    }
    T3DVertPacked* data = &s_t3d_vtx_pool[first/2];
    data_cache_hit_writeback(data, aligned*16);
    t3d_vert_load(data, 0, aligned);
    for (int k=1; k+1<count; k++) t3d_tri_draw(0,k,k+1);
    t3d_tri_sync();
    s_live_vertex_cursor += aligned;
    s_live_parts++;
}

static void d2_live_triangle(const D2LiveVertex tri[3], uint32_t color, int page, int clut)
{
    float min_u=fminf(tri[0].u,fminf(tri[1].u,tri[2].u)), max_u=fmaxf(tri[0].u,fmaxf(tri[1].u,tri[2].u));
    float min_v=fminf(tri[0].v,fminf(tri[1].v,tri[2].v)), max_v=fmaxf(tri[0].v,fmaxf(tri[1].v,tri[2].v));
    int first_u=(int)min_u/64*64, first_v=(int)min_v/64*64;
    for (int tv=first_v; tv<=max_v && tv<256; tv+=64)
    for (int tu=first_u; tu<=max_u && tu<256; tu+=64) {
        D2LiveVertex a[12],b[12];
        for(int k=0;k<3;k++) a[k]=tri[k];
        int n=d2_live_clip(a,3,b,0,(float)tu,true);
        n=d2_live_clip(b,n,a,0,(float)(tu+64),false);
        n=d2_live_clip(a,n,b,1,(float)tv,true);
        n=d2_live_clip(b,n,a,1,(float)(tv+64),false);
        if(n<3) continue;
        float area=0;
        for(int k=1;k+1<n;k++) area+=fabsf((a[k].u-a[0].u)*(a[k+1].v-a[0].v)-(a[k].v-a[0].v)*(a[k+1].u-a[0].u));
        if(area<0.001f) continue;
        int ou,ov;
        bool bound=d2_rdp_t3d_texture(page,clut,tu,tv,64,64,&ou,&ov)!=0;
        if (!bound) { s_live_missing++; continue; }
        d2_live_emit(a,n,color,ou,ov);
    }
}

static void d2_t3d_draw_live_face(const D2QueuedInstance& inst, const unsigned char* p)
{
    MODEL* model=modelpointers[inst.model_idx];
    SVECTOR* verts=GET_MODEL_DATA(SVECTOR,model,vertices);
    bool is_tile=s_t3d_tile_ofs[inst.model_idx]!=0;
    rdpq_mode_combiner(RDPQ_COMBINER1((TEX0,ZERO,SHADE,ZERO),(ZERO,ZERO,ZERO,ONE)));
    t3d_state_set_drawflags((enum T3DDrawFlags)(T3D_FLAG_SHADED|T3D_FLAG_TEXTURED|T3D_FLAG_NO_LIGHT));
    {
        int type=p[0]&31, size=PolySizes[type];
        if(!size) return;
        int ids[4],tset,tid; bool triangle; UV_INFO uv[4];
        bool supported=extract_poly_info(p,type,&ids[0],&ids[1],&ids[2],&ids[3],&triangle,&tset,&tid,&uv[0],&uv[1],&uv[2],&uv[3]);
        if(!supported || tset>=128 || tid>=32) return;
        int page=texture_pages[tset],clut=texture_cluts[tset][tid];
        D2LiveVertex v[4]; int count=triangle?3:4;
        for(int k=0;k<count;k++) { const SVECTOR& pos=verts[ids[k]]; v[k]={(float)pos.vx,(float)pos.vy,(float)pos.vz,(float)uv[k].u,(float)uv[k].v}; }
        uint32_t color=0xFFFFFFFFu;
        if(is_tile) color=((uint32_t)s_tile_tint.r<<24)|((uint32_t)s_tile_tint.g<<16)|((uint32_t)s_tile_tint.b<<8)|255;
        // Preserve UV/position pairs in model perimeter order, not PS1 strip order.
        D2LiveVertex t0[3]={v[0],v[1],v[triangle?2:3]};
        d2_live_triangle(t0,color,page,clut);
        if(!triangle) { D2LiveVertex t1[3]={v[1],v[2],v[3]}; d2_live_triangle(t1,color,page,clut); }
        s_live_faces++;
    }
}

static void d2_t3d_draw_live_model(const D2QueuedInstance& inst)
{
    MODEL* model=modelpointers[inst.model_idx];
    const unsigned char* p=GET_MODEL_DATA(unsigned char,model,poly_block);
    for(int face=0;face<model->num_polys;face++) {
        int size=PolySizes[p[0]&31];
        if(!size) break;
        d2_t3d_draw_live_face(inst,p);
        p+=size;
    }
}
