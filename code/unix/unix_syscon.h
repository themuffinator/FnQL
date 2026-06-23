#ifndef __UNIX_SYSCON_H__
#define __UNIX_SYSCON_H__

#include "../qcommon/q_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

void Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean useXYpos );
void Sys_DestroyConsole( void );
void Sys_ConsoleFrame( void );
void Conbuf_AppendText( const char *msg );
char *Sys_WindowConsoleInput( void );
qboolean Sys_ConsoleVideoActive( void );

#if defined(USE_SDL_SYSCON) && !defined(DEDICATED)
#include <SDL3/SDL.h>
qboolean Sys_ConsoleHandleEvent( const SDL_Event *event );
#endif

#ifdef __cplusplus
}
#endif

#endif
