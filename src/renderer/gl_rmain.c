/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// r_main.c

#include "quakedef.h"

#include "glquake.h"
#include "gl_local.h"
#ifdef __SSE__
#include <xmmintrin.h>
#endif
#include <cglm/cglm.h>

entity_t r_worldentity;

qboolean r_cache_thrash;  // compatability

vec3_t modelorg, r_entorigin;
entity_t *currententity;

int r_visframecount;  // bumped when going to a new PVS
int r_framecount;     // used for dlight push checking

mplane_t frustum[4];

int c_brush_polys, c_alias_polys;

qboolean envmap;  // true during envmap command capture

int currenttexture = -1;  // to avoid unnecessary texture sets

int cnttextures[2] = {-1, -1};  // cached

int particletexture;  // little dot for particles
int playertextures;   // up to 16 color translated skins

int mirrortexturenum;  // quake texturenum, not gltexturenum
qboolean mirror;
mplane_t *mirror_plane;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_world_matrix[16];
float r_base_world_matrix[16];

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

texture_t *r_notexture_mip;

int d_lightstylevalue[256];  // 8.8 fraction of base light value

void R_MarkLeaves(void);

cvar_t r_norefresh = {"r_norefresh", "0"};
cvar_t r_drawentities = {"r_drawentities", "1"};
cvar_t r_drawviewmodel = {"r_drawviewmodel", "1"};
cvar_t r_speeds = {"r_speeds", "0"};
cvar_t r_fullbright = {"r_fullbright", "0"};
cvar_t r_lightmap = {"r_lightmap", "0"};
cvar_t r_shadows = {"r_shadows", "0"};
cvar_t r_mirroralpha = {"r_mirroralpha", "1"};
cvar_t r_wateralpha = {"r_wateralpha", "1"};
cvar_t r_dynamic = {"r_dynamic", "1"};
cvar_t r_novis = {"r_novis", "0"};
cvar_t r_interpolate_anim = {"r_interpolate_anim", "1", true};  // fenix@io.com: model interpolation
cvar_t r_interpolate_pos = {"r_interpolate_pos", "1", true};    // " "

cvar_t gl_finish = {"gl_finish", "0"};
cvar_t gl_clear = {"gl_clear", "0"};
cvar_t gl_cull = {"gl_cull", "1"};
cvar_t gl_texsort = {"gl_texsort", "1"};
cvar_t gl_smoothmodels = {"gl_smoothmodels", "1"};
cvar_t gl_affinemodels = {"gl_affinemodels", "1"};
cvar_t gl_polyblend = {"gl_polyblend", "1"};
cvar_t gl_flashblend = {"gl_flashblend", "1"};
cvar_t gl_playermip = {"gl_playermip", "0"};
cvar_t gl_nocolors = {"gl_nocolors", "0"};
cvar_t gl_keeptjunctions = {"gl_keeptjunctions", "0"};
cvar_t gl_reporttjunctions = {"gl_reporttjunctions", "0"};
cvar_t gl_doubleeyes = {"gl_doubleeys", "1"};
cvar_t r_sky = {"r_sky", "1"};

extern cvar_t gl_ztrick;

static int _backup_min_filter;
static int _backup_max_filter;

void R_DrawSkySphere(void);
void R_DrawParticles(void);
void R_RenderBrushPoly(msurface_t *fa);
static mat4 _model_matrix __attribute__((aligned(32))) =
    {
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
};
/*
=================
R_CullBox

Returns true if the box is completely outside the frustom
=================
*/
qboolean R_CullBox(const vec3_t mins, const vec3_t maxs) {
  int i;

  for (i = 0; i < 4; i++)
    if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2)
      return true;
  return false;
}

void R_RotateForEntity(entity_t *e) {
  glGetFloatv(GL_MODELVIEW_MATRIX, (float *)_model_matrix);

  glm_translate(_model_matrix, (float *)e->origin);

  glm_rotate_z(_model_matrix, DEG2RAD(e->angles[1]), _model_matrix);
  glm_rotate_y(_model_matrix, DEG2RAD(-e->angles[0]), _model_matrix);
  glm_rotate_x(_model_matrix, DEG2RAD(e->angles[2]), _model_matrix);

  glLoadMatrixf((float *)_model_matrix);
}

