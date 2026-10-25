/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_cgame.c  -- client system interaction with client game

#include "client.h"

#include "../botlib/botlib.h"
#include "../vr/vr_base.h"
#include "../vr/vr_clientinfo.h"
#include "cl_legacy_abi.h"

#ifdef USE_MUMBLE
#include "libmumblelink.h"
#endif

extern	botlib_export_t	*botlib_export;
extern vr_clientinfo_t vr;

extern qboolean loadCamera(const char *name);
extern void startCamera(int time);
extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

/*
====================
CL_GetGameState
====================
*/
void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}

/*
====================
CL_GetGlconfig
====================
*/
void CL_GetGlconfig( glconfig_t *glconfig ) {
	*glconfig = cls.glconfig;
}

/*
====================
CL_GetUserCmd
====================
*/
qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cmdNumber > cl.cmdNumber ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: %i >= %i", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cmdNumber <= cl.cmdNumber - CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}

int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetParseEntityState
====================
*/
qboolean	CL_GetParseEntityState( int parseEntityNumber, entityState_t *state ) {
	// can't return anything that hasn't been parsed yet
	if ( parseEntityNumber >= cl.parseEntitiesNum ) {
		Com_Error( ERR_DROP, "CL_GetParseEntityState: %i >= %i",
			parseEntityNumber, cl.parseEntitiesNum );
	}

	// can't return anything that has been overwritten in the circular buffer
	if ( parseEntityNumber <= cl.parseEntitiesNum - MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	*state = cl.parseEntities[ parseEntityNumber & ( MAX_PARSE_ENTITIES - 1 ) ];
	return qtrue;
}

/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
void	CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}

/*
====================
CL_GetSnapshot
====================
*/
qboolean	CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber > cl.snapshot.messageNum" );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	Com_Memcpy( snapshot->areamask, clSnap->areamask, sizeof( snapshot->areamask ) );
	snapshot->ps = clSnap->ps;
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
	for ( i = 0 ; i < count ; i++ ) {
		snapshot->entities[i] = 
			cl.parseEntities[ ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ];
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}

/*
=====================
CL_SetUserCmdValue
=====================
*/
void CL_SetUserCmdValue( int userCmdValue, float sensitivityScale ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameSensitivity = sensitivityScale;
}

/*
=====================
CL_AddCgameCommand
=====================
*/
void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, NULL );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv(1) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;
		
	for ( i = 0 ; i < MAX_CONFIGSTRINGS ; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	}

}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
qboolean CL_GetServerCommand( int serverCommandNumber ) {
	char	*s;
	char	*cmd;
	static char bigConfigString[BIG_INFO_STRING];
	int argc;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( serverCommandNumber <= clc.serverCommandSequence - MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying )
			return qfalse;
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( serverCommandNumber > clc.serverCommandSequence ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	s = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

rescan:
	Cmd_TokenizeString( s );
	cmd = Cmd_Argv(0);
	argc = Cmd_Argc();

	if ( !strcmp( cmd, "disconnect" ) ) {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=552
		// allow server to indicate why they were disconnected
		if ( argc >= 2 )
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected - %s", Cmd_Argv( 1 ) );
		else
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected" );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString, BIG_INFO_STRING, "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString) + strlen(s) + 1 >= BIG_INFO_STRING ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		strcat( bigConfigString, s );
		strcat( bigConfigString, "\"" );
		s = bigConfigString;
		goto rescan;
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		Com_Memset( cl.cmds, 0, sizeof( cl.cmds ) );
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}

/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	cls.cgameStarted = qfalse;
	if ( !cgvm ) {
		return;
	}
	VM_Call( cgvm, CG_SHUTDOWN );
	VM_Free( cgvm );
	cgvm = NULL;
}

static int	FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}

/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
/*
====================
CL_VRHeadOffset

Urban Terror's cgame knows nothing about the headset position. ioq3quest's own cgame turns
leaning into slow player movement, which in UrT feels like the world sliding along with the
head. Instead move the rendered camera by the real head offset (stage space -> world, rotated
by the difference between the game's view yaw and the headset yaw), limited to a short
distance from the body and stopped at walls. The view weapon stays with the body.
====================
*/
extern cvar_t *vr_worldscale;
static void CL_VRHeadOffset( refdef_t *rd ) {
	// QuakeQuest model: horizontal head movement moves the player (positional movement in
	// vr_input.c, with collision), so the camera only gets the vertical offset here: real
	// crouching / standing up relative to the height taken when play starts.
	// vr_urtLeanCamera 1 = old behaviour (camera slides up to 0.6 m with the head).
	static qboolean haveAnchor;
	static vec3_t anchor;
	static vec3_t lastOrg;
	static float playerHeight;
	static int lastGameFrame = -10;
	const float maxLean = 0.6f;
	float scale, dx, dz, len, f, l, theta, gameYaw;
	vec3_t offset, end, mins = { -4, -4, -4 }, maxs = { 4, 4, 4 };
	trace_t tr;

	scale = vr_worldscale ? vr_worldscale->value : 32.0f;
	VectorClear( offset );

	if ( cls.framecount - lastGameFrame > 2 ) {
		playerHeight = vr.hmdposition[1];
	}
	lastGameFrame = cls.framecount;
	offset[2] = ( vr.hmdposition[1] - playerHeight ) * scale;
	if ( offset[2] < -40.0f ) offset[2] = -40.0f;
	if ( offset[2] > 16.0f ) offset[2] = 16.0f;

	if ( Cvar_VariableIntegerValue( "vr_urtLeanCamera" ) ) {
		if ( !haveAnchor || Distance( rd->vieworg, lastOrg ) > 64.0f ) {
			VectorCopy( vr.hmdposition, anchor );
			haveAnchor = qtrue;
		}
		dx = vr.hmdposition[0] - anchor[0];
		dz = vr.hmdposition[2] - anchor[2];
		len = sqrt( dx * dx + dz * dz );
		if ( len > maxLean ) {
			float pull = ( len - maxLean ) / len;
			anchor[0] += dx * pull;
			anchor[2] += dz * pull;
			dx = vr.hmdposition[0] - anchor[0];
			dz = vr.hmdposition[2] - anchor[2];
		}
		f = -dz;
		l = -dx;
		gameYaw = RAD2DEG( atan2( rd->viewaxis[0][1], rd->viewaxis[0][0] ) );
		theta = DEG2RAD( gameYaw - vr.hmdorientation[YAW] );
		offset[0] = ( f * cos( theta ) - l * sin( theta ) ) * scale;
		offset[1] = ( f * sin( theta ) + l * cos( theta ) ) * scale;
	} else {
		haveAnchor = qfalse;
	}
	VectorCopy( rd->vieworg, lastOrg );

	VectorAdd( rd->vieworg, offset, end );
	CM_BoxTrace( &tr, rd->vieworg, end, mins, maxs, 0, CONTENTS_SOLID, qfalse );
	VectorCopy( tr.endpos, rd->vieworg );
}

/*
====================
VR controller aiming for Urban Terror (engine side, like QuakeQuest)

UrT's cgame/qagame are closed QVMs that know nothing about VR. The engine therefore:
  * sends the dominant controller's direction to the game as the view angles (CL_FinishMove),
    so the server and the cgame's prediction shoot where the hand points,
  * renders the camera from the headset instead of from those angles (CL_VRApplyView),
  * moves the first-person weapon/hands model from in front of the eye into the hand
    (first-person entities are held back until the scene is rendered, then re-placed),
  * draws a laser sight from the hand to what it is pointing at.
====================
*/
extern cvar_t *vr_worldscale;
qboolean cl_vrAimActive;		// read by CL_FinishMove
int cl_vrAimTime;
float cl_vrAimYawDelta;		// aim yaw minus head yaw (degrees)
float cl_vrAimPitch;		// absolute aim pitch
float cl_vrAimPitchCorr;	// pitch the server adds on top of what we send (UrT: about 11 degrees)

