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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"
#include "../vr/vr_clientinfo.h"

extern vr_clientinfo_t vr;
extern cvar_t *vr_hudDrawStatus;

qboolean	scr_initialized;		// ready to draw

cvar_t		*cl_timegraph;
cvar_t		*cl_debuggraph;
cvar_t		*cl_graphheight;
cvar_t		*cl_graphscale;
cvar_t		*cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t	hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	float	xscale;
	float	yscale;

#if 0
		// adjust for wide screens
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			*x += 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * 640 / 480 ) );
		}
#endif

	// scale for screen sizes
	xscale = cls.glconfig.vidWidth / 640.0;
	yscale = cls.glconfig.vidHeight / 480.0;

	if (vr.virtual_screen || vr_hudDrawStatus->integer != 2) {
		if (x) {
			*x *= xscale;
		}
		if (y) {
			*y *= yscale;
		}
		if (w) {
			*w *= xscale;
		}
		if (h) {
			*h *= yscale;
		}
	} else {
		float screenXScale = xscale / 2.75f;
		float screenYScale = yscale / 2.25f;

		if (x) {
			*x *= screenXScale;
            *x += (cls.glconfig.vidWidth - (640 * screenXScale)) / 2.0f;
		}
		if (y) {
			*y *= screenYScale;
            *y += (cls.glconfig.vidHeight - (480 * screenYScale)) / 2.0f;
		}
		if (w) {
			*w *= screenXScale;
		}
		if (h) {
			*h *= screenYScale;
		}
	}
}

/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	re.SetColor( color );

	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

	re.SetColor( NULL );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}



/*
** SCR_DrawChar
** chars are drawn at 640*480 virtual screen size
*/
static void SCR_DrawChar( int x, int y, float size, int ch ) {
	int row, col;
	float frow, fcol;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -size ) {
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;
	SCR_AdjustFrom640( &ax, &ay, &aw, &ah );

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}

/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -SMALLCHAR_HEIGHT ) {
		return;
	}

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, SMALLCHAR_WIDTH, SMALLCHAR_HEIGHT,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}


/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0;
	color[3] = setColor[3];
	re.SetColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( !noColorEscape && Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx+2, y+2, size, *s );
		xx += size;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				Com_Memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawChar( xx, y, size, *s );
		xx += size;
		s++;
	}
	re.SetColor( NULL );
}


void SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape ) {
	float	color[4];

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qfalse, noColorEscape );
}

void SCR_DrawBigStringColor( int x, int y, const char *s, vec4_t color, qboolean noColorEscape ) {
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color, qtrue, noColorEscape );
}


/*
==================
SCR_DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.
==================
*/
void SCR_DrawSmallStringExt( int x, int y, const char *string, float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				Com_Memcpy( color, g_color_table[ColorIndex(*(s+1))], sizeof( color ) );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawSmallChar( xx, y, *s );
		xx += SMALLCHAR_WIDTH;
		s++;
	}
	re.SetColor( NULL );
}



/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}

/*
** SCR_GetBigStringWidth
*/ 
int	SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * BIGCHAR_WIDTH;
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording
=================
*/
void SCR_DrawDemoRecording( void ) {
	char	string[1024];
	int		pos;

	if ( !clc.demorecording ) {
		return;
	}
	if ( clc.spDemoRecording ) {
		return;
	}

	pos = FS_FTell( clc.demofile );
	sprintf( string, "RECORDING %s: %ik", clc.demoName, pos / 1024 );

	SCR_DrawStringExt( 320 - strlen( string ) * 4, 20, 8, string, g_color_table[7], qtrue, qfalse );
}


#ifdef USE_VOIP
/*
=================
SCR_DrawVoipMeter
=================
*/
void SCR_DrawVoipMeter( void ) {
	char	buffer[16];
	char	string[256];
	int limit, i;

	if (!cl_voipShowMeter->integer)
		return;  // player doesn't want to show meter at all.
	else if (!cl_voipSend->integer)
		return;  // not recording at the moment.
	else if (clc.state != CA_ACTIVE)
		return;  // not connected to a server.
	else if (!clc.voipEnabled)
		return;  // server doesn't support VoIP.
	else if (clc.demoplaying)
		return;  // playing back a demo.
	else if (!cl_voip->integer)
		return;  // client has VoIP support disabled.

	limit = (int) (clc.voipPower * 10.0f);
	if (limit > 10)
		limit = 10;

	for (i = 0; i < limit; i++)
		buffer[i] = '*';
	while (i < 10)
		buffer[i++] = ' ';
	buffer[i] = '\0';

	sprintf( string, "VoIP: [%s]", buffer );
	SCR_DrawStringExt( 320 - strlen( string ) * 4, 10, 8, string, g_color_table[7], qtrue, qfalse );
}
#endif