/*
=============
R_BlendedRotateForEntity

fenix@io.com: model transform interpolation
=============
*/
void R_BlendedRotateForEntity(entity_t *e) {
  float timepassed;
  float blend;
  vec3_t d;
  int i;

  glGetFloatv(GL_MODELVIEW_MATRIX, (float *)_model_matrix);

  // positional interpolation

  timepassed = realtime - e->translate_start_time;

  if (e->translate_start_time == 0 || timepassed > 1) {
    e->translate_start_time = realtime;
    VectorCopy(e->origin, e->origin1);
    VectorCopy(e->origin, e->origin2);
  }

  if (!VectorCompare(e->origin, e->origin2)) {
    e->translate_start_time = realtime;
    VectorCopy(e->origin2, e->origin1);
    VectorCopy(e->origin, e->origin2);
    blend = 0;
  } else {
    blend = timepassed * 10.0f;

    if (cl.paused || blend > 1) blend = 1;
  }

  VectorSubtract(e->origin2, e->origin1, d);

  glm_translate(_model_matrix, (vec3){e->origin1[0] + (blend * d[0]), e->origin1[1] + (blend * d[1]), e->origin1[2] + (blend * d[2])});

  // orientation interpolation (Euler angles, yuck!)
  timepassed = realtime - e->rotate_start_time;

  if (e->rotate_start_time == 0 || timepassed > 1) {
    e->rotate_start_time = realtime;
    VectorCopy(e->angles, e->angles1);
    VectorCopy(e->angles, e->angles2);
  }

  if (!VectorCompare(e->angles, e->angles2)) {
    e->rotate_start_time = realtime;
    VectorCopy(e->angles2, e->angles1);
    VectorCopy(e->angles, e->angles2);
    blend = 0;
  } else {
    blend = timepassed * 10.0f;

    if (cl.paused || blend > 1) blend = 1;
  }

  VectorSubtract(e->angles2, e->angles1, d);

  // always interpolate along the shortest path
  for (i = 0; i < 3; i++) {
    if (d[i] > 180) {
      d[i] -= 360;
    } else if (d[i] < -180) {
      d[i] += 360;
    }
  }

  glm_rotate_z(_model_matrix, DEG2RAD(e->angles1[1] + (blend * d[1])), _model_matrix);
  glm_rotate_y(_model_matrix, DEG2RAD(-e->angles1[0] + (-blend * d[0])), _model_matrix);
  glm_rotate_x(_model_matrix, DEG2RAD(e->angles1[2] + (blend * d[2])), _model_matrix);

  glLoadMatrixf((float *)_model_matrix);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame(entity_t *currententity) {
  msprite_t *psprite;
  mspritegroup_t *pspritegroup;
  mspriteframe_t *pspriteframe;
  int i, numframes, frame;
  float *pintervals, fullinterval, targettime, time;

  psprite = currententity->model->cache.data;
  frame = currententity->frame;

  if ((frame >= psprite->numframes) || (frame < 0)) {
    Con_DPrintf("R_DrawSprite: no such frame %d\n", frame);
    frame = 0;
  }

  if (psprite->frames[frame].type == SPR_SINGLE) {
    pspriteframe = psprite->frames[frame].frameptr;
  } else {
    pspritegroup = (mspritegroup_t *)psprite->frames[frame].frameptr;
    pintervals = pspritegroup->intervals;
    numframes = pspritegroup->numframes;
    fullinterval = pintervals[numframes - 1];

    time = cl.time + currententity->syncbase;

    // when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
    // are positive, so we don't have to worry about division by 0
    targettime = time - ((int)(time / fullinterval)) * fullinterval;

    for (i = 0; i < (numframes - 1); i++) {
      if (pintervals[i] > targettime)
        break;
    }

    pspriteframe = pspritegroup->frames[i];
  }

  return pspriteframe;
}

/*
=================
R_DrawSpriteModel

=================
*/
//Atomizer - New math function

#define VectorScalarMult(a, b, c) \
  {                               \
    c[0] = a[0] * b;              \
    c[1] = a[1] * b;              \
    c[2] = a[2] * b;              \
  }

void R_DrawSpriteModel(entity_t *e) {
  vec3_t point;
  mspriteframe_t *frame;
  float *up, *right;
  vec3_t v_forward, v_right, v_up;
  msprite_t *psprite;

  // don't even bother culling, because it's just a single
  // polygon without a surface cache
  frame = R_GetSpriteFrame(e);
  psprite = currententity->model->cache.data;

  if (psprite->type == SPR_ORIENTED) {  // bullet marks on walls
    AngleVectors(currententity->angles, v_forward, v_right, v_up);
    up = v_up;
    right = v_right;
  } else {  // normal sprite
    up = vup;
    right = vright;
  }

  //glColor3f (1,1,1);

  GL_DisableMultitexture();

  GL_Bind(frame->gl_texturenum);

  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  //@Note: Check but i think this is better
  //glEnable (GL_ALPHA_TEST);
  glEnable(GL_BLEND);
  glEnableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  point[0] = (e->origin[0] + frame->down * up[0]) + frame->right * right[0];
  point[1] = (e->origin[1] + frame->down * up[1]) + frame->right * right[1];
  point[2] = (e->origin[2] + frame->down * up[2]) + frame->right * right[2];
  r_batchedtempverts[0] = (glvert_fast_t){.flags = VERTEX, .vert = {point[0], point[1], point[2]}, .texture = {1, 1}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  point[0] = (e->origin[0] + frame->down * up[0]) + frame->left * right[0];
  point[1] = (e->origin[1] + frame->down * up[1]) + frame->left * right[1];
  point[2] = (e->origin[2] + frame->down * up[2]) + frame->left * right[2];
  r_batchedtempverts[1] = (glvert_fast_t){.flags = VERTEX, .vert = {point[0], point[1], point[2]}, .texture = {0, 1}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  point[0] = (e->origin[0] + frame->up * up[0]) + frame->right * right[0];
  point[1] = (e->origin[1] + frame->up * up[1]) + frame->right * right[1];
  point[2] = (e->origin[2] + frame->up * up[2]) + frame->right * right[2];
  r_batchedtempverts[2] = (glvert_fast_t){.flags = VERTEX, .vert = {point[0], point[1], point[2]}, .texture = {1, 0}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  point[0] = (e->origin[0] + frame->up * up[0]) + frame->left * right[0];
  point[1] = (e->origin[1] + frame->up * up[1]) + frame->left * right[1];
  point[2] = (e->origin[2] + frame->up * up[2]) + frame->left * right[2];
  r_batchedtempverts[3] = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {point[0], point[1], point[2]}, .texture = {0, 0}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedtempverts[0].vert);
  glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedtempverts[0].texture);
  glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &r_batchedtempverts[0].color);

  glDepthMask(GL_FALSE);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDepthMask(GL_TRUE);
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS 162

float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

vec3_t shadevector;
float shadelight, ambientlight;

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
float r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
    ;

float *shadedots = r_avertexnormal_dots[0];

int lastposenum0;
int lastposenum;

/*
=============
GL_DrawAliasFrame
=============
*/
void GL_DrawAliasFrame(aliashdr_t *paliashdr, int posenum) {
  unsigned char l;
  trivertx_t *verts;
  int *order;
  int count;

  glEnableClientState(GL_COLOR_ARRAY);

  lastposenum = posenum;
  verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
  verts += posenum * paliashdr->poseverts;
  order = (int *)((byte *)paliashdr + paliashdr->commands);

  while (1) {
    // get the vertex count and primitive type
    count = *order++;
    if (!count)
      break;  // done

    int c;
    int i = 0;

    glvert_fast_t *submission_pointer = &r_batchedtempverts[0];

#ifdef GL_EXT_dreamcast_direct_buffer
    glEnable(GL_DIRECT_BUFFER_KOS);
    glDirectBufferReserve_INTERNAL_KOS(count, (int *)&submission_pointer, GL_TRIANGLE_STRIP);
#endif

    glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->vert);
    glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->texture);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &submission_pointer->color);

    c = count;
    do {
      l = ambientlight < 0 ? 255 : (unsigned char)((shadedots[verts->lightnormalindex] * shadelight) * 255);
      *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {verts->v[0], verts->v[1], verts->v[2]}, .texture = {((float *)order)[0], ((float *)order)[1]}, .color = {l, l, l, 255}, .pad0 = {0}};
      order += 2;
      i++;
      verts++;
    } while (--c);
    (*(submission_pointer - 1)).flags = VERTEX_EOL;

    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);