/*
Urban Terror's server adds a pitch offset to the commanded view angles. Measure it from the
last snapshot: the angle the server reports vs. the command it processed (serverTime ==
ps.commandTime) plus delta_angles. Not while firing, so recoil does not leak in.
*/
static void CL_VRMeasurePitchCorrection( void ) {
	static int lastSnap, lastFire;
	int i;
	if ( cl.snap.serverTime == lastSnap || !cl.snap.valid ) {
		return;
	}
	lastSnap = cl.snap.serverTime;
	for ( i = 0; i < 4; i++ ) {
		if ( cl.cmds[( cl.cmdNumber - i ) & CMD_MASK].buttons & BUTTON_ATTACK ) {
			lastFire = cls.realtime;
		}
	}
	if ( cls.realtime - lastFire < 600 ) {
		return;
	}
	for ( i = 0; i < CMD_BACKUP - 1; i++ ) {
		const usercmd_t *c = &cl.cmds[( cl.cmdNumber - i ) & CMD_MASK];
		if ( c->serverTime == cl.snap.ps.commandTime ) {
			float sent = AngleNormalize180( SHORT2ANGLE( c->angles[PITCH] ) + SHORT2ANGLE( cl.snap.ps.delta_angles[PITCH] ) );
			float got = AngleNormalize180( cl.snap.ps.viewangles[PITCH] );
			float err = AngleNormalize180( got - sent );
			if ( fabs( sent ) < 70.0f && fabs( err ) < 30.0f ) {
				cl_vrAimPitchCorr = 0.8f * cl_vrAimPitchCorr + 0.2f * err;
			}
			return;
		}
	}
}

#define MAX_VR_FP_ENTS 64

// names of the cgame's models by handle (to find UrT's "*_hold" arms model)
#define MAX_VR_MODEL_NAMES 1024
static char vrModelNames[MAX_VR_MODEL_NAMES][MAX_QPATH];

/*
UrT's first-person arms are one model per weapon ("<weapon>_hold.md3") with a surface per
hand (h_handleft*, h_handright*). In VR the right hand stays with the gun and the left arm is
drawn a second time at the left controller. The pivot is the left hand: the front-most
vertices of the h_handleft surface, per animation frame, read from the MD3 file.
*/
#define MAX_VR_HOLD_MODELS 32
typedef struct {
	char	name[MAX_QPATH];
	int		numFrames;
	vec3_t	*pivot;		// per frame, model space; NULL if the model has no left hand
	vec3_t	*elbow;		// per frame: back end of the left forearm
	vec3_t	*rpivot;	// per frame: right hand (front-most 7 units of h_handright*), NULL if none
	int		rFrames;
} vrHoldModel_t;
static vrHoldModel_t vrHoldModels[MAX_VR_HOLD_MODELS];
static int vrNumHoldModels;

// mean of the vertices within 7 units of the front-most (max x) point: the hand of an arm surface
static void CL_VRHandCluster( const short *xyz, int numVerts, vec3_t out ) {
	float maxX = -99999.0f;
	vec3_t sum = { 0, 0, 0 };
	int v, n = 0;
	for ( v = 0; v < numVerts; v++ ) {
		float x = LittleShort( xyz[v * 4] ) / 64.0f;
		if ( x > maxX ) maxX = x;
	}
	for ( v = 0; v < numVerts; v++ ) {
		float x = LittleShort( xyz[v * 4] ) / 64.0f;
		if ( x > maxX - 7.0f ) {
			sum[0] += x;
			sum[1] += LittleShort( xyz[v * 4 + 1] ) / 64.0f;
			sum[2] += LittleShort( xyz[v * 4 + 2] ) / 64.0f;
			n++;
		}
	}
	VectorScale( sum, n ? 1.0f / n : 0.0f, out );
}

static const vrHoldModel_t *CL_VRHoldModel( const char *name ) {
	vrHoldModel_t *hm;
	union { void *v; byte *b; } buf;
	int i, len;

	for ( i = 0; i < vrNumHoldModels; i++ ) {
		if ( !Q_stricmp( vrHoldModels[i].name, name ) ) return &vrHoldModels[i];
	}
	if ( vrNumHoldModels >= MAX_VR_HOLD_MODELS ) return NULL;
	hm = &vrHoldModels[vrNumHoldModels++];
	Com_Memset( hm, 0, sizeof( *hm ) );
	Q_strncpyz( hm->name, name, sizeof( hm->name ) );

	len = FS_ReadFile( name, &buf.v );
	if ( len >= 108 && !memcmp( buf.b, "IDP3", 4 ) ) {
		int numFrames = LittleLong( *(int *)( buf.b + 76 ) );
		int numSurfs = LittleLong( *(int *)( buf.b + 84 ) );
		int ofs = LittleLong( *(int *)( buf.b + 100 ) );
		int s;
		for ( s = 0; s < numSurfs && ofs > 0 && ofs + 108 <= len; s++ ) {
			const byte *sf = buf.b + ofs;
			char sname[65];
			int sFrames = LittleLong( *(int *)( sf + 72 ) );
			int numVerts = LittleLong( *(int *)( sf + 80 ) );
			int ofsXyz = LittleLong( *(int *)( sf + 100 ) );
			int ofsEnd = LittleLong( *(int *)( sf + 104 ) );
			Com_Memcpy( sname, sf + 4, 64 ); sname[64] = 0;
			Q_strlwr( sname );
			if ( !strncmp( sname, "h_handright", 11 ) && !hm->rpivot && numVerts > 0 && sFrames > 0 &&
					ofs + ofsXyz + sFrames * numVerts * 8 <= len ) {
				int f;
				hm->rFrames = sFrames;
				hm->rpivot = Z_Malloc( sFrames * sizeof( vec3_t ) );
				for ( f = 0; f < sFrames; f++ ) {
					CL_VRHandCluster( (const short *)( sf + ofsXyz + f * numVerts * 8 ), numVerts, hm->rpivot[f] );
				}
			}
			if ( !strncmp( sname, "h_handleft", 10 ) && !hm->pivot && numVerts > 0 && sFrames > 0 &&
					ofs + ofsXyz + sFrames * numVerts * 8 <= len ) {
				int f, v;
				hm->numFrames = sFrames;
				hm->pivot = Z_Malloc( sFrames * sizeof( vec3_t ) );
				hm->elbow = Z_Malloc( sFrames * sizeof( vec3_t ) );
				for ( f = 0; f < sFrames; f++ ) {
					const short *xyz = (const short *)( sf + ofsXyz + f * numVerts * 8 );
					float maxX = -99999.0f;
					vec3_t sum = { 0, 0, 0 };
					int n = 0;
					for ( v = 0; v < numVerts; v++ ) {
						float x = LittleShort( xyz[v * 4] ) / 64.0f;
						if ( x > maxX ) maxX = x;
					}
					// the hand: everything within 7 units of the front-most point
					for ( v = 0; v < numVerts; v++ ) {
						float x = LittleShort( xyz[v * 4] ) / 64.0f;
						if ( x > maxX - 7.0f ) {
							sum[0] += x;
							sum[1] += LittleShort( xyz[v * 4 + 1] ) / 64.0f;
							sum[2] += LittleShort( xyz[v * 4 + 2] ) / 64.0f;
							n++;
						}
					}
					VectorScale( sum, 1.0f / n, hm->pivot[f] );
					{	// elbow: the rear-most 7 units
						float minX = 99999.0f;
						vec3_t esum = { 0, 0, 0 };
						int en = 0;
						for ( v = 0; v < numVerts; v++ ) {
							float x = LittleShort( xyz[v * 4] ) / 64.0f;
							if ( x < minX ) minX = x;
						}
						for ( v = 0; v < numVerts; v++ ) {
							float x = LittleShort( xyz[v * 4] ) / 64.0f;
							if ( x < minX + 7.0f ) {
								esum[0] += x;
								esum[1] += LittleShort( xyz[v * 4 + 1] ) / 64.0f;
								esum[2] += LittleShort( xyz[v * 4 + 2] ) / 64.0f;
								en++;
							}
						}
						VectorScale( esum, 1.0f / en, hm->elbow[f] );
					}
				}
			}
			if ( ofsEnd <= 0 ) break;
			ofs += ofsEnd;
		}
		(void)numFrames;
	}
	if ( len > 0 ) FS_FreeFile( buf.v );
	Com_DPrintf( "VR arms: %s left hand %s, right hand %s\n", name, hm->pivot ? "found" : "missing",
		hm->rpivot ? "found" : "missing" );
	return hm;
}

