// Merge native world faces into the PS1 painter's ordering table. The RSP still
// transforms/clips vertices; CPU depth keys retain the original GTE OT scale.
struct D2OrderedFace {
    const unsigned char* poly;
    unsigned short instance, bucket;
    unsigned int sequence;
};
static D2OrderedFace s_order_faces[4096];
static const D2QueuedInstance* s_order_instances[D2_T3D_MAX_MATS];
static int s_order_count, s_order_cursor, s_order_instance_count;
static bool s_order_active;

static int d2_order_compare(const void* left, const void* right)
{
    const D2OrderedFace& a=*(const D2OrderedFace*)left;
    const D2OrderedFace& b=*(const D2OrderedFace*)right;
    if(a.bucket!=b.bucket) return (int)b.bucket-(int)a.bucket;
    return a.sequence<b.sequence ? 1 : a.sequence>b.sequence ? -1 : 0;
}

static void d2_order_add_instance(const D2QueuedInstance& inst)
{
    if(s_order_instance_count>=D2_T3D_MAX_MATS) return;
    int index=s_order_instance_count++;
    s_order_instances[index]=&inst;
    build_instance_matrix(inst,&s_t3d_mat_pool[s_t3d_viewport._bufferIdx][index],false);
    MODEL* model=modelpointers[inst.model_idx];
    SVECTOR* vertices=GET_MODEL_DATA(SVECTOR,model,vertices);
    const unsigned char* p=GET_MODEL_DATA(unsigned char,model,poly_block);
    MATRIX2* matrix=&CompoundMatrix[inst.yang];
    bool tile=s_t3d_tile_ofs[inst.model_idx]!=0;
    unsigned int tile_types=tile ? d2_rd32(model+1)>>2 : 0;
    int tile_type=0;
    const int offsets[8]={s_t3d_tile_ofs[inst.model_idx],s_t3d_tile_ofs[inst.model_idx],0,75,133,
                         s_t3d_tile_ofs[inst.model_idx],s_t3d_tile_ofs[inst.model_idx],s_t3d_tile_ofs[inst.model_idx]};
    for(int face=0;face<model->num_polys;face++) {
        int type=p[0]&31,size=PolySizes[type];
        if(!size) break;
        int face_type=tile_type;
        if(tile) { tile_type=tile_types&7; tile_types>>=3; }
        int ids[4],tset,tid; bool triangle; UV_INFO uv[4];
        bool valid=extract_poly_info(p,type,&ids[0],&ids[1],&ids[2],&ids[3],&triangle,&tset,&tid,&uv[0],&uv[1],&uv[2],&uv[3]);
        const unsigned char* poly=p;p+=size;
        if(!valid) continue;
        int corners=triangle?3:4;
        double camera[4][3]; int depth_sum=0;
        for(int k=0;k<corners;k++) {
            const SVECTOR& v=vertices[ids[k]];
            for(int row=0;row<3;row++) {
                long long fixed=(long long)matrix->m[row][0]*v.vx+(long long)matrix->m[row][1]*v.vy+(long long)matrix->m[row][2]*v.vz;
                int translation=row==0?inst.tx:row==1?inst.ty:inst.tz;
                camera[k][row]=(double)(fixed>>12)+translation;
            }
            int z=(int)camera[k][2];
            depth_sum+=z<0?0:z>65535?65535:z;
        }
        int third=triangle?2:3;
        // Same front-face sign as GTE NCLIP when all three points are in front.
        if(camera[0][2]>16 && camera[1][2]>16 && camera[third][2]>16) {
            const double* a=camera[0];const double* b=camera[1];const double* c=camera[third];
            double area=a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0]);
            if(area<=0) continue;
        }
        int depth=triangle ? (int)(((long long)depth_sum*341)>>13) : depth_sum>>5;
        int bias=(int)model->zBias-64;
        if(bias<0) bias=0;
        depth+=tile ? offsets[face_type&7] : 8+bias*4;
        if(depth<0) depth=0;if(depth>=0x1070) depth=0x106f;
        if(s_order_count>=4096) {
            debugf("[T3D_ORDER] face limit exceeded\n");
            break;
        }
        s_order_faces[s_order_count] = {poly,(unsigned short)index,(unsigned short)depth,(unsigned int)s_order_count};
        s_order_count++;
    }
}