#ifdef GL_EXT_dreamcast_direct_buffer
    glDisable(GL_DIRECT_BUFFER_KOS);
#endif
    #ifdef PARANOID
    if (count > 32)
      printf("%s:%d drew: %d\n", __FILE__, __LINE__, count);
    #endif
  }

  glDisableClientState(GL_COLOR_ARRAY);
}

/*
 =============
 GL_DrawAliasBlendedFrame

 fenix@io.com: model animation interpolation
 haydenkow: vertex arrays
 =============
 */
void GL_DrawAliasBlendedFrame(aliashdr_t *paliashdr, int pose1, int pose2, float blend) {
  unsigned char l;
  trivertx_t *verts1;
  trivertx_t *verts2;
  int *order;
  int count;
  vec3_t d;

  lastposenum0 = pose1;
  lastposenum = pose2;

  verts1 = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
  verts2 = verts1;

  verts1 += pose1 * paliashdr->poseverts;
  verts2 += pose2 * paliashdr->poseverts;

  order = (int *)((byte *)paliashdr + paliashdr->commands);

  glEnableClientState(GL_COLOR_ARRAY);

  while (1) {
    // get the vertex count and primitive type
    count = *order++;
    if (!count)
      break;  // done

    int c;

    glvert_fast_t *submission_pointer = &r_batchedtempverts[0];

#ifdef GL_EXT_dreamcast_direct_buffer
    glEnable(GL_DIRECT_BUFFER_KOS);
    glDirectBufferReserve_INTERNAL_KOS(count, (int *)&submission_pointer, GL_TRIANGLE_STRIP);
#endif

    glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->vert);
    glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->texture);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &submission_pointer->color);

    c = count;
    do {
      /* Interpolate lighting */
      d[0] = shadedots[verts2->lightnormalindex] - shadedots[verts1->lightnormalindex];
      l = ambientlight < 0 ? 255 : (unsigned char)(((shadedots[verts1->lightnormalindex] + (blend * d[0])) * shadelight) * 255);
      VectorSubtract(verts2->v, verts1->v, d);

      *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {verts1->v[0] + (blend * d[0]), verts1->v[1] + (blend * d[1]), verts1->v[2] + (blend * d[2])}, .texture = {((float *)order)[0], ((float *)order)[1]}, .color = {l, l, l, 255}, .pad0 = {0}};
      order += 2;
      verts1++;
      verts2++;
    } while (--c);
    (*(submission_pointer - 1)).flags = VERTEX_EOL;

    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);