static qboolean CL_VRIsHoldModel( qhandle_t h ) {
	return h > 0 && h < MAX_VR_MODEL_NAMES && strstr( vrModelNames[h], "_hold" ) != NULL;
}

// left arm: the model's left forearm (elbow -> hand) is laid along the left controller's
// forward axis, the model's up as close to the controller's up as possible, the hand at the
// controller. e = the right-hand copy (for model, frame and scale).
static void CL_VRAddLeftArm( const refEntity_t *e, const vec3_t offOrg, vec3_t oAxis[3] ) {
	const vrHoldModel_t *hm = CL_VRHoldModel( vrModelNames[e->hModel] );
	refEntity_t left;
	vec3_t p, el, fm, lm, um, zUp = { 0, 0, 1 };
	float lerp, scale;
	int i, f0, f1;

	if ( !hm || !hm->pivot ) return;
	f0 = e->frame; f1 = e->oldframe;
	if ( f0 < 0 || f0 >= hm->numFrames ) f0 = 0;
	if ( f1 < 0 || f1 >= hm->numFrames ) f1 = f0;
	lerp = e->backlerp;
	for ( i = 0; i < 3; i++ ) {
		p[i] = hm->pivot[f0][i] * ( 1.0f - lerp ) + hm->pivot[f1][i] * lerp;
		el[i] = hm->elbow[f0][i] * ( 1.0f - lerp ) + hm->elbow[f1][i] * lerp;
	}
	// forearm frame in model space
	VectorSubtract( p, el, fm );
	if ( VectorNormalize( fm ) < 0.01f ) return;
	VectorMA( zUp, -DotProduct( zUp, fm ), fm, um );
	if ( VectorNormalize( um ) < 0.01f ) return;
	CrossProduct( um, fm, lm );
	scale = VectorLength( e->axis[0] );

	left = *e;
	left.vrHandSide = 1;
	// world image of model axis i: scale * ( fm[i] F + lm[i] L + um[i] U )
	for ( i = 0; i < 3; i++ ) {
		VectorScale( oAxis[0], fm[i] * scale, left.axis[i] );
		VectorMA( left.axis[i], lm[i] * scale, oAxis[1], left.axis[i] );
		VectorMA( left.axis[i], um[i] * scale, oAxis[2], left.axis[i] );
	}
	VectorCopy( offOrg, left.origin );
	for ( i = 0; i < 3; i++ ) VectorMA( left.origin, -p[i], left.axis[i], left.origin );
	VectorCopy( left.origin, left.oldorigin );
	VectorCopy( offOrg, left.lightingOrigin );
	left.nonNormalizedAxes = qtrue;
	re.AddRefEntityToScene( &left );
}
static refEntity_t vrFpEnts[MAX_VR_FP_ENTS];
static int vrNumFpEnts;

static qboolean CL_VRAimEnabled( void ) {
	return Cvar_VariableIntegerValue( "vr_urtControllerAim" ) &&
		clc.state == CA_ACTIVE && !( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CONSOLE ) );
}

static qboolean CL_VRFirstPersonPlayer( void ) {
	// own player, alive (PM_NORMAL/PM_NOCLIP/PM_SPECTATOR), not following someone else
	return cl.snap.ps.clientNum == clc.clientNum && cl.snap.ps.pm_type < 3;
}

static void CL_VRHoldEntity( const refEntity_t *ent ) {
	if ( vrNumFpEnts < MAX_VR_FP_ENTS ) {
		vrFpEnts[vrNumFpEnts++] = *ent;
	}
}

static void CL_VRTransformPoint( const vec3_t in, const vec3_t gOrg, vec3_t gAxis[3],
		const vec3_t grip, const vec3_t hOrg, vec3_t hAxis[3], vec3_t out ) {
	vec3_t d, l;
	int i;
	VectorSubtract( in, gOrg, d );
	for ( i = 0; i < 3; i++ ) {
		l[i] = DotProduct( d, gAxis[i] ) - grip[i];
	}
	VectorCopy( hOrg, out );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( out, l[i], hAxis[i], out );
	}
}

static void CL_VRTransformAxis( const vec3_t in, vec3_t gAxis[3], vec3_t hAxis[3], vec3_t out ) {
	int i;
	VectorClear( out );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( out, DotProduct( in, gAxis[i] ), hAxis[i], out );
	}
}

// stage-space offset (OpenXR: +x right, +y up, -z forward) -> world, for a given head yaw
static void CL_VRStageToWorld( const vec3_t stage, float headYaw, vec3_t out ) {
	float scale = vr_worldscale ? vr_worldscale->value : 32.0f;
	float f = -stage[2], l = -stage[0], u = stage[1];
	float theta = DEG2RAD( headYaw - vr.hmdorientation[YAW] );
	out[0] = ( f * cos( theta ) - l * sin( theta ) ) * scale;
	out[1] = ( f * sin( theta ) + l * cos( theta ) ) * scale;
	out[2] = u * scale;
}

// Scope zoom. Urban Terror zooms by narrowing its refdef FOV (ut_zoomin/ut_zoomout/ut_zoomreset).
// The VR eye projection ignores the game's FOV, so the same narrowing is applied to the eye
// projection instead (vr_renderer.c, VR_ZoomAngle): zoom = tan(cg_fov/2) / tan(fov_x/2).
float vr_urtZoom = 1.0f;
int vr_urtZoomTime;
static qboolean cl_vrScopeView;	// zoomed: the camera looks along the gun, not the head
static float vrZoomPressFov;	// fov_x when vr_zoom was pressed, 0 = no check pending
static int vrZoomPressTime;
static float vrZoomLastFov = 90.0f;

void CL_VRZoom_f( void ) {
	// one zoom step; if the weapon is already at its strongest zoom (the FOV doesn't get
	// narrower), go back to no zoom
	vrZoomPressFov = vrZoomLastFov;
	vrZoomPressTime = cls.realtime;
	Cbuf_AddText( "ut_zoomin\n" );
}

static void CL_VRUpdateZoom( float fovX ) {
	float base = Cvar_VariableValue( "cg_fov" );
	if ( base < 10.0f || base > 170.0f ) base = 90.0f;
	if ( vrZoomPressFov > 0.0f ) {
		if ( fovX < vrZoomPressFov - 0.5f ) {
			vrZoomPressFov = 0.0f;		// zoomed further in
		} else if ( cls.realtime - vrZoomPressTime > 400 ) {
			if ( fovX < base - 0.5f ) {
				Cbuf_AddText( "ut_zoomreset\n" );
			}
			vrZoomPressFov = 0.0f;
		}
	}
	vrZoomLastFov = fovX;
	if ( fovX > 1.0f && fovX < base - 0.5f && Cvar_VariableIntegerValue( "vr_urtZoom" ) ) {
		vr_urtZoom = tan( DEG2RAD( base ) * 0.5 ) / tan( DEG2RAD( fovX ) * 0.5 );
	} else {
		vr_urtZoom = 1.0f;
	}
	vr_urtZoomTime = cls.realtime;
}

/*
Barrel alignment. A flat-screen view model sits low-right and is angled so the barrel points at
the screen-centre crosshair; in the hand that angle makes the gun shoot off its barrel. The
weapon's muzzle tag (tag_flash on the arms model) gives the real barrel direction: the gun is
turned about the grip so that direction is the hand's aim direction. Measured only while the
weapon is idle (not raising, reloading or firing), so the game's animations still show.
*/
static qhandle_t vrGunModel;
static int vrGunModelTime, vrGunBusyTime;
static qboolean vrGunDirValid;
static vec3_t vrGunDir;			// smoothed barrel direction, eye-frame coordinates
static qboolean vrMuzzleValid;
static vec3_t vrMuzzleLocal;	// muzzle relative to the hand, hand-frame coordinates

