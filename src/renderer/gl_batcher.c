// gl_batcher.c: handles creating little batches of polys

#include "quakedef.h"
#ifdef _arch_dreamcast
#ifdef BUILD_LIBGL
#include <glext.h>
#else
#include <GL/glext.h>
#endif
#endif

glvert_fast_t __attribute__((aligned(32))) r_batchedtempverts[MAX_BATCHED_TEMPVERTEXES];
glvert_fast_t __attribute__((aligned(32))) r_batchedfastvertexes[MAX_BATCHED_SURFVERTEXES];
glvert_fast_t __attribute__((aligned(32))) r_batchedfastvertexes_text[MAX_BATCHED_TEXTVERTEXES];

int text_size = 8;

unsigned int r_numsurfvertexes = 0;
unsigned int r_numsurfvertexes_text = 0;

extern inline void R_BeginBatchingFastSurfaces();
extern inline void R_BeginBatchingSurfacesQuad();

#define DIRECT_NORMAL_SURF 1

void R_EndBatchingFastSurfaces(void) {
  #if DIRECT_NORMAL_SURF
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedfastvertexes[0].vert);
  glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedfastvertexes[0].texture);
  glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &r_batchedfastvertexes[0].color);
  glDrawArrays(GL_TRIANGLES, 0, r_numsurfvertexes);
  #ifdef PARANOID
  if (r_numsurfvertexes > 1560)
    printf("%s:%d drew: %d\n", __FILE__, __LINE__, r_numsurfvertexes);
  #endif

  r_numsurfvertexes = 0;
  #endif
}

void R_EndBatchingSurfacesQuads(void) {
  glEnable(GL_BLEND);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_COLOR_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedfastvertexes_text[0].vert);
  glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &r_batchedfastvertexes_text[0].texture);
  glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &r_batchedfastvertexes_text[0].color);
  glDrawArrays(GL_TRIANGLES, 0, r_numsurfvertexes_text);
  glDisable(GL_BLEND);
  r_numsurfvertexes_text = 0;
}

void R_BatchSurface(glpoly_t *p) {
  #if DIRECT_NORMAL_SURF
  const int tris = p->numverts - 2;

  int i;
  const float *v0 = p->verts[0];
  float *v = p->verts[1];
  for (i = 0; i < tris; i++) {
    r_batchedfastvertexes[r_numsurfvertexes++] = (glvert_fast_t){.flags = VERTEX, .vert = {v0[0], v0[1], v0[2]}, .texture = {v0[3], v0[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    r_batchedfastvertexes[r_numsurfvertexes++] = (glvert_fast_t){.flags = VERTEX, .vert = {v[0], v[1], v[2]}, .texture = {v[3], v[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    v += VERTEXSIZE;
    r_batchedfastvertexes[r_numsurfvertexes++] = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {v[0], v[1], v[2]}, .texture = {v[3], v[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
  }
  if (r_numsurfvertexes > (MAX_BATCHED_SURFVERTEXES - 6)) {
    R_EndBatchingFastSurfaces();
  }
  #else 
  const int tris = p->numverts - 2;
  const int verts = tris*3;
  glvert_fast_t* submission_pointer = &r_batchedtempverts[0];

  #ifdef GL_EXT_dreamcast_direct_buffer
  glEnable(GL_DIRECT_BUFFER_KOS);
  glDirectBufferReserve_INTERNAL_KOS(verts, (int *)&submission_pointer, GL_TRIANGLES);
  #endif

  glVertexPointer(3, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->vert);
  glTexCoordPointer(2, GL_FLOAT, sizeof(glvert_fast_t), &submission_pointer->texture);
  glColorPointer(GL_BGRA, GL_UNSIGNED_BYTE, sizeof(glvert_fast_t), &submission_pointer->color);

  int i;
  const float *v0 = p->verts[0];
  float *v = p->verts[1];
  for (i = 0; i < tris; i++) {
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {v0[0], v0[1], v0[2]}, .texture = {v0[3], v0[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {v[0], v[1], v[2]}, .texture = {v[3], v[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    v += VERTEXSIZE;
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {v[0], v[1], v[2]}, .texture = {v[3], v[4]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
  }

  glDrawArrays(GL_TRIANGLES, 0, verts);

  #ifdef GL_EXT_dreamcast_direct_buffer
  glDisable(GL_DIRECT_BUFFER_KOS);
  #endif
  #endif
}

void R_BatchSurfaceLightmap(glpoly_t *p) {
  const int tris = p->numverts - 2;
  glvert_fast_t* submission_pointer = R_GetDirectBufferAddress();

  int i;
  const float *v0 = p->verts[0];
  float *v = p->verts[1];
  for (i = 0; i < tris; i++) {
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {v0[0], v0[1], v0[2]}, .texture = {v0[5], v0[6]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX, .vert = {v[0], v[1], v[2]}, .texture = {v[5], v[6]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
    v += VERTEXSIZE;
    *submission_pointer++ = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {v[0], v[1], v[2]}, .texture = {v[5], v[6]}, .color = {255, 255, 255, 255}, .pad0 = {0}};
  }
}

void R_BeginBatchingFastSurfaces() {
  r_numsurfvertexes = 0;
}

void R_BeginBatchingSurfacesQuad() {
  r_numsurfvertexes_text = 0;
}

void R_BatchSurfaceQuadText(int x, int y, float frow, float fcol, float size) {
  //Vertex 1
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 0] = (glvert_fast_t){.flags = VERTEX, .vert = {x, y, 0}, .texture = {fcol, frow}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  //Vertex 2
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 1] = (glvert_fast_t){.flags = VERTEX, .vert = {x + text_size, y, 0}, .texture = {fcol + size, frow}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  //Vertex 4
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 2] = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {x, y + text_size, 0}, .texture = {fcol, frow + size}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  //Vertex 4
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 3] = (glvert_fast_t){.flags = VERTEX, .vert = {x, y + text_size, 0}, .texture = {fcol, frow + size}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  //Vertex 2
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 4] = (glvert_fast_t){.flags = VERTEX, .vert = {x + text_size, y, 0}, .texture = {fcol + size, frow}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  //Vertex 3
  //Quad vertex
  r_batchedfastvertexes_text[r_numsurfvertexes_text + 5] = (glvert_fast_t){.flags = VERTEX_EOL, .vert = {x + text_size, y + text_size, 0}, .texture = {fcol + size, frow + size}, .color = {255, 255, 255, 255}, .pad0 = {0}};

  r_numsurfvertexes_text += 6;

  if (r_numsurfvertexes_text > (MAX_BATCHED_TEXTVERTEXES - 6))
    R_EndBatchingSurfacesQuads();
}

static glvert_fast_t *_tempBufferAddress = NULL;

glvert_fast_t *R_GetDirectBufferAddress() {
  return _tempBufferAddress;
}

void R_SetDirectBufferAddress(glvert_fast_t *address) {
  _tempBufferAddress = address;
}