#ifdef GL_EXT_dreamcast_direct_buffer
    glDisable(GL_DIRECT_BUFFER_KOS);
#endif
    #ifdef PARANOID
    if (count > 32)
      printf("%s:%d drew: %d\n", __FILE__, __LINE__, count);
    #endif
  }

  glDisableClientState(GL_COLOR_ARRAY);
}

/*
=============
GL_DrawAliasShadow
=============
*/
extern vec3_t lightspot;

void GL_DrawAliasShadow(aliashdr_t *paliashdr, int posenum) {
  trivertx_t *verts;
  int *order;
  float height, lheight;
  int count;

  lheight = currententity->origin[2] - lightspot[2];

  height = 0;
  verts = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
  verts += posenum * paliashdr->poseverts;
  order = (int *)((byte *)paliashdr + paliashdr->commands);

  height = -lheight + 1.0;
  glEnableClientState(GL_COLOR_ARRAY);
  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);

  int c;

  while (1) {
    count = *order++;
    if (!count)
      break;

    glvert_fast_t *submission_pointer = &r_batchedtempverts[0];

#ifdef GL_EXT_dreamcast_direct_buffer
    glEnable(GL_DIRECT_BUFFER_KOS);
    glDirectBufferReserve_INTERNAL_KOS(count, (int *)&submission_pointer, GL_TRIANGLE_STRIP);
#endif

    glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->vert);
    glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->texture);
    glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &submission_pointer->color);

    for (c = 0; c < count; c++) {
      height -= 0.001f;
      const float x = verts->v[0] * paliashdr->scale[0] + paliashdr->scale_origin[0];
      const float y = verts->v[1] * paliashdr->scale[1] + paliashdr->scale_origin[1];
      const float z = verts->v[2] * paliashdr->scale[2] + paliashdr->scale_origin[2];
      *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {x - shadevector[0] * (z + lheight), y - shadevector[1] * (z + lheight), height}, .texture = {0, 0}, .color = {0, 0, 0, 128}, .pad0 = {0}};
      order += 2;
      verts++;
    }

    (*(submission_pointer - 1)).flags = VERTEX_EOL;
    glDrawArrays(GL_TRIANGLE_STRIP, 0, count);
  }

  #ifdef PARANOID
  if (count > 192)
    printf("%s:%d drew: %d\n", __FILE__, __LINE__, count);
  #endif

#ifdef GL_EXT_dreamcast_direct_buffer
  glDisable(GL_DIRECT_BUFFER_KOS);
#endif

  glEnable(GL_CULL_FACE);
  glEnable(GL_TEXTURE_2D);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
}

/*
=================
R_SetupAliasFrame

=================
*/
void R_SetupAliasFrame(int frame, aliashdr_t *paliashdr) {
  int pose, numposes;
  float interval;

  if ((frame >= paliashdr->numframes) || (frame < 0)) {
    Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
    frame = 0;
  }

  pose = paliashdr->frames[frame].firstpose;
  numposes = paliashdr->frames[frame].numposes;

  if (numposes > 1) {
    interval = paliashdr->frames[frame].interval;
    pose += (int)(cl.time / interval) % numposes;
  }

  GL_DrawAliasFrame(paliashdr, pose);
}

/*
 =================
 R_SetupAliasBlendedFrame

 fenix@io.com: model animation interpolation
 =================
 */