// rotation that turns unit vector d onto +x, applied to v
static void CL_VRRotateToX( const vec3_t d, const vec3_t v, vec3_t out ) {
	vec3_t x = { 1, 0, 0 }, k, kv;
	float c = DotProduct( d, x ), sn, kl;
	CrossProduct( d, x, k );
	kl = VectorNormalize( k );
	if ( kl < 1e-5f ) { VectorCopy( v, out ); return; }
	sn = kl;
	CrossProduct( k, v, kv );
	VectorScale( v, c, out );
	VectorMA( out, sn, kv, out );
	VectorMA( out, DotProduct( k, v ) * ( 1.0f - c ), k, out );
}

static void CL_VRMeasureBarrel( const refEntity_t *hold, vec3_t gAxis[3] ) {
	orientation_t tag;
	vec3_t dir, dl;
	int i;
	if ( hold->hModel != vrGunModel ) {
		vrGunModel = hold->hModel;
		vrGunModelTime = cls.realtime;
		vrGunDirValid = qfalse;
	}
	for ( i = 0; i < 4; i++ ) {
		int b = cl.cmds[( cl.cmdNumber - i ) & CMD_MASK].buttons;
		if ( b & ( BUTTON_ATTACK | ( 1 << 5 ) ) ) vrGunBusyTime = cls.realtime;	// fire / reload
	}
	if ( cls.realtime - vrGunModelTime < 1500 ) return;	// raise animation
	if ( vrGunDirValid && cls.realtime - vrGunBusyTime < 2500 ) return;
	if ( !re.LerpTag( &tag, hold->hModel, hold->oldframe, hold->frame, 1.0f - hold->backlerp, "tag_flash" ) ) return;
	VectorClear( dir );
	for ( i = 0; i < 3; i++ ) VectorMA( dir, tag.axis[0][i], hold->axis[i], dir );
	if ( VectorNormalize( dir ) < 1e-4f ) return;
	for ( i = 0; i < 3; i++ ) dl[i] = DotProduct( dir, gAxis[i] );
	if ( dl[0] < cos( DEG2RAD( 25.0f ) ) ) return;	// not pointing roughly forward: some animation
	if ( !vrGunDirValid ) {
		VectorCopy( dl, vrGunDir );
		vrGunDirValid = qtrue;
	} else {
		VectorScale( vrGunDir, 0.9f, vrGunDir );
		VectorMA( vrGunDir, 0.1f, dl, vrGunDir );
		VectorNormalize( vrGunDir );
	}
}