/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

static	int			current;
static	float		values[1024];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value)
{
	values[current] = value;
	current = (current + 1) % ARRAY_LEN(values);
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
	int		a, x, y, w, i, h;
	float	v;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	re.SetColor( g_color_table[0] );
	re.DrawStretchPic(x, y - cl_graphheight->integer, 
		w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );
	re.SetColor( NULL );

	for (a=0 ; a<w ; a++)
	{
		i = (ARRAY_LEN(values)+current-1-(a % ARRAY_LEN(values))) % ARRAY_LEN(values);
		v = values[i];
		v = v * cl_graphscale->integer + cl_graphshift->integer;
		
		if (v < 0)
			v += cl_graphheight->integer * (1+(int)(-v / cl_graphheight->integer));
		h = (int)v % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = qtrue;
}


//=======================================================


/*
=================================================================================

VR virtual keyboard

Quest/PICO have no keyboard. While Urban Terror's menu edits a text field (player name, chat,
server password...) a keyboard is drawn on the lower part of the menu screen; the laser
pointer clicks its keys, which are sent to the menu as typed characters.

=================================================================================
*/
int vr_menuPointerX = -1, vr_menuPointerY = -1, vr_menuPointerTime;
int cl_vrKbdEditTime;

#define KBD_X 20.0f
#define KBD_Y 274.0f
#define KBD_W 600.0f
#define KBD_ROWH 36.0f
#define KBD_GAP 3.0f
#define KBD_ROWS 5

enum { KK_CHAR, KK_SHIFT, KK_SPACE, KK_CLEAR, KK_BACK, KK_DONE };
typedef struct { int type; char lower, upper; float units; } kbdKey_t;

static const char *kbdRows[4][2] = {
	{ "1234567890-", "!@#$%^&*()_" },
	{ "qwertyuiop", "QWERTYUIOP" },
	{ "asdfghjkl", "ASDFGHJKL" },
	{ "zxcvbnm.,", "ZXCVBNM?:" },
};

static qboolean kbdShift = qtrue;		// one-shot: on when the keyboard opens (capital first letter)
static qboolean kbdWasVisible;
static qboolean kbdSwallowUp;
static int kbdBackHeldSince, kbdBackLastRepeat;
static qboolean kbdBackHeld;
static int kbdFlashKey = -1, kbdFlashTime;

static qboolean CL_VRKeyboardVisible( void ) {
	return ( Key_GetCatcher() & KEYCATCH_UI ) && !( Key_GetCatcher() & KEYCATCH_CONSOLE ) &&
		cl_vrKbdEditTime && cls.realtime - cl_vrKbdEditTime < 300 &&
		Cvar_VariableIntegerValue( "vr_keyboard" );
}

// key rect for index i (rows 0-3 = character rows, row 4 = Shift/Space/Back/Done); returns qfalse past the end
static qboolean CL_VRKeyboardKeyRect( int i, int *type, char *ch, float *x, float *y, float *w, float *h ) {
	int row, n = 0;
	float unit = ( KBD_W - 10 * KBD_GAP ) / 11.0f;
	for ( row = 0; row < 4; row++ ) {
		int len = strlen( kbdRows[row][0] );
		if ( i < n + len ) {
			int col = i - n;
			float rowW = len * unit + ( len - 1 ) * KBD_GAP;
			*type = KK_CHAR;
			*ch = kbdRows[row][kbdShift ? 1 : 0][col];
			*x = KBD_X + ( KBD_W - rowW ) * 0.5f + col * ( unit + KBD_GAP );
			*y = KBD_Y + row * ( KBD_ROWH + KBD_GAP );
			*w = unit;
			*h = KBD_ROWH;
			return qtrue;
		}
		n += len;
	}
	{	// bottom row: Shift 2 | Space 4 | Clear 2 | Del 2 | Done 2 (units)
		static const int types[5] = { KK_SHIFT, KK_SPACE, KK_CLEAR, KK_BACK, KK_DONE };
		static const float units[5] = { 2, 4, 2, 2, 2 };
		int j = i - n, c;
		float xx = KBD_X, avail = 11 * unit + 10 * KBD_GAP - 4 * KBD_GAP, sum = 12.0f;
		if ( j < 0 || j > 4 ) return qfalse;
		for ( c = 0; c < j; c++ ) xx += units[c] / sum * avail + KBD_GAP;
		*type = types[j];
		*ch = ' ';
		*x = xx;
		*y = KBD_Y + 4 * ( KBD_ROWH + KBD_GAP );
		*w = units[j] / sum * avail;
		*h = KBD_ROWH;
		return qtrue;
	}
}

static int CL_VRKeyboardHit( void ) {
	int i, type;
	char ch;
	float x, y, w, h;
	if ( vr_menuPointerX < 0 || cls.realtime - vr_menuPointerTime > 200 ) return -1;
	for ( i = 0; CL_VRKeyboardKeyRect( i, &type, &ch, &x, &y, &w, &h ); i++ ) {
		if ( vr_menuPointerX >= x - 1 && vr_menuPointerX < x + w + 1 &&
				vr_menuPointerY >= y - 1 && vr_menuPointerY < y + h + 1 ) {
			return i;
		}
	}
	return -1;
}

static qboolean CL_VRKeyboardPointerInside( void ) {
	return vr_menuPointerX >= 0 && cls.realtime - vr_menuPointerTime <= 200 &&
		vr_menuPointerX >= KBD_X - 6 && vr_menuPointerX <= KBD_X + KBD_W + 6 &&
		vr_menuPointerY >= KBD_Y - 6 && vr_menuPointerY <= KBD_Y + KBD_ROWS * ( KBD_ROWH + KBD_GAP ) + 6;
}

static void CL_VRKeyboardTap( int key ) {
	CL_KeyEvent( key, qtrue, Sys_Milliseconds() );
	CL_KeyEvent( key, qfalse, Sys_Milliseconds() );
}

static void CL_VRKeyboardPress( int i ) {
	extern void VR_Vibrate( int duration, int chan, float intensity );
	extern cvar_t *vr_righthanded;
	int type;
	char ch;
	float x, y, w, h;
	if ( !CL_VRKeyboardKeyRect( i, &type, &ch, &x, &y, &w, &h ) ) return;
	kbdFlashKey = i;
	kbdFlashTime = cls.realtime;
	VR_Vibrate( 15, ( vr_righthanded && vr_righthanded->integer ) ? 2 : 1, 0.5f );
	switch ( type ) {
	case KK_CHAR:
		CL_CharEvent( ch );
		kbdShift = qfalse;
		break;
	case KK_SPACE:
		CL_CharEvent( ' ' );
		break;
	case KK_SHIFT:
		kbdShift = !kbdShift;
		break;
	case KK_CLEAR: {	// caret to the end (End key), then delete everything (UrT fields are at most ~64 chars)
		int n;
		CL_VRKeyboardTap( K_END );
		for ( n = 0; n < 80; n++ ) CL_CharEvent( 8 );
		kbdShift = qtrue;
		break;
	}
	case KK_BACK:
		// the menu's text field deletes on the character Ctrl-H (8), not on the Backspace key
		CL_CharEvent( 8 );
		kbdBackHeld = qtrue;
		kbdBackHeldSince = kbdBackLastRepeat = cls.realtime;
		break;
	case KK_DONE:
		CL_VRKeyboardTap( K_ENTER );
		break;
	}
}

// Called from CL_KeyEvent; qtrue = the event was a click on the keyboard and is used up.
qboolean CL_VRKeyboardKey( int key, qboolean down ) {
	if ( key != K_MOUSE1 ) return qfalse;
	if ( !down ) {
		kbdBackHeld = qfalse;
		if ( kbdSwallowUp ) {
			kbdSwallowUp = qfalse;
			return qtrue;
		}
		return qfalse;
	}
	if ( !CL_VRKeyboardVisible() || !CL_VRKeyboardPointerInside() ) return qfalse;
	{
		int i = CL_VRKeyboardHit();
		if ( i >= 0 ) CL_VRKeyboardPress( i );
	}
	kbdSwallowUp = qtrue;	// clicks in the keyboard's gaps don't reach the menu either
	return qtrue;
}

static void CL_VRKeyboardDraw( void ) {
	static const float panel[4] = { 0.05f, 0.07f, 0.12f, 0.92f };
	static const float keyCol[4] = { 0.22f, 0.26f, 0.34f, 1.0f };
	static const float keyHover[4] = { 0.30f, 0.45f, 0.75f, 1.0f };
	static const float keyFlash[4] = { 0.95f, 0.65f, 0.20f, 1.0f };
	static const float keyOn[4] = { 0.20f, 0.55f, 0.35f, 1.0f };
	static const float white[4] = { 1, 1, 1, 1 };
	int i, type, hover;
	char ch;
	float x, y, w, h;

	if ( !CL_VRKeyboardVisible() ) {
		kbdWasVisible = qfalse;
		kbdBackHeld = qfalse;
		return;
	}
	if ( !kbdWasVisible ) {
		kbdWasVisible = qtrue;
		kbdShift = qtrue;
	}
	// Backspace repeats while held: 450 ms delay, then every 70 ms
	if ( kbdBackHeld && cls.realtime - kbdBackHeldSince > 450 && cls.realtime - kbdBackLastRepeat > 70 ) {
		kbdBackLastRepeat = cls.realtime;
		CL_CharEvent( 8 );
	}

	hover = CL_VRKeyboardHit();
	SCR_FillRect( KBD_X - 8, KBD_Y - 8, KBD_W + 16, KBD_ROWS * ( KBD_ROWH + KBD_GAP ) + 13, panel );
	for ( i = 0; CL_VRKeyboardKeyRect( i, &type, &ch, &x, &y, &w, &h ); i++ ) {
		const float *c = keyCol;
		const char *label;
		char buf[2];
		float size = 16.0f;
		if ( type == KK_SHIFT && kbdShift ) c = keyOn;
		if ( i == hover ) c = keyHover;
		if ( i == kbdFlashKey && cls.realtime - kbdFlashTime < 120 ) c = keyFlash;
		SCR_FillRect( x, y, w, h, c );
		switch ( type ) {
		case KK_SHIFT: label = "Shift"; break;
		case KK_SPACE: label = "Space"; break;
		case KK_CLEAR: label = "Clear"; break;
		case KK_BACK: label = "<- Del"; break;
		case KK_DONE: label = "Done"; break;
		default: buf[0] = ch; buf[1] = 0; label = buf; size = 20.0f; break;
		}
		SCR_DrawStringExt( (int)( x + ( w - strlen( label ) * size ) * 0.5f ), (int)( y + ( h - size ) * 0.5f ),
			size, label, (float *)white, qtrue, qtrue );
	}
	re.SetColor( NULL );
}

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
	qboolean uiFullscreen;

	re.BeginFrame( stereoFrame );

	uiFullscreen = (uivm && VM_Call( uivm, UI_IS_FULLSCREEN ));

	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	if ( uiFullscreen || clc.state < CA_LOADING ) {
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			re.SetColor( g_color_table[0] );
			re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
			re.SetColor( NULL );
		}
	}

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
	if ( uivm && !uiFullscreen ) {
		switch( clc.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad clc.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			S_StopAllSounds();
			VM_Call( uivm, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			// refresh to update the time
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qfalse );
			break;
		case CA_LOADING:
		case CA_PRIMED:
			// draw the game information screen and loading progress
			CL_CGameRendering(stereoFrame);

			// also draw the connection information, so it doesn't
			// flash away too briefly on local or lan games
			// refresh to update the time
			VM_Call( uivm, UI_REFRESH, cls.realtime );
			VM_Call( uivm, UI_DRAW_CONNECT_SCREEN, qtrue );
			break;
		case CA_ACTIVE:
			// always supply STEREO_CENTER as vieworg offset is now done by the engine.
			CL_CGameRendering(stereoFrame);
			SCR_DrawDemoRecording();
#ifdef USE_VOIP
			SCR_DrawVoipMeter();
#endif
			break;
		}
	}

	// the menu draws next
	if ( Key_GetCatcher( ) & KEYCATCH_UI && uivm ) {
		VM_Call( uivm, UI_REFRESH, cls.realtime );
		CL_VRKeyboardDraw();
	}

	// console draws next
	Con_DrawConsole ();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer || cl_debugMove->integer ) {
		SCR_DrawDebugGraph ();
	}
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int	recursive;

	if ( !scr_initialized ) {
		return;				// not initialized yet
	}

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	// If there is no VM, there are also no rendering commands issued. Stop the renderer in
	// that case.
	if( uivm || com_dedicated->integer )
	{
		// XXX
		int in_anaglyphMode = Cvar_VariableIntegerValue("r_anaglyphMode");
		// if running in stereo, we need to draw the frame twice
		if ( qfalse )//cls.glconfig.stereoEnabled || in_anaglyphMode)
		{
			SCR_DrawScreenField( STEREO_LEFT );
			SCR_DrawScreenField( STEREO_RIGHT );
		} else {
			SCR_DrawScreenField( STEREO_CENTER );
		}

		if ( com_speeds->integer ) {
			re.EndFrame( &time_frontend, &time_backend );
		} else {
			re.EndFrame( NULL, NULL );
		}
	}
	
	recursive = 0;
}