void R_SetupAliasBlendedFrame(int frame, aliashdr_t *paliashdr, entity_t *e) {
  int pose;
  int numposes;
  float blend;

  if ((frame >= paliashdr->numframes) || (frame < 0)) {
    Con_DPrintf("R_AliasSetupFrame: no such frame %d\n", frame);
    frame = 0;
  }

  pose = paliashdr->frames[frame].firstpose;
  numposes = paliashdr->frames[frame].numposes;

  if (numposes > 1) {
    e->frame_interval = paliashdr->frames[frame].interval;
    pose += (int)(cl.time / e->frame_interval) % numposes;
  } else {
    /* One tenth of a second is a good for most Quake animations.
               If the nextthink is longer then the animation is usually meant to pause
               (e.g. check out the shambler magic animation in shambler.qc).  If its
               shorter then things will still be smoothed partly, and the jumps will be
               less noticable because of the shorter time.  So, this is probably a good
               assumption. */
    e->frame_interval = 0.1;
  }

  if (e->pose2 != pose) {
    e->frame_start_time = realtime;
    e->pose1 = e->pose2;
    e->pose2 = pose;
    blend = 0;
  } else {
    blend = (realtime - e->frame_start_time) / e->frame_interval;
  }

  // wierd things start happening if blend passes 1
  if (cl.paused || blend > 1) blend = 1;

  GL_DrawAliasBlendedFrame(paliashdr, e->pose1, e->pose2, blend);
}

/*
=================
R_DrawAliasModel

=================
*/
void R_DrawAliasModel(entity_t *e) {
  int i;
  int lnum;
  vec3_t dist;
  float add;
  model_t *clmodel;
  vec3_t mins, maxs;
  aliashdr_t *paliashdr;
  int anim;

  clmodel = currententity->model;

  VectorAdd(currententity->origin, clmodel->mins, mins);
  VectorAdd(currententity->origin, clmodel->maxs, maxs);

  if (R_CullBox(mins, maxs))
    return;

  VectorCopy(currententity->origin, r_entorigin);
  VectorSubtract(r_origin, r_entorigin, modelorg);

  //
  // get lighting information
  //

  ambientlight = shadelight = R_LightPoint(currententity->origin);

  // allways give the gun some light
  if (e == &cl.viewent && ambientlight < 24)
    ambientlight = shadelight = 24;

  for (lnum = 0; lnum < MAX_DLIGHTS; lnum++) {
    if (cl_dlights[lnum].die >= cl.time) {
      VectorSubtract(currententity->origin,
                     cl_dlights[lnum].origin,
                     dist);
      add = cl_dlights[lnum].radius - Length(dist);

      if (add > 0) {
        ambientlight += add;
        //ZOID models should be affected by dlights as well
        shadelight += add;
      }
    }
  }

  // clamp lighting so it doesn't overbright as much
  if (ambientlight > 128)
    ambientlight = 128;
  if (ambientlight + shadelight > 192)
    shadelight = 192 - ambientlight;

  // ZOID: never allow players to go totally black
  i = currententity - cl_entities;
  if (i >= 1 && i <= cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
    if (ambientlight < 8)
      ambientlight = shadelight = 8;

  // HACK HACK HACK -- no fullbright colors, so make torches full light
  if (!strcmp(clmodel->name, "progs/flame2.mdl") || !strcmp(clmodel->name, "progs/flame.mdl"))
    ambientlight = shadelight = -1;

  shadedots = r_avertexnormal_dots[((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1)];
  shadelight = shadelight / 200.0;

#ifdef _arch_dreamcast
  fsincos(-e->angles[1], &shadevector[1], &shadevector[0]);
#else
  //float an = DEG2RAD(e->angles[1]);
  shadevector[0] = COS(-DEG2RAD(e->angles[1]));
  shadevector[1] = SIN(-DEG2RAD(e->angles[1]));
#endif
  shadevector[2] = 1;
  VectorNormalize(shadevector);

  //
  // locate the proper data
  //
  paliashdr = (aliashdr_t *)Mod_Extradata(currententity->model);

  c_alias_polys += paliashdr->numtris;

  //
  // draw all the triangles
  //

  GL_DisableMultitexture();

  glPushMatrix();
  /* Old */
  // fenix@io.com: model transform interpolation
  if (r_interpolate_pos.value) {
    R_BlendedRotateForEntity(e);
  } else {
    R_RotateForEntity(e);
  }

  if (!strcmp(clmodel->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
    glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2] - (22 + 8));
    // float size of eyes, since they are really hard to see in gl
    glScalef(paliashdr->scale[0] * 2, paliashdr->scale[1] * 2, paliashdr->scale[2] * 2);
  } else {
    glTranslatef(paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
    glScalef(paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);
  }

  anim = (int)(cl.time * 10) & 3;
  GL_Bind(paliashdr->gl_texturenum[currententity->skinnum][anim]);

  // we can't dynamically colormap textures, so they are cached
  // seperately for the players.  Heads are just uncolored.
  if (currententity->colormap != vid.colormap && !gl_nocolors.value) {
    i = currententity - cl_entities;
    if (i >= 1 && i <= cl.maxclients /* && !strcmp (currententity->model->name, "progs/player.mdl") */)
      GL_Bind(playertextures - 1 + i);
  }
  
  /* Entities are filtered linear becuase mipamps arent gauranteed*/
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  if (gl_smoothmodels.value)
    glShadeModel(GL_SMOOTH);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  if (gl_affinemodels.value)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

  glEnable(GL_CULL_FACE);
  glCullFace(GL_FRONT);
  /* Old */
  // fenix@io.com: model animation interpolation
  if (r_interpolate_anim.value) {
    R_SetupAliasBlendedFrame(currententity->frame, paliashdr, currententity);
  } else {
    R_SetupAliasFrame(currententity->frame, paliashdr);
  }

  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, _backup_min_filter);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, _backup_max_filter);

  if (gl_affinemodels.value)
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

  glPopMatrix();

  if (r_shadows.value) {
    glPushMatrix();
    R_RotateForEntity(e);
    glDisable(GL_TEXTURE_2D);
    GL_DrawAliasShadow(paliashdr, lastposenum);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glPopMatrix();
  }
}