static void CL_VRApplyView( const legacyRefdef_t *in, refdef_t *rd ) {
	vec3_t head, aim, hand, off, fwd, end, dir, grip, gAxis[3], hAxis[3], zero = { 0, 0, 0 };
	vec3_t aimEye, offOrg, offAng, oAxis[3], gunAxis[3];
	float gunScale, unsquashS = 1.0f, unsquashBaseF = 0.0f;
	float headYaw;
	trace_t tr;
	int i;

	CL_VRUpdateZoom( in->fov_x );
	if ( !CL_VRAimEnabled() || !CL_VRFirstPersonPlayer() ) {
		// not aiming with the controller: put the held entities back unchanged
		for ( i = 0; i < vrNumFpEnts; i++ ) {
			re.AddRefEntityToScene( &vrFpEnts[i] );
		}
		vrNumFpEnts = 0;
		cl_vrAimActive = qfalse;
		CL_VRHeadOffset( rd );
		return;
	}

	// camera: headset orientation, yaw anchored to the client's (head-driven) view yaw
	headYaw = AngleNormalize180( cl.viewangles[YAW] + SHORT2ANGLE( cl.snap.ps.delta_angles[YAW] ) );
	head[PITCH] = vr.hmdorientation[PITCH];
	head[YAW] = headYaw;
	head[ROLL] = vr.hmdorientation[ROLL];
	AnglesToAxis( head, rd->viewaxis );
	CL_VRHeadOffset( rd );

	// hand position and orientation in the world
	VectorSubtract( vr.weaponposition, vr.hmdposition, off );
	CL_VRStageToWorld( off, headYaw, hand );
	VectorAdd( rd->vieworg, hand, hand );
	aim[PITCH] = vr.weaponangles[PITCH];
	aim[YAW] = AngleNormalize180( vr.weaponangles[YAW] + ( headYaw - vr.hmdorientation[YAW] ) );
	aim[ROLL] = vr.weaponangles[ROLL];
	AnglesToAxis( aim, hAxis );

	// left (off) hand position and orientation in the world, plus a grip offset in its frame
	VectorSubtract( vr.offhandposition, vr.hmdposition, off );
	CL_VRStageToWorld( off, headYaw, offOrg );
	VectorAdd( rd->vieworg, offOrg, offOrg );
	offAng[PITCH] = vr.offhandangles[PITCH];
	offAng[YAW] = AngleNormalize180( vr.offhandangles[YAW] + ( headYaw - vr.hmdorientation[YAW] ) );
	offAng[ROLL] = vr.offhandangles[ROLL];
	AnglesToAxis( offAng, oAxis );
	{	// turn UrT's foregrip hand pose into a controller grip (tunable, controller frame)
		vec3_t adj, adjAxis[3], tmp[3];
		int k;
		adj[PITCH] = Cvar_VariableValue( "vr_urtLeftArmPitch" );
		adj[YAW] = Cvar_VariableValue( "vr_urtLeftArmYaw" );
		adj[ROLL] = Cvar_VariableValue( "vr_urtLeftArmRoll" );
		AnglesToAxis( adj, adjAxis );
		for ( k = 0; k < 3; k++ ) {
			VectorScale( oAxis[0], adjAxis[k][0], tmp[k] );
			VectorMA( tmp[k], adjAxis[k][1], oAxis[1], tmp[k] );
			VectorMA( tmp[k], adjAxis[k][2], oAxis[2], tmp[k] );
		}
		for ( k = 0; k < 3; k++ ) VectorCopy( tmp[k], oAxis[k] );
	}
	VectorMA( offOrg, Cvar_VariableValue( "vr_urtLeftHandForward" ), oAxis[0], offOrg );
	VectorMA( offOrg, Cvar_VariableValue( "vr_urtLeftHandLeft" ), oAxis[1], offOrg );
	VectorMA( offOrg, Cvar_VariableValue( "vr_urtLeftHandUp" ), oAxis[2], offOrg );

	// what the hand points at; the game shoots from the eye, so aim the eye at that point
	VectorCopy( hAxis[0], fwd );
	{	// from the muzzle, so the crosshair sits on the barrel line (unless the muzzle is in a wall)
		vec3_t start;
		VectorCopy( hand, start );
		if ( vrMuzzleValid && Cvar_VariableIntegerValue( "vr_urtAlignBarrel" ) ) {
			vec3_t m;
			VectorCopy( hand, m );
			for ( i = 0; i < 3; i++ ) VectorMA( m, vrMuzzleLocal[i], hAxis[i], m );
			CM_BoxTrace( &tr, hand, m, zero, zero, 0, CONTENTS_SOLID, qfalse );
			if ( tr.fraction >= 1.0f && !tr.startsolid ) VectorCopy( m, start );
		}
		VectorMA( start, 8192, fwd, end );
		CM_BoxTrace( &tr, start, end, zero, zero, 0, CONTENTS_SOLID | CONTENTS_BODY, qfalse );
	}
	VectorSubtract( tr.endpos, in->vieworg, dir );
	if ( VectorLength( dir ) > 64.0f ) {
		vectoangles( dir, aimEye );
	} else {
		VectorCopy( aim, aimEye );
	}
	CL_VRMeasurePitchCorrection();
	// Scoped: look through the scope, i.e. along the line the shot takes (eye -> aim point), so
	// the view and UrT's scope overlay follow the hand. Roll stays level.
	cl_vrScopeView = vr_urtZoom > 1.01f && Cvar_VariableIntegerValue( "vr_urtScopeFollowsHand" );
	if ( cl_vrScopeView ) {
		// from the camera itself (it can sit a little above/below the game's eye) to the point
		// the shot hits, so the view centre is exactly on it
		vec3_t sa, sd;
		VectorSubtract( tr.endpos, rd->vieworg, sd );
		if ( VectorLength( sd ) > 64.0f ) {
			vectoangles( sd, sa );
		} else {
			VectorCopy( aimEye, sa );
		}
		sa[ROLL] = 0;
		AnglesToAxis( sa, rd->viewaxis );
	}
	cl_vrAimYawDelta = AngleNormalize180( aimEye[YAW] - headYaw );
	cl_vrAimPitch = AngleNormalize180( aimEye[PITCH] );
	cl_vrAimTime = cls.realtime;
	cl_vrAimActive = qtrue;

	// weapon/hands: from the game's eye frame into the hand frame
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( in->viewaxis[i], gAxis[i] );
	}
	gunScale = Cvar_VariableValue( "vr_urtGunScale" );
	if ( gunScale <= 0.05f ) gunScale = 1.0f;
	// Urban Terror squashes its whole view weapon (arms included) to ~61% along the view direction,
	// which is invisible on a flat screen but makes the gun short and flat once it turns with the
	// controller. Undo it: stretch axes and part offsets along the view forward (gAxis[0]) by 1/s,
	// about the arms model's origin (the base the parts are placed from).
	if ( Cvar_VariableIntegerValue( "vr_urtUnsquash" ) && vrNumFpEnts > 0 ) {
		float s = 1.0f;
		vec3_t base;
		int k;
		VectorCopy( vrFpEnts[0].origin, base );
		for ( i = 0; i < vrNumFpEnts; i++ ) {
			refEntity_t *e = &vrFpEnts[i];
			if ( e->reType != RT_MODEL ) continue;
			if ( CL_VRIsHoldModel( e->hModel ) ) VectorCopy( e->origin, base );
			for ( k = 0; k < 3; k++ ) {
				float l = VectorLength( e->axis[k] );
				if ( l > 0.2f && l < s ) s = l;
			}
		}
		if ( s < 0.98f ) {
			float st = 1.0f / s - 1.0f;
			vec3_t bd;
			VectorSubtract( base, in->vieworg, bd );
			unsquashS = s;
			unsquashBaseF = DotProduct( bd, gAxis[0] );
			for ( i = 0; i < vrNumFpEnts; i++ ) {
				refEntity_t *e = &vrFpEnts[i];
				vec3_t d;
				if ( e->reType != RT_MODEL ) continue;
				for ( k = 0; k < 3; k++ ) {
					VectorMA( e->axis[k], st * DotProduct( e->axis[k], gAxis[0] ), gAxis[0], e->axis[k] );
				}
				VectorSubtract( e->origin, base, d );
				VectorMA( e->origin, st * DotProduct( d, gAxis[0] ), gAxis[0], e->origin );
				VectorSubtract( e->oldorigin, base, d );
				VectorMA( e->oldorigin, st * DotProduct( d, gAxis[0] ), gAxis[0], e->oldorigin );
			}
		}
	}
	grip[0] = Cvar_VariableValue( "vr_urtGunForward" );
	grip[1] = Cvar_VariableValue( "vr_urtGunLeft" );
	grip[2] = Cvar_VariableValue( "vr_urtGunUp" );
	// the grip point was tuned on the squashed model: stretch it with the gun, so the handle
	// stays in the hand
	if ( unsquashS < 0.98f ) {
		grip[0] = unsquashBaseF + ( grip[0] - unsquashBaseF ) / unsquashS;
	}
	// Better: each weapon's own right hand (palm) goes onto the controller. One fixed grip point
	// for all weapons put the pistols' handle ~0.5 m in front of the hand.
	if ( Cvar_VariableIntegerValue( "vr_urtGripFromHand" ) ) {
		for ( i = 0; i < vrNumFpEnts; i++ ) {
			const refEntity_t *e = &vrFpEnts[i];
			const vrHoldModel_t *hm;
			if ( e->reType != RT_MODEL || !CL_VRIsHoldModel( e->hModel ) ) continue;
			hm = CL_VRHoldModel( vrModelNames[e->hModel] );
			if ( hm && hm->rpivot ) {
				int f0 = e->frame, f1 = e->oldframe, k;
				vec3_t p, w, d;
				if ( f0 < 0 || f0 >= hm->rFrames ) f0 = 0;
				if ( f1 < 0 || f1 >= hm->rFrames ) f1 = f0;
				// palm of the idle pose: don't let reload/raise animations drag the gun about
				for ( k = 0; k < 3; k++ ) p[k] = hm->rpivot[f0][k] * ( 1.0f - e->backlerp ) + hm->rpivot[f1][k] * e->backlerp;
				VectorCopy( e->origin, w );
				for ( k = 0; k < 3; k++ ) VectorMA( w, p[k], e->axis[k], w );
				VectorSubtract( w, in->vieworg, d );
				grip[0] = DotProduct( d, gAxis[0] ) + Cvar_VariableValue( "vr_urtHandForward" );
				grip[1] = DotProduct( d, gAxis[1] ) + Cvar_VariableValue( "vr_urtHandLeft" );
				grip[2] = DotProduct( d, gAxis[2] ) + Cvar_VariableValue( "vr_urtHandUp" );
			}
			break;
		}
	}
	// gun basis: the hand frame, turned so the barrel lies along the aim direction
	for ( i = 0; i < 3; i++ ) VectorCopy( hAxis[i], gunAxis[i] );
	if ( Cvar_VariableIntegerValue( "vr_urtAlignBarrel" ) ) {
		for ( i = 0; i < vrNumFpEnts; i++ ) {
			if ( vrFpEnts[i].reType == RT_MODEL && CL_VRIsHoldModel( vrFpEnts[i].hModel ) ) {
				CL_VRMeasureBarrel( &vrFpEnts[i], gAxis );
				break;
			}
		}
		if ( vrGunDirValid && i < vrNumFpEnts ) {
			int j, k;
			for ( j = 0; j < 3; j++ ) {
				vec3_t ej = { 0, 0, 0 }, r;
				ej[j] = 1.0f;
				CL_VRRotateToX( vrGunDir, ej, r );
				VectorClear( gunAxis[j] );
				for ( k = 0; k < 3; k++ ) VectorMA( gunAxis[j], r[k], hAxis[k], gunAxis[j] );
			}
		}
	}
	for ( i = 0; i < vrNumFpEnts; i++ ) {
		refEntity_t *e = &vrFpEnts[i];
		vec3_t a0, a1, a2, o;
		qboolean holdModel = e->reType == RT_MODEL && CL_VRIsHoldModel( e->hModel );
		if ( holdModel ) {
			// UrT's arms ("*_hold"): right hand with the gun, left arm at the left controller
			if ( !Cvar_VariableIntegerValue( "vr_urtArms" ) ) {
				continue;
			}
		} else if ( Distance( e->origin, in->vieworg ) < Cvar_VariableValue( "vr_urtArmsCutoff" ) &&
				!Cvar_VariableIntegerValue( "vr_urtArms" ) ) {
			continue;
		}
		CL_VRTransformAxis( e->axis[0], gAxis, gunAxis, a0 );
		CL_VRTransformAxis( e->axis[1], gAxis, gunAxis, a1 );
		CL_VRTransformAxis( e->axis[2], gAxis, gunAxis, a2 );
		// UrT's view model is sized for a flat screen (about 1 m long in VR): shrink it around
		// the grip, as QuakeQuest does with its weaponScale
		VectorScale( a0, gunScale, e->axis[0] );
		VectorScale( a1, gunScale, e->axis[1] );
		VectorScale( a2, gunScale, e->axis[2] );
		if ( gunScale != 1.0f ) {
			e->nonNormalizedAxes = qtrue;
		}
		CL_VRTransformPoint( e->origin, in->vieworg, gAxis, grip, hand, gunAxis, o );
		VectorSubtract( o, hand, o );
		VectorMA( hand, gunScale, o, e->origin );
		CL_VRTransformPoint( e->oldorigin, in->vieworg, gAxis, grip, hand, gunAxis, o );
		VectorSubtract( o, hand, o );
		VectorMA( hand, gunScale, o, e->oldorigin );
		CL_VRTransformPoint( e->lightingOrigin, in->vieworg, gAxis, grip, hand, gunAxis, o );
		VectorCopy( o, e->lightingOrigin );
		if ( holdModel ) {
			orientation_t mt;
			if ( re.LerpTag( &mt, e->hModel, e->oldframe, e->frame, 1.0f - e->backlerp, "tag_flash" ) ) {
				// muzzle in the hand frame, for next frame's aim trace
				vec3_t m, d;
				int k;
				VectorCopy( e->origin, m );
				for ( k = 0; k < 3; k++ ) VectorMA( m, mt.origin[k], e->axis[k], m );
				VectorSubtract( m, hand, d );
				for ( k = 0; k < 3; k++ ) vrMuzzleLocal[k] = DotProduct( d, hAxis[k] );
				vrMuzzleValid = VectorLength( d ) < 64.0f;
			}
			if ( vr.weapon_stabilised ) {
				// left grip held near the gun: the left hand holds the front grip, in the arms
				// model's own pose (two-handed aiming, vr_input.c)
				e->vrHandSide = 0;
			} else {
				e->vrHandSide = 2;	// everything but the left hand
				CL_VRAddLeftArm( e, offOrg, oAxis );
			}
		}
		re.AddRefEntityToScene( e );
	}
	vrNumFpEnts = 0;

	// 3D crosshair where the hand aims, pulled slightly back toward the hand so it is not
	// buried in the wall; the size grows with the distance so it keeps the same angular size
	if ( Cvar_VariableIntegerValue( "vr_urtCrosshair" ) && !vr.weapon_zoomed && !cl_vrScopeView ) {
		refEntity_t ch;
		vec3_t back;
		float dist;
		VectorSubtract( tr.endpos, rd->vieworg, back );
		dist = VectorNormalize( back );
		if ( dist > 8.0f ) {
			Com_Memset( &ch, 0, sizeof( ch ) );
			ch.reType = RT_SPRITE;
			ch.renderfx = RF_DEPTHHACK;
			ch.customShader = re.RegisterShader( "sprites/vr/crosshair" );	// handles change on vid_restart
			VectorMA( tr.endpos, -2.0f, back, ch.origin );
			ch.radius = dist * tan( DEG2RAD( Cvar_VariableValue( "vr_urtCrosshairSize" ) * 0.5f ) );
			ch.shaderRGBA[0] = 0xff;
			ch.shaderRGBA[1] = 0xff;
			ch.shaderRGBA[2] = 0xff;
			ch.shaderRGBA[3] = 0xff;
			re.AddRefEntityToScene( &ch );
		}
	}

	// laser sight
	if ( Cvar_VariableIntegerValue( "vr_urtLaser" ) && !cl_vrScopeView ) {
		qhandle_t laserShader;
		refEntity_t laser;
		laserShader = re.RegisterShader( "railCore" );	// handles change on vid_restart
		if ( !laserShader ) laserShader = cls.whiteShader;
		Com_Memset( &laser, 0, sizeof( laser ) );
		laser.reType = RT_LASERSIGHT;
		laser.customShader = laserShader;
		VectorCopy( hand, laser.origin );
		VectorCopy( tr.endpos, laser.oldorigin );
		laser.radius = 0.5f;
		AxisClear( laser.axis );
		laser.shaderRGBA[0] = 0xff;
		laser.shaderRGBA[1] = 0x00;
		laser.shaderRGBA[2] = 0x00;
		laser.shaderRGBA[3] = 0x60;
		re.AddRefEntityToScene( &laser );
	}
}

