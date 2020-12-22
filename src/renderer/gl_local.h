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
void GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);

#if defined(_WIN32)
#include <windows.h>

// Function prototypes for the Texture Object Extension routines
typedef GLboolean (APIENTRY *ARETEXRESFUNCPTR)(GLsizei, const GLuint *,
                    const GLboolean *);
typedef void (APIENTRY *BINDTEXFUNCPTR)(GLenum, GLuint);
typedef void (APIENTRY *DELTEXFUNCPTR)(GLsizei, const GLuint *);
typedef void (APIENTRY *GENTEXFUNCPTR)(GLsizei, GLuint *);
typedef GLboolean (APIENTRY *ISTEXFUNCPTR)(GLuint);
typedef void (APIENTRY *PRIORTEXFUNCPTR)(GLsizei, const GLuint *,
                    const GLclampf *);
typedef void (APIENTRY *TEXSUBIMAGEPTR)(int, int, int, int, int, int, int, int, void *);

extern	BINDTEXFUNCPTR bindTexFunc;
extern	DELTEXFUNCPTR delTexFunc;
extern	TEXSUBIMAGEPTR TexSubImage2DFunc;
#endif

extern	int texture_extension_number;

extern	float	gldepthmin, gldepthmax;

typedef struct mpic_s
{
	int			width, height;
	int			texnum;
	float		sl, tl, sh, th;
} mpic_t;

void R_TimeRefresh_f (void);
texture_t *R_TextureAnimation (texture_t *base);

//====================================================

extern	float	r_world_matrix[16];

extern	const GLubyte *gl_vendor;
extern	const GLubyte *gl_renderer;
extern	const GLubyte *gl_version;
extern	const GLubyte *gl_extensions;

void R_InitSky (struct miptex_s *mt);	// called at level load
void R_TranslatePlayerSkin (int playernum);
void R_PushDlights (void);
void GL_Bind (int texnum);
void GL_Set2D (void);
void GL_Upload8_EXT (byte *data, int width, int height, uint8_t flags);
int GL_LoadPicTexture (mpic_t *pic, byte *data);
void GL_BuildLightmaps (void);

void R_AnimateLight (void);
void R_DrawWorld (void);
void R_DrawBrushModel (entity_t *ent);
void R_RenderDlights (void);
void R_DrawWaterSurfaces (void);
int R_LightPoint (vec3_t p);
void R_MarkLights (dlight_t *light, int bit, mnode_t *node);

void EmitWaterPolys (msurface_t *fa);
void EmitWaterPolys_Temp (msurface_t *fa);
void EmitSkyPolys (msurface_t *fa);
void EmitBothSkyLayers (msurface_t *fa);
void R_DrawSkyChain (msurface_t *s);
qboolean R_LoadSkys (char *name);
void R_DrawSkyBoxChain (msurface_t *s);
void R_ClearSkyBox (void);
void R_DrawSkyBox (void);
void R_RotateForEntity (entity_t *e);
extern qboolean r_drawskybox;

void GL_SubdivideSurface (msurface_t *fa);
void GL_MakeAliasModelDisplayLists (model_t *m, aliashdr_t *hdr);

qboolean R_CullBox (const vec3_t mins, const vec3_t maxs);
qboolean R_CullSphere (const vec3_t centre, float radius);

extern float v_blend[4];
extern model_t *r_worldmodel;

// View
void V_CalcBlend (void);


#ifdef _arch_dreamcast
#define APIENTRY /* */
#endif

typedef void (APIENTRY *lpMTexFUNC) (GLenum, GLfloat, GLfloat);
typedef void (APIENTRY *lpSelTexFUNC) (GLenum);
extern lpMTexFUNC qglMTexCoord2fSGIS;
extern lpSelTexFUNC qglSelectTextureSGIS;

extern qboolean gl_mtexable;

void GL_DisableMultitexture(void);
void GL_EnableMultitexture(void);

void LoadPCX (FILE *f);
void WritePCXfile (char *filename, byte *data, int width, int height,
	int rowbytes, byte *palette);
extern byte *pcx_rgb;