//==================================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList(void) {
  int i;

  if (!r_drawentities.value)
    return;

  extern int gl_filter_min;
  extern int gl_filter_max;

  _backup_min_filter = gl_filter_min;
  _backup_max_filter = gl_filter_max;

  // draw sprites seperately, because of alpha blending
  for (i = 0; i < cl_numvisedicts; i++) {
    currententity = cl_visedicts[i];

    switch (currententity->model->type) {
      case mod_alias:
        //@Note: Controls drawing of ALL Living entities
        R_DrawAliasModel(currententity);
        break;

      case mod_brush:
        //@Note: Controls drawing of level entities (doors, panels)
        R_DrawBrushModel(currententity);
        break;

      default:
        break;
    }
  }

  for (i = 0; i < cl_numvisedicts; i++) {
    currententity = cl_visedicts[i];

    if (currententity->model->type == mod_sprite) {
      /*switch (currententity->model->type)
		{
		case mod_sprite:*/
      R_DrawSpriteModel(currententity);
      //break;
    }
  }
}
/*
=============
R_DrawViewModel
=============
*/
void R_DrawViewModel (void)
{
	float		ambient[4], diffuse[4];
	int			j;
	int			lnum;
	vec3_t		dist;
	float		add;
	dlight_t	*dl;
	int			ambientlight, shadelight;

	if (!r_drawviewmodel.value)
		return;

	if (!r_drawentities.value)
		return;

	if (cl.items & IT_INVISIBILITY)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;
  
	j = R_LightPoint (currententity->origin);

	if (j < 24)
		j = 24;		// allways give some light on gun
	ambientlight = j;
	shadelight = j;

// add dynamic lights		
	for (lnum=0 ; lnum<MAX_DLIGHTS ; lnum++)
	{
		dl = &cl_dlights[lnum];
		if (!dl->radius)
			continue;
		if (!dl->radius)
			continue;
		if (dl->die < cl.time)
			continue;

		VectorSubtract (currententity->origin, dl->origin, dist);
		add = dl->radius - Length(dist);
		if (add > 0)
			ambientlight += add;
	}

	ambient[0] = ambient[1] = ambient[2] = ambient[3] = (float)ambientlight / 128;
	diffuse[0] = diffuse[1] = diffuse[2] = diffuse[3] = (float)shadelight / 128;

// hack the depth range to prevent view model from poking into walls
  float save1 = r_interpolate_anim.value;
  float save2 = r_interpolate_pos.value;
  r_interpolate_anim.value = r_interpolate_pos.value = 0;
  #ifdef _arch_dreamcast
  glDepthRangef(0.6f,0.7f);
  R_DrawAliasModel(currententity);
  glDepthRangef(gldepthmin, gldepthmax);
  #else
  glDepthRange(gldepthmin, gldepthmin + 0.1f*(gldepthmax-gldepthmin));
  R_DrawAliasModel(currententity);
  glDepthRange(gldepthmin, gldepthmax);
  #endif
  r_interpolate_anim.value = save1;
  r_interpolate_pos.value = save2;
}