int d2_t3d_begin_ordered_world(void)
{
    s_order_active=false;
#if !D2_T3D_LIVE_TEXTURES
    return 0;
#else
    // Never touch frontend state when its OT contains no native world.
    if(!s_t3d_tile_queue_count && !s_t3d_bld_queue_count) return 1;
#ifdef D2_T3D_TELEMETRY
    s_live_ot_start=d2_live_clock();
#endif
    float h=scr_z>0?(float)scr_z:256.0f;
    T3DVec3 eye={{0,0,0}},target={{0,0,256}},up={{0,-1,0}};
    t3d_viewport_set_projection(&s_t3d_viewport,2.0f*atanf(120.0f/h),16.0f,32768.0f);
    t3d_viewport_look_at(&s_t3d_viewport,&eye,&target,&up);
    t3d_viewport_attach(&s_t3d_viewport);
    s_live_vertex_cursor=0;s_live_faces=s_live_parts=s_live_missing=0;
    s_order_count=s_order_cursor=s_order_instance_count=0;
    for(int i=0;i<s_t3d_tile_queue_count;i++) d2_order_add_instance(s_t3d_tile_queue[i]);
    for(int i=0;i<s_t3d_bld_queue_count;i++) d2_order_add_instance(s_t3d_bld_queue[i]);
#ifdef D2_T3D_TELEMETRY
    uint32_t sort_start=d2_live_clock();
#endif
    qsort(s_order_faces,s_order_count,sizeof(s_order_faces[0]),d2_order_compare);
#ifdef D2_T3D_TELEMETRY
    s_live_telemetry.sort+=d2_live_clock()-sort_start;
    s_live_telemetry.prepare+=d2_live_clock()-s_live_ot_start;
    s_live_telemetry.faces+=s_order_count;
#endif
    s_order_active=true;
    return 1;
#endif
}

void d2_t3d_draw_world_until(int bucket)
{
    if(!s_order_active || s_order_cursor>=s_order_count || s_order_faces[s_order_cursor].bucket<bucket) return;
#ifdef D2_T3D_TELEMETRY
    uint32_t render_start=d2_live_clock();
    s_live_telemetry.batches++;
#endif
    // The legacy backend may have changed modes/TMEM since the previous batch.
    d2_rdp_invalidate_backend_cache();
    rdpq_mode_begin();
    rdpq_set_mode_standard();
    rdpq_mode_persp(true);
    rdpq_mode_filter(FILTER_POINT);
    rdpq_mode_zbuf(false,false);
    rdpq_mode_end();
    t3d_matrix_push_pos(1);
    int last_instance=-1;
    while(s_order_cursor<s_order_count && s_order_faces[s_order_cursor].bucket>=bucket) {
        const D2OrderedFace& face=s_order_faces[s_order_cursor++];
        if(last_instance!=face.instance) {
            t3d_matrix_set(&s_t3d_mat_pool[s_t3d_viewport._bufferIdx][face.instance],true);
            last_instance=face.instance;
#ifdef D2_T3D_TELEMETRY
            s_live_telemetry.matrices++;
#endif
        }
        d2_t3d_draw_live_face(*s_order_instances[face.instance],face.poly);
    }
    t3d_matrix_pop(1);
    d2_rdp_invalidate_backend_cache();
#ifdef D2_T3D_TELEMETRY
    s_live_telemetry.render+=d2_live_clock()-render_start;
#endif
}

void d2_t3d_end_ordered_world(void)
{
    d2_t3d_draw_world_until(0);
#ifdef D2_T3D_TELEMETRY
    if(s_order_active) {
        s_live_telemetry.ot_span+=d2_live_clock()-s_live_ot_start;
        s_live_telemetry.missing+=s_live_missing;
        if(++s_live_telemetry.frames==60) {
            const D2LiveTelemetry& t=s_live_telemetry;
            extern int CameraCnt;
            debugf("[T3D_PERF] cc=%d nf=%u prep=%u sort=%u render=%u bind=%u emit=%u ring_wait=%u ot_span=%u faces=%u vtx=%u tris=%u batches=%u matrices=%u rings=%u missing=%u\n",
                CameraCnt,t.frames,t.prepare/60,t.sort/60,t.render/60,t.bind/60,t.emit/60,t.ring_wait/60,t.ot_span/60,
                t.faces/60,t.vertices/60,t.triangles/60,t.batches/60,t.matrices/60,t.rings,t.missing);
            memset(&s_live_telemetry,0,sizeof(s_live_telemetry));
        }
    }
#endif
    s_order_active=false;
    s_t3d_tile_queue_count=s_t3d_bld_queue_count=0;
}