/*
====================
VR HUD for Urban Terror (as ioq3quest's own cgame does it)

UrT's cgame draws its HUD into the whole eye image, whose corners are outside the
Quest's view. While playing, the 2D drawn after the world scene is sent into the
640x480 HUD buffer instead (coordinates scaled back from the eye-buffer size), and that
buffer is shown as a sprite floating in front of the player that smoothly follows the head.
====================
*/
static qboolean cl_vrHudBuffer;

static qboolean CL_VRHudEnabled( void ) {
	int status = Cvar_VariableIntegerValue( "vr_hudDrawStatus" );
	return CL_VRAimEnabled() && status != 0 && status != 2 && !vr.weapon_zoomed;
}

static void CL_VRHudScale( float *x, float *y, float *w, float *h ) {
	float sx = 640.0f / (float)cls.glconfig.vidWidth;
	float sy = 480.0f / (float)cls.glconfig.vidHeight;
	*x *= sx; *w *= sx;
	*y *= sy; *h *= sy;
}

static void CL_VRAddHudSprite( const refdef_t *rd ) {
	qhandle_t hudShader;
	static float yawX = 0.0f, yawY = 1.0f, prevPitch = 0.0f;
	refEntity_t ent;
	vec3_t angles, forward, right, up, endpos;
	float scale = Cvar_VariableValue( "vr_worldscaleScaler" );
	float dist, radius, viewYaw;

	hudShader = re.RegisterShader( "sprites/vr/hud" );	// handles change on vid_restart
	if ( scale <= 0.0f ) scale = 1.0f;
	dist = ( Cvar_VariableValue( "vr_hudDepth" ) + 3 ) * 3 * scale;
	// vr_urtHudScale > 1 makes the HUD picture wider, so UrT's corner elements sit further out
	radius = dist / 3.0f * ( Cvar_VariableValue( "vr_urtHudScale" ) > 0.2f ? Cvar_VariableValue( "vr_urtHudScale" ) : 1.0f );

	viewYaw = SHORT2ANGLE( cl.snap.ps.delta_angles[YAW] ) + ( cl.viewangles[YAW] - vr.hmdorientation[YAW] );
	yawX = 0.95f * yawX + 0.05f * cos( DEG2RAD( vr.hmdorientation[YAW] ) );
	yawY = 0.95f * yawY + 0.05f * sin( DEG2RAD( vr.hmdorientation[YAW] ) );
	angles[YAW] = viewYaw + RAD2DEG( atan2( yawY, yawX ) );
	angles[PITCH] = 0.95f * prevPitch + 0.05f * vr.hmdorientation[PITCH];
	prevPitch = angles[PITCH];
	angles[ROLL] = 0;
	if ( cl_vrScopeView ) {
		// scoped: the HUD (with UrT's scope overlay) sits exactly in front of the scope view, so
		// the reticle centre is where the shot goes
		vectoangles( rd->viewaxis[0], angles );
		angles[ROLL] = 0;
		// reticle at "infinity", like a real scope: at the HUD's ~0.5 m each eye sees it a few
		// degrees off the view centre (stereo parallax), so shots landed beside it. Same angular
		// size, far away.
		{
			float far = Cvar_VariableValue( "vr_urtScopeReticleDist" );
			if ( far > 1.0f ) {
				dist *= far;
				radius *= far;
			}
		}
		yawX = cos( DEG2RAD( vr.hmdorientation[YAW] ) );
		yawY = sin( DEG2RAD( vr.hmdorientation[YAW] ) );
		prevPitch = vr.hmdorientation[PITCH];
	}
	AngleVectors( angles, forward, right, up );
	VectorMA( rd->vieworg, dist, forward, endpos );
	if ( !cl_vrScopeView ) {	// scoped: the reticle centre must stay on the view centre
		VectorMA( endpos, Cvar_VariableValue( "vr_hudYOffset" ) / 20, up, endpos );
	}

	Com_Memset( &ent, 0, sizeof( ent ) );
	ent.reType = RT_SPRITE;
	ent.renderfx = RF_DEPTHHACK | RF_FIRST_PERSON;
	VectorCopy( endpos, ent.origin );
	ent.radius = radius;
	ent.invert = qtrue;
	ent.customShader = hudShader;
	re.AddRefEntityToScene( &ent );
}