/*
============
R_PolyBlend
============
*/
void R_PolyBlend(void) {
  if (!gl_polyblend.value)
    return;
  if (!v_blend[3])
    return;

  glDisable(GL_TEXTURE_2D);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glLoadIdentity();

  glRotatef(-90, 1, 0, 0);  // put Z going up
  glRotatef(90, 0, 0, 1);   // put Z going up
  glvert_fast_t quadvert[4];
  /* 1----2
	   |    |
		 |    |
		 4----3
		 Strip: 1423
		 2Tris: 124423
 	*/
  //Vertex 1
  //Quad vertex
  quadvert[0] = (glvert_fast_t){.flags = VERTEX, .vert = {10, -100, 100}, .texture = {0, 1}, .color = {(uint8_t)(v_blend[2] * 255), (uint8_t)(v_blend[1] * 255), (uint8_t)(v_blend[0] * 255), (uint8_t)(v_blend[3] * 255)}, .pad0 = {0}};

  //Vertex 4
  //Quad vertex
  quadvert[1] = (glvert_fast_t){.flags = VERTEX, .vert = {10, -100, -100}, .texture = {0, 1}, .color = {(uint8_t)(v_blend[2] * 255), (uint8_t)(v_blend[1] * 255), (uint8_t)(v_blend[0] * 255), (uint8_t)(v_blend[3] * 255)}, .pad0 = {0}};

  //Vertex 2
  //Quad vertex
  quadvert[2] = (glvert_fast_t){.flags = VERTEX, .vert = {10, 100, 100}, .texture = {0, 1}, .color = {(uint8_t)(v_blend[2] * 255), (uint8_t)(v_blend[1] * 255), (uint8_t)(v_blend[0] * 255), (uint8_t)(v_blend[3] * 255)}, .pad0 = {0}};

  //Vertex 3
  //Quad vertex
  quadvert[3] = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {10, 100, -100}, .texture = {0, 1}, .color = {(uint8_t)(v_blend[2] * 255), (uint8_t)(v_blend[1] * 255), (uint8_t)(v_blend[0] * 255), (uint8_t)(v_blend[3] * 255)}, .pad0 = {0}};

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &quadvert[0].vert);
  glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &quadvert[0].texture);
  glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &quadvert[0].color);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableClientState(GL_COLOR_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDepthMask(GL_TRUE);

  glEnable(GL_TEXTURE_2D);
}

int SignbitsForPlane(mplane_t *out) {
  int bits, j;

  // for fast box on planeside test

  bits = 0;
  for (j = 0; j < 3; j++) {
    if (out->normal[j] < 0)
      bits |= 1 << j;
  }
  return bits;
}

void R_SetFrustum(void) {
  int i;

  if (r_refdef.fov_x == 90) {
    // front side is visible

    VectorAdd(vpn, vright, frustum[0].normal);
    VectorSubtract(vpn, vright, frustum[1].normal);

    VectorAdd(vpn, vup, frustum[2].normal);
    VectorSubtract(vpn, vup, frustum[3].normal);
  } else {
    // rotate VPN right by FOV_X/2 degrees
    RotatePointAroundVector(frustum[0].normal, vup, vpn, -(90 - r_refdef.fov_x * 0.5f));
    // rotate VPN left by FOV_X/2 degrees
    RotatePointAroundVector(frustum[1].normal, vup, vpn, 90 - r_refdef.fov_x * 0.5f);
    // rotate VPN up by FOV_X/2 degrees
    RotatePointAroundVector(frustum[2].normal, vright, vpn, 90 - r_refdef.fov_y * 0.5f);
    // rotate VPN down by FOV_X/2 degrees
    RotatePointAroundVector(frustum[3].normal, vright, vpn, -(90 - r_refdef.fov_y * 0.5f));
  }

  for (i = 0; i < 4; i++) {
    frustum[i].type = PLANE_ANYZ;
    frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
    frustum[i].signbits = SignbitsForPlane(&frustum[i]);
  }
}

/*
===============
R_SetupFrame
===============
*/
void R_SetupFrame(void) {
  // don't allow cheats in multiplayer
  if (cl.maxclients > 1)
    Cvar_Set("r_fullbright", "0");

  R_AnimateLight();

  r_framecount++;

  // build the transformation matrix for the given view angles
  VectorCopy(r_refdef.vieworg, r_origin);

  AngleVectors(r_refdef.viewangles, vpn, vright, vup);

  // current viewleaf
  r_oldviewleaf = r_viewleaf;
  r_viewleaf = Mod_PointInLeaf(r_origin, cl.worldmodel);

  V_SetContentsColor(r_viewleaf->contents);
  V_CalcBlend();

  r_cache_thrash = false;

  c_brush_polys = 0;
  c_alias_polys = 0;
}

void MYgluPerspective(GLdouble fovy, GLdouble aspect,
                      GLdouble zNear, GLdouble zFar) {
  GLdouble xmin, xmax, ymin, ymax;

  ymax = zNear * tanf(fovy * M_PI / 360.0f);
  ymin = -ymax;

  xmin = ymin * aspect;
  xmax = ymax * aspect;

  glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}

