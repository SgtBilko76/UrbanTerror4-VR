/*
===========================================================================
Legacy (stock ioquake3) renderer ABI for QVM game modules.

ioq3quest extends two structs that the game modules hand to the engine:
  refdef_t    gains `qboolean isHUD` before `time`
  refEntity_t gains `qboolean invert` at the end
Its own VR cgame/ui are compiled against those layouts. Urban Terror's
cgame/ui ship as QVM bytecode built against stock ioquake3 headers, so the
structs they pass must be converted at the syscall boundary.
===========================================================================
*/
#ifndef CL_LEGACY_ABI_H
#define CL_LEGACY_ABI_H

#include "../renderercommon/tr_types.h"

typedef struct {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewaxis[3];
	int			time;
	int			rdflags;
	byte		areamask[MAX_MAP_AREA_BYTES];
	char		text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];
} legacyRefdef_t;

// refEntity_t without the trailing `invert` field
#define LEGACY_REFENTITY_SIZE	( offsetof( refEntity_t, invert ) )

static ID_INLINE void CL_RefdefFromLegacy( const legacyRefdef_t *in, refdef_t *out ) {
	Com_Memset( out, 0, sizeof( *out ) );
	out->x = in->x;
	out->y = in->y;
	out->width = in->width;
	out->height = in->height;
	out->fov_x = in->fov_x;
	out->fov_y = in->fov_y;
	VectorCopy( in->vieworg, out->vieworg );
	VectorCopy( in->viewaxis[0], out->viewaxis[0] );
	VectorCopy( in->viewaxis[1], out->viewaxis[1] );
	VectorCopy( in->viewaxis[2], out->viewaxis[2] );
	out->isHUD = qfalse;
	out->time = in->time;
	out->rdflags = in->rdflags;
	Com_Memcpy( out->areamask, in->areamask, sizeof( out->areamask ) );
	Com_Memcpy( out->text, in->text, sizeof( out->text ) );
}

static ID_INLINE void CL_RefEntityFromLegacy( const void *in, refEntity_t *out ) {
	Com_Memcpy( out, in, LEGACY_REFENTITY_SIZE );
	out->invert = qfalse;
}


#endif