intptr_t CL_CgameSystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case CG_PRINT:
		Com_Printf( "%s", (const char*)VMA(1) );
		return 0;
	case CG_ERROR:
		Com_Error( ERR_DROP, "%s", (const char*)VMA(1) );
		return 0;
	case CG_MILLISECONDS:
		return Sys_Milliseconds();
	case CG_CVAR_REGISTER:
		Cvar_Register( VMA(1), VMA(2), VMA(3), args[4] ); 
		return 0;
	case CG_CVAR_UPDATE:
		Cvar_Update( VMA(1) );
		return 0;
	case CG_CVAR_SET:
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;
	case CG_CVAR_VARIABLESTRINGBUFFER:
		Cvar_VariableStringBuffer( VMA(1), VMA(2), args[3] );
		return 0;
	case CG_ARGC:
		return Cmd_Argc();
	case CG_ARGV:
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
		return 0;
	case CG_ARGS:
		Cmd_ArgsBuffer( VMA(1), args[2] );
		return 0;
	case CG_FS_FOPENFILE:
		return FS_FOpenFileByMode( VMA(1), VMA(2), args[3] );
	case CG_FS_READ:
		FS_Read( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_WRITE:
		FS_Write( VMA(1), args[2], args[3] );
		return 0;
	case CG_FS_FCLOSEFILE:
		FS_FCloseFile( args[1] );
		return 0;
	case CG_FS_SEEK:
		return FS_Seek( args[1], args[2], args[3] );
	case CG_SENDCONSOLECOMMAND:
		Cbuf_AddText( VMA(1) );
		return 0;
	case CG_ADDCOMMAND:
		CL_AddCgameCommand( VMA(1) );
		return 0;
	case CG_REMOVECOMMAND:
		Cmd_RemoveCommandSafe( VMA(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand(VMA(1), qfalse);
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
//		Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
// We can't call Com_EventLoop here, a restart will crash and this _does_ happen
// if there is a map change while we are downloading at pk3.
// ZOID
		SCR_UpdateScreen();
		return 0;
	case CG_CM_LOADMAP:
		CL_CM_LoadMap( VMA(1) );
		return 0;
	case CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
		return CM_InlineModel( args[1] );
	case CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qfalse );
	case CG_CM_TEMPCAPSULEMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qtrue );
	case CG_CM_POINTCONTENTS:
		return CM_PointContents( VMA(1), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( VMA(1), args[2], VMA(3), VMA(4) );
	case CG_CM_BOXTRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
		return 0;
	case CG_CM_CAPSULETRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qfalse );
		return 0;
	case CG_CM_TRANSFORMEDCAPSULETRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qtrue );
		return 0;
	case CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], VMA(2), VMA(3), args[4], VMA(5), args[6], VMA(7) );
	case CG_S_STARTSOUND:
		S_StartSound( VMA(1), args[2], args[3], args[4] );
		return 0;
	case CG_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds(args[1]);
		return 0;
	case CG_S_ADDLOOPINGSOUND:
		S_AddLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_ADDREALLOOPINGSOUND:
		S_AddRealLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_STOPLOOPINGSOUND:
		S_StopLoopingSound( args[1] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION:
		S_UpdateEntityPosition( args[1], VMA(2) );
		return 0;
	case CG_S_RESPATIALIZE:
		S_Respatialize( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
		return S_RegisterSound( VMA(1), args[2] );
	case CG_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMA(1), VMA(2) );
		return 0;
	case CG_R_LOADWORLDMAP:
		re.LoadWorld( VMA(1) );
		return 0; 
	case CG_R_REGISTERMODEL: {
		qhandle_t h = re.RegisterModel( VMA(1) );
		if ( h > 0 && h < MAX_VR_MODEL_NAMES ) {
			Q_strncpyz( vrModelNames[h], VMA(1), sizeof( vrModelNames[h] ) );
		}
		return h;
	}
	case CG_R_REGISTERSKIN:
		return re.RegisterSkin( VMA(1) );
	case CG_R_REGISTERSHADER:
		return re.RegisterShader( VMA(1) );
	case CG_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( VMA(1) );
	case CG_R_REGISTERFONT:
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;
	case CG_R_CLEARSCENE:
		vrNumFpEnts = 0;	// drop first-person entities held back for controller aiming
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE: {
		refEntity_t ent;
		CL_RefEntityFromLegacy( VMA(1), &ent );
		if ( ( ent.renderfx & RF_FIRST_PERSON ) && CL_VRAimEnabled() ) {
			CL_VRHoldEntity( &ent );
			return 0;
		}
		re.AddRefEntityToScene( &ent );
		return 0;
	}
	case CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;
	case CG_R_ADDPOLYSTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), args[4] );
		return 0;
	case CG_R_LIGHTFORPOINT:
		return re.LightForPoint( VMA(1), VMA(2), VMA(3), VMA(4) );
	case CG_R_ADDLIGHTTOSCENE:
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_ADDADDITIVELIGHTTOSCENE:
		re.AddAdditiveLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_RENDERSCENE: {
		refdef_t rd;
		CL_RefdefFromLegacy( VMA(1), &rd );
		if ( rd.rdflags & RDF_NOWORLDMODEL ) {
			if ( cl_vrHudBuffer ) {
				float x = rd.x, y = rd.y, w = rd.width, h = rd.height;
				CL_VRHudScale( &x, &y, &w, &h );
				rd.x = x; rd.y = y; rd.width = w; rd.height = h;
				rd.isHUD = qtrue;
			}
			// model-only scene (HUD/menu models): draw held entities where the game put them
			int i;
			for ( i = 0; i < vrNumFpEnts; i++ ) {
				re.AddRefEntityToScene( &vrFpEnts[i] );
			}
			vrNumFpEnts = 0;
		} else {
			CL_VRApplyView( VMA(1), &rd );
			// The VR eye projection always covers the whole eye buffer. Urban Terror sizes its
			// 3D view from its own settings (it came out at ~92% in the top-left corner), which
			// shrinks the world and leaves black borders, so the world view always fills the eye.
			rd.x = 0;
			rd.y = 0;
			rd.width = cls.glconfig.vidWidth;
			rd.height = cls.glconfig.vidHeight;
			// The renderer builds its culling frustum from fov_x/fov_y; the eyes see more than
			// UrT's cg_fov, so use the headset's field of view (as ioq3quest's own cgame does).
			if ( vr.fov_x > 1.0f && vr.fov_y > 1.0f ) {
				rd.fov_x = vr.fov_x;
				rd.fov_y = vr.fov_y;
			}
		}
		if ( !( rd.rdflags & RDF_NOWORLDMODEL ) && CL_VRHudEnabled() ) {
			CL_VRAddHudSprite( &rd );
		}
		re.RenderScene( &rd );
		if ( !( rd.rdflags & RDF_NOWORLDMODEL ) && !cl_vrHudBuffer && CL_VRHudEnabled() ) {
			// everything the cgame draws in 2D from here on is its HUD
			re.HUDBufferStart( qtrue );
			cl_vrHudBuffer = qtrue;
		}
		return 0;
	}
	case CG_R_SETCOLOR:
		re.SetColor( VMA(1) );
		return 0;
	case CG_R_DRAWSTRETCHPIC: {
		float x = VMF(1), y = VMF(2), w = VMF(3), h = VMF(4);
		if ( cl_vrHudBuffer ) {
			CL_VRHudScale( &x, &y, &w, &h );
		}
		re.DrawStretchPic( x, y, w, h, VMF(5), VMF(6), VMF(7), VMF(8), args[9] );
		return 0;
	}
	case CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;
	case CG_R_LERPTAG:
		return re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
	case CG_GETGLCONFIG:
		CL_GetGlconfig( VMA(1) );
		return 0;
	case CG_GETGAMESTATE:
		CL_GetGameState( VMA(1) );
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( VMA(1), VMA(2) );
		return 0;
	case CG_GETSNAPSHOT:
		return CL_GetSnapshot( args[1], VMA(2) );
	case CG_GETSERVERCOMMAND:
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], VMA(2) );
	case CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], VMF(2) );
		return 0;
	case CG_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();
  case CG_KEY_ISDOWN:
		return Key_IsDown( args[1] );
  case CG_KEY_GETCATCHER:
		return Key_GetCatcher();
  case CG_KEY_SETCATCHER:
		// Don't allow the cgame module to close the console
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ) );
    return 0;
  case CG_KEY_GETKEY:
		return Key_GetKey( VMA(1) );



	case CG_MEMSET:
		Com_Memset( VMA(1), args[2], args[3] );
		return 0;
	case CG_MEMCPY:
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return 0;
	case CG_STRNCPY:
		strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	case CG_SIN:
		return FloatAsInt( sin( VMF(1) ) );
	case CG_COS:
		return FloatAsInt( cos( VMF(1) ) );
	case CG_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );
	case CG_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );
	case CG_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );
	case CG_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );
	case CG_ACOS:
		return FloatAsInt( Q_acos( VMF(1) ) );

	case CG_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case CG_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case CG_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case CG_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case CG_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case CG_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;

	case CG_REAL_TIME:
		return Com_RealTime( VMA(1) );
	case CG_SNAPVECTOR:
		Q_SnapVector(VMA(1));
		return 0;

	case CG_CIN_PLAYCINEMATIC:
	  return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6]);

	case CG_CIN_STOPCINEMATIC:
	  return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
	  return CIN_RunCinematic(args[1]);

	case CG_CIN_DRAWCINEMATIC:
	  CIN_DrawCinematic(args[1]);
	  return 0;

	case CG_CIN_SETEXTENTS:
	  CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
	  return 0;

	case CG_R_REMAP_SHADER:
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