/*
=============
R_SetupGL
=============
*/
void R_SetupGL(void) {
  float screenaspect;
  extern int glwidth, glheight;
  int x, x2, y2, y, w, h;

  //
  // set up viewpoint
  //
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  x = r_refdef.vrect.x * glwidth / vid.width;
  x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth / vid.width;
  y = (vid.height - r_refdef.vrect.y) * glheight / vid.height;
  y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight / vid.height;

  // fudge around because of frac screen scale
  if (x > 0)
    x--;
  if (x2 < glwidth)
    x2++;
  if (y2 < 0)
    y2--;
  if (y < glheight)
    y++;

  w = x2 - x;
  h = y - y2;

  if (envmap) {
    x = y2 = 0;
    w = h = 256;
  }

  glViewport(glx + x, gly + y2, w, h);
  screenaspect = (float)r_refdef.vrect.width / r_refdef.vrect.height;
  float yfov = 2 * atanf((float)r_refdef.vrect.height / r_refdef.vrect.width) * 180 / M_PI;
  gluPerspective(yfov, screenaspect, 4, 4096);
  //MYgluPerspective(r_refdef.fov_y, screenaspect, 4, 4096);

  //
  // set drawing parms
  //
  if (gl_cull.value)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
  glCullFace(GL_FRONT);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  mat4 model_temp = GLM_MAT4_IDENTITY_INIT;

  glm_rotate_x(model_temp, DEG2RAD(-90.0f), model_temp);  // put Z going up
  glm_rotate_z(model_temp, DEG2RAD(90), model_temp);      // put Z going up

  glm_rotate_x(model_temp, DEG2RAD(-r_refdef.viewangles[2]), model_temp);
  glm_rotate_y(model_temp, DEG2RAD(-r_refdef.viewangles[0]), model_temp);
  glm_rotate_z(model_temp, DEG2RAD(-r_refdef.viewangles[1]), model_temp);

  glm_translate(model_temp, (vec3){-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]});

  glLoadMatrixf((float *)model_temp);

  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
void R_RenderScene(void) {
  R_SetupFrame();

  R_SetFrustum();

  R_SetupGL();

  R_MarkLeaves();  // done here so we know if we're in water

  /*@Note: controls world geometry drawing */
  R_DrawWorld();  // adds static entities to the list

  S_ExtraUpdate();  // don't let sound get messed up if going slow

  R_DrawEntitiesOnList();

  GL_DisableMultitexture();

  R_RenderDlights();

  R_DrawParticles();
}

/*
=============
R_Clear
=============
*/
void R_Clear (void)
{
  #if 0
	if (r_mirroralpha.value != 1.0)
	{
		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 0.5;
		glDepthFunc (GL_LEQUAL);
	}
	else if (gl_ztrick.value)
	{
		static int trickframe;

		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT);

		trickframe++;
		if (trickframe & 1)
		{
			gldepthmin = 0;
			gldepthmax = 0.49999;
			glDepthFunc (GL_LEQUAL);
		}
		else
		{
			gldepthmin = 1;
			gldepthmax = 0.5;
			glDepthFunc (GL_GEQUAL);
		}
	}
	else
	{
  #endif
		if (gl_clear.value)
			glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		else
			glClear (GL_DEPTH_BUFFER_BIT);
		gldepthmin = 0;
		gldepthmax = 1;
		glDepthFunc (GL_LEQUAL);
	//}

	glDepthRange (gldepthmin, gldepthmax);
}

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void R_RenderView(void) {
  float time1 = 0, time2 = 0;

  if (r_norefresh.value)
    return;

  if (!r_worldentity.model || !cl.worldmodel)
    Con_DPrintf("R_RenderView: NULL worldmodel");

  if (r_speeds.value) {
    time1 = Sys_FloatTime();
    c_brush_polys = 0;
    c_alias_polys = 0;
  }

  mirror = false;

  if (gl_finish.value)
    glFinish();

  R_Clear();

  // render normal view

  /***** Experimental silly looking fog ******
****** Use r_fullbright if you enable ******
	GLfloat colors[4] = {(GLfloat) 0.0, (GLfloat) 0.0, (GLfloat) 1, (GLfloat) 0.20};
	glFogi(GL_FOG_MODE, GL_LINEAR);
	glFogfv(GL_FOG_COLOR, colors);
	glFogf(GL_FOG_END, 512.0);
	glEnable(GL_FOG);
********************************************/

  R_RenderScene();
  R_DrawViewModel();
  R_DrawWaterSurfaces();

  //  More fog right here :)
  //	glDisable(GL_FOG);
  //  End of all fog code...

  /* render mirror view
	Move check outside, save the call
	if(mirror)
		R_Mirror ();
	*/

  R_PolyBlend(); /*@NOTE: HACK! */

  if (r_speeds.value) {
    //		glFinish ();
    time2 = Sys_FloatTime();
    Con_DPrintf("%3i ms  %4i wpoly %4i epoly\n", (int)((time2 - time1) * 1000), c_brush_polys, c_alias_polys);
  }
}
