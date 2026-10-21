#ifndef __VR_BASE
#define __VR_BASE

#if __ANDROID__

#include "vr_types.h"

engine_t* VR_Init( ovrJava java );
void VR_InitCvars( void );
void VR_Destroy( engine_t* engine );
void VR_EnterVR( engine_t* engine, ovrJava java );
void VR_LeaveVR( engine_t* engine );

engine_t* VR_GetEngine( void );
int VR_useScreenLayer( void );
int VR_isPauseable( void );

float radians(float deg);

void VR_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight );

#endif


// Quake 3 marks "spectating, following another player" with pm_flags bit 4096 (PMF_FOLLOW).
// Urban Terror's closed game code reuses that bit for something else, which made the engine
// show gameplay on the flat follow-cam screen, turn A into "team spectator" and X into a
// follow-mode switch. The engine can't tell UrT's follow state, so these Q3 paths are off.
#define VR_PS_FOLLOWING(ps)	(0)

#endif