/*
	case CG_LOADCAMERA:
		return loadCamera(VMA(1));

	case CG_STARTCAMERA:
		startCamera(args[1]);
		return 0;

	case CG_GETCAMERAINFO:
		return getCameraInfo(args[1], VMA(2), VMA(3));
*/
	case CG_GET_ENTITY_TOKEN:
		return re.GetEntityToken( VMA(1), args[2] );
	case CG_R_INPVS:
		return re.inPVS( VMA(1), VMA(2) );
	case CG_HAPTICEVENT:
	    VR_HapticEvent( VMA(1), args[2], args[3], args[4], VMF(5), VMF(6) );
		return 0;
	case CG_R_HUDBUFFER_START:
		re.HUDBufferStart(args[1]);
		return 0;
	case CG_R_HUDBUFFER_END:
		re.HUDBufferEnd();
		return 0;


	default:
	        assert(0);
		Com_Error( ERR_DROP, "Bad cgame system trap: %ld", (long int) args[0] );
	}
	return 0;
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;
	vmInterpret_t		interpret;

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// load the dll or bytecode
	interpret = Cvar_VariableValue("vm_cgame");
	if(cl_connectedToPureServer)
	{
		// if sv_pure is set we only allow qvms to be loaded
		if(interpret != VMI_COMPILED && interpret != VMI_BYTECODE)
			interpret = VMI_COMPILED;
	}

	cgvm = VM_Create( "cgame", CL_CgameSystemCalls, interpret );
	if ( !cgvm ) {
		Com_Error( ERR_DROP, "VM_Create on cgame failed" );
	}
	clc.state = CA_LOADING;

	//Pass the vr client info in on the init
	long val = (long)(&vr);
	int *ptr = (int*)(&val);	 //HACK!!

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	VM_Call( cgvm, CG_INIT, clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum, ptr[0], ptr[1] );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	clc.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
	if (!Sys_LowPhysicalMemory()) {
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/
qboolean CL_GameCommand( void ) {
	if ( !cgvm ) {
		return qfalse;
	}

	return VM_Call( cgvm, CG_CONSOLE_COMMAND );
}



/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	VM_Call( cgvm, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying );
	if ( cl_vrHudBuffer ) {
		re.HUDBufferEnd();
		cl_vrHudBuffer = qfalse;
	}
	VM_Debug( 0 );
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.realtime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale modified frametimes
		if ( com_timescale->value == 0 || com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}
	clc.state = CA_ACTIVE;

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.realtime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cvar_Set( "activeAction", "" );
	}

#ifdef USE_MUMBLE
	if ((cl_useMumble->integer) && !mumble_islinked()) {
		int ret = mumble_link(CLIENT_WINDOW_TITLE);
		Com_Printf("Mumble: Linking to Mumble application %s\n", ret==0?"ok":"failed");
	}
#endif

#ifdef USE_VOIP
	if (!clc.voipCodecInitialized) {
		int i;
		int error;

		clc.opusEncoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_VOIP, &error);

		if ( error ) {
			Com_DPrintf("VoIP: Error opus_encoder_create %d\n", error);
			return;
		}

		for (i = 0; i < MAX_CLIENTS; i++) {
			clc.opusDecoder[i] = opus_decoder_create(48000, 1, &error);
			if ( error ) {
				Com_DPrintf("VoIP: Error opus_decoder_create(%d) %d\n", i, error);
				return;
			}
			clc.voipIgnore[i] = qfalse;
			clc.voipGain[i] = 1.0f;
		}
		clc.voipCodecInitialized = qtrue;
		clc.voipMuteAll = qfalse;
		Cmd_AddCommand ("voip", CL_Voip_f);
		Cvar_Set("cl_voipSendTarget", "spatial");
		Com_Memset(clc.voipTargets, ~0, sizeof(clc.voipTargets));
	}
#endif
}

/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	// getting a valid frame message ends the connection process
	if ( clc.state != CA_ACTIVE ) {
		if ( clc.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( clc.state != CA_ACTIVE ) {
			return;
		}
	}	

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime < cl.oldFrameServerTime ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;


	// get our current view of time

	if ( clc.demoplaying && cl_freezeDemo->integer ) {
		// cl_freezeDemo is used to lock a demo in place for single frame advances

	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better 
		// smoothness or better responsiveness.
		int tn;
		
		tn = cl_timeNudge->integer;
		if (tn<-30) {
			tn = -30;
		} else if (tn>30) {
			tn = 30;
		}

		cl.serverTime = cls.realtime + cl.serverTimeDelta - tn;

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime < cl.oldServerTime ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		if ( cls.realtime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definitely
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( cl_timedemo->integer ) {
		int now = Sys_Milliseconds( );
		int frameDuration;

		if (!clc.timeDemoStart) {
			clc.timeDemoStart = clc.timeDemoLastFrame = now;
			clc.timeDemoMinDuration = INT_MAX;
			clc.timeDemoMaxDuration = 0;
		}

		frameDuration = now - clc.timeDemoLastFrame;
		clc.timeDemoLastFrame = now;

		// Ignore the first measurement as it'll always be 0
		if( clc.timeDemoFrames > 0 )
		{
			if( frameDuration > clc.timeDemoMaxDuration )
				clc.timeDemoMaxDuration = frameDuration;

			if( frameDuration < clc.timeDemoMinDuration )
				clc.timeDemoMinDuration = frameDuration;

			// 255 ms = about 4fps
			if( frameDuration > UCHAR_MAX )
				frameDuration = UCHAR_MAX;

			clc.timeDemoDurations[ ( clc.timeDemoFrames - 1 ) %
				MAX_TIMEDEMO_DURATIONS ] = frameDuration;
		}

		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	while ( cl.serverTime >= cl.snap.serverTime ) {
		// feed another messag, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( clc.state != CA_ACTIVE ) {
			return;		// end of demo
		}
	}

}



