/*
net_ws.h - network shared functions
Copyright (C) 2017 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#ifndef NET_WS_H
#define NET_WS_H

typedef enum
{
	NS_CLIENT,
	NS_SERVER,
	NS_COUNT
} netsrc_t;

#include "netadr.h"

#define MAX_ROUTEABLE_PACKET		1400

#define SPLIT_SIZE			( MAX_ROUTEABLE_PACKET - sizeof( SPLITPACKET ))

extern convar_t	*net_showpackets;
extern convar_t	*net_clockwindow;

void NET_Init( void );
void NET_Shutdown( void );
void NET_Sleep( int msec );
qboolean NET_IsActive( void );
qboolean NET_IsConfigured( void );
void NET_Config( qboolean net_enable );
qboolean NET_IsLocalAddress( netadr_t adr );
char *NET_AdrToString( const netadr_t a );
char *NET_BaseAdrToString( const netadr_t a );
qboolean NET_IsReservedAdr( netadr_t a );
qboolean NET_CompareClassBAdr( netadr_t a, netadr_t b );
qboolean NET_StringToAdr( const char *string, netadr_t *adr );
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b );
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b );
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length );
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to );
void NET_ClearLagData( qboolean bClient, qboolean bServer );

#endif//NET_WS_H