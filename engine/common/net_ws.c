/*
net_ws.c - win network interface
Copyright (C) 2007 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include <winsock.h>
#include <wsipx.h>
#include "common.h"
#include "netchan.h"
#include "mathlib.h"

#define PORT_ANY		-1
#define MAX_LOOPBACK	4
#define MASK_LOOPBACK	(MAX_LOOPBACK - 1)
#define NET_MAX_FRAGMENTS	32

// wsock32.dll exports
static int (_stdcall *pWSACleanup)( void );
static word (_stdcall *pNtohs)( word netshort );
static int (_stdcall *pWSAGetLastError)( void );
static int (_stdcall *pCloseSocket)( SOCKET s );
static word (_stdcall *pHtons)( word hostshort );
static dword (_stdcall *pInet_Addr)( const char* cp );
static char* (_stdcall *pInet_Ntoa)( struct in_addr in );
static SOCKET (_stdcall *pSocket)( int af, int type, int protocol );
static struct hostent *(_stdcall *pGetHostByName)( const char* name );
static int (_stdcall *pIoctlSocket)( SOCKET s, long cmd, dword* argp );
static int (_stdcall *pWSAStartup)( word wVersionRequired, LPWSADATA lpWSAData );
static int (_stdcall *pBind)( SOCKET s, const struct sockaddr* addr, int namelen );
static int (_stdcall *pSetSockopt)( SOCKET s, int level, int optname, const char* optval, int optlen );
static int (_stdcall *pRecvFrom)( SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen );
static int (_stdcall *pSendTo)( SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen );
static int (_stdcall *pSelect)( int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, const struct timeval* timeout );
static int (_stdcall *pConnect)( SOCKET s, const struct sockaddr *name, int namelen );
static int (_stdcall *pGetSockName)( SOCKET s, struct sockaddr *name, int *namelen );
static int (_stdcall *pSend)( SOCKET s, const char *buf, int len, int flags );
static int (_stdcall *pRecv)( SOCKET s, char *buf, int len, int flags );
static int (_stdcall *pGetHostName)( char *name, int namelen );
static dword (_stdcall *pNtohl)( dword netlong );

static dllfunc_t winsock_funcs[] =
{
{ "bind", (void **) &pBind },
{ "send", (void **) &pSend },
{ "recv", (void **) &pRecv },
{ "ntohs", (void **) &pNtohs },
{ "htons", (void **) &pHtons },
{ "ntohl", (void **) &pNtohl },
{ "socket", (void **) &pSocket },
{ "select", (void **) &pSelect },
{ "sendto", (void **) &pSendTo },
{ "connect", (void **) &pConnect },
{ "recvfrom", (void **) &pRecvFrom },
{ "inet_addr", (void **) &pInet_Addr },
{ "inet_ntoa", (void **) &pInet_Ntoa },
{ "WSAStartup", (void **) &pWSAStartup },
{ "WSACleanup", (void **) &pWSACleanup },
{ "setsockopt", (void **) &pSetSockopt },
{ "ioctlsocket", (void **) &pIoctlSocket },
{ "closesocket", (void **) &pCloseSocket },
{ "gethostname", (void **) &pGetHostName },
{ "getsockname", (void **) &pGetSockName },
{ "gethostbyname", (void **) &pGetHostByName },
{ "WSAGetLastError", (void **) &pWSAGetLastError },
{ NULL, NULL }
};

dll_info_t winsock_dll = { "wsock32.dll", winsock_funcs, false };

typedef struct
{
	byte		data[NET_MAX_MESSAGE];
	int		datalen;
} net_loopmsg_t;

typedef struct
{
	net_loopmsg_t	msgs[MAX_LOOPBACK];
	int		get, send;
} net_loopback_t;

typedef struct packetlag_s
{
	byte		*data;	// Raw stream data is stored.
	int		size;
	netadr_t		from;
	float		receivedtime;
	struct packetlag_s	*next;
	struct packetlag_s	*prev;
} packetlag_t;

// split long packets. Anything over 1460 is failing on some routers.
typedef struct
{
	int		current_sequence;
	int		split_count;
	int		total_size;
	char		buffer[NET_MAX_PAYLOAD];
} LONGPACKET;

// use this to pick apart the network stream, must be packed
#pragma pack(push, 1)
typedef struct
{
	int		net_id;
	int		sequence_number;
	byte		packet_id;
} SPLITPACKET;
#pragma pack(pop)

typedef struct
{
	net_loopback_t	loopbacks[NS_COUNT];
	packetlag_t	lagdata[NS_COUNT];
	int		losscount[NS_COUNT];
	float		fakelag;			// cached fakelag value
	LONGPACKET	split;
	int		split_flags[NET_MAX_FRAGMENTS];
	long		sequence_number;

	int		ip_sockets[NS_COUNT];
	int		ipx_sockets[NS_COUNT];

	WSADATA		winsockdata;
	qboolean		initialized;
	qboolean		noip, noipx;
	qboolean		configured;
} net_state_t;

static net_state_t		net;
static convar_t		*net_ipname;
static convar_t		*net_hostport;
static convar_t		*net_iphostport;
static convar_t		*net_ipxhostport;
static convar_t		*net_clientport;
static convar_t		*net_ipclientport;
static convar_t		*net_ipxclientport;
static convar_t		*net_fakelag;
static convar_t		*net_fakeloss;
static convar_t		*net_address;
convar_t			*net_clockwindow;

netadr_t			net_local;
netadr_t			net_local_ipx;

/*
====================
NET_OpenWinSock

load wsock32.dll
====================
*/
qboolean NET_OpenWinSock( void )
{
	// initialize the Winsock function vectors (we do this instead of statically linking
	// so we can run on Win 3.1, where there isn't necessarily Winsock)
	if( Sys_LoadLibrary( &winsock_dll ))
		return true;
	return false;
}

/*
====================
NET_FreeWinSock

unload wsock32.dll
====================
*/
void NET_FreeWinSock( void )
{
	Sys_FreeLibrary( &winsock_dll );
}

/*
====================
NET_ErrorString
====================
*/
char *NET_ErrorString( void )
{
	int	err = WSANOTINITIALISED;

	if( net.initialized )
		err = pWSAGetLastError();

	switch( err )
	{
	case WSAEINTR: return "WSAEINTR";
	case WSAEBADF: return "WSAEBADF";
	case WSAEACCES: return "WSAEACCES";
	case WSAEDISCON: return "WSAEDISCON";
	case WSAEFAULT: return "WSAEFAULT";
	case WSAEINVAL: return "WSAEINVAL";
	case WSAEMFILE: return "WSAEMFILE";
	case WSAEWOULDBLOCK: return "WSAEWOULDBLOCK";
	case WSAEINPROGRESS: return "WSAEINPROGRESS";
	case WSAEALREADY: return "WSAEALREADY";
	case WSAENOTSOCK: return "WSAENOTSOCK";
	case WSAEDESTADDRREQ: return "WSAEDESTADDRREQ";
	case WSAEMSGSIZE: return "WSAEMSGSIZE";
	case WSAEPROTOTYPE: return "WSAEPROTOTYPE";
	case WSAENOPROTOOPT: return "WSAENOPROTOOPT";
	case WSAEPROTONOSUPPORT: return "WSAEPROTONOSUPPORT";
	case WSAESOCKTNOSUPPORT: return "WSAESOCKTNOSUPPORT";
	case WSAEOPNOTSUPP: return "WSAEOPNOTSUPP";
	case WSAEPFNOSUPPORT: return "WSAEPFNOSUPPORT";
	case WSAEAFNOSUPPORT: return "WSAEAFNOSUPPORT";
	case WSAEADDRINUSE: return "WSAEADDRINUSE";
	case WSAEADDRNOTAVAIL: return "WSAEADDRNOTAVAIL";
	case WSAENETDOWN: return "WSAENETDOWN";
	case WSAENETUNREACH: return "WSAENETUNREACH";
	case WSAENETRESET: return "WSAENETRESET";
	case WSAECONNABORTED: return "WSWSAECONNABORTEDAEINTR";
	case WSAECONNRESET: return "WSAECONNRESET";
	case WSAENOBUFS: return "WSAENOBUFS";
	case WSAEISCONN: return "WSAEISCONN";
	case WSAENOTCONN: return "WSAENOTCONN";
	case WSAESHUTDOWN: return "WSAESHUTDOWN";
	case WSAETOOMANYREFS: return "WSAETOOMANYREFS";
	case WSAETIMEDOUT: return "WSAETIMEDOUT";
	case WSAECONNREFUSED: return "WSAECONNREFUSED";
	case WSAELOOP: return "WSAELOOP";
	case WSAENAMETOOLONG: return "WSAENAMETOOLONG";
	case WSAEHOSTDOWN: return "WSAEHOSTDOWN";
	case WSASYSNOTREADY: return "WSASYSNOTREADY";
	case WSAVERNOTSUPPORTED: return "WSAVERNOTSUPPORTED";
	case WSANOTINITIALISED: return "WSANOTINITIALISED";
	case WSAHOST_NOT_FOUND: return "WSAHOST_NOT_FOUND";
	case WSATRY_AGAIN: return "WSATRY_AGAIN";
	case WSANO_RECOVERY: return "WSANO_RECOVERY";
	case WSANO_DATA: return "WSANO_DATA";
	default: return "NO ERROR";
	}
}

/*
====================
NET_NetadrToSockadr
====================
*/
static void NET_NetadrToSockadr( netadr_t *a, struct sockaddr *s )
{
	memset( s, 0, sizeof( *s ));

	if( a->type == NA_BROADCAST )
	{
		((struct sockaddr_in *)s)->sin_family = AF_INET;
		((struct sockaddr_in *)s)->sin_port = a->port;
		((struct sockaddr_in *)s)->sin_addr.s_addr = INADDR_BROADCAST;
	}
	else if( a->type == NA_IP )
	{
		((struct sockaddr_in *)s)->sin_family = AF_INET;
		((struct sockaddr_in *)s)->sin_addr.s_addr = *(int *)&a->ip;
		((struct sockaddr_in *)s)->sin_port = a->port;
	}
	else if( a->type == NA_IPX )
	{
		((struct sockaddr_ipx *)s)->sa_family = AF_IPX;
		memcpy(((struct sockaddr_ipx *)s)->sa_netnum, &a->ipx[0], 4 );
		memcpy(((struct sockaddr_ipx *)s)->sa_nodenum, &a->ipx[4], 6 );
		((struct sockaddr_ipx *)s)->sa_socket = a->port;
	}
	else if( a->type == NA_BROADCAST_IPX )
	{
		((struct sockaddr_ipx *)s)->sa_family = AF_IPX;
		memset(((struct sockaddr_ipx *)s)->sa_netnum, 0, 4 );
		memset(((struct sockaddr_ipx *)s)->sa_nodenum, 0xff, 6 );
		((struct sockaddr_ipx *)s)->sa_socket = a->port;
	}
}

/*
====================
NET_SockadrToNetAdr
====================
*/
static void NET_SockadrToNetadr( struct sockaddr *s, netadr_t *a )
{
	if( s->sa_family == AF_INET )
	{
		a->type = NA_IP;
		*(int *)&a->ip = ((struct sockaddr_in *)s)->sin_addr.s_addr;
		a->port = ((struct sockaddr_in *)s)->sin_port;
	}
	else if( s->sa_family == AF_IPX )
	{
		a->type = NA_IPX;
		memcpy( a->ipx, s->sa_data, 10 );
		a->port = ((struct sockaddr_ipx *)s)->sa_socket;
	}
}

/*
=============
NET_StringToAdr

localhost
idnewt
idnewt:28000
192.246.40.70
192.246.40.70:28000
=============
*/
static qboolean NET_StringToSockaddr( const char *s, struct sockaddr *sadr )
{
	char	*colon;
	char	copy[128];
	int	val, i, j;

	if( !net.initialized ) return false;
	
	memset( sadr, 0, sizeof( *sadr ));

	if(( Q_strlen( s ) >= 24 ) && ( s[8] == ':' ) && ( s[21] == ':' )) // check for an IPX address
	{
		sadr->sa_family = AF_IPX;
		copy[2] = 0;

		for( i = j = 0; i < 10; i++, j += 2 )
		{
			if( j == 8 ) j++; // skip ':'
			copy[0] = s[j+0];
			copy[1] = s[j+1];
			sscanf( copy, "%x", &val );
			sadr->sa_data[i] = (char)val;
		}

		sscanf( &s[22], "%u", &val );
		*(word *)&sadr->sa_data[10] = pHtons( val );
	}
	else
	{
		((struct sockaddr_in *)sadr)->sin_family = AF_INET;
		((struct sockaddr_in *)sadr)->sin_port = 0;

		Q_strncpy( copy, s, sizeof( copy ));

		// strip off a trailing :port if present
		for( colon = copy; *colon; colon++ )
		{
			if( *colon == ':' )
			{
				*colon = 0;
				((struct sockaddr_in *)sadr)->sin_port = pHtons((short)Q_atoi( colon + 1 ));	
			}
		}

		((struct sockaddr_in *)sadr)->sin_addr.s_addr = pInet_Addr( copy );

		if(((struct sockaddr_in *)sadr)->sin_addr.s_addr == INADDR_NONE )
		{
			struct hostent	*h = pGetHostByName( copy );

			if( h == NULL || h->h_addr == NULL )
				return false;

			((struct sockaddr_in *)sadr)->sin_addr.s_addr = *(uint *)h->h_addr;	
		}
	}

	return true;
}

/*
====================
NET_AdrToString
====================
*/
char *NET_AdrToString( const netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return "loopback";
	else if( a.type == NA_IP )
		return va( "%i.%i.%i.%i:%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3], pNtohs( a.port ));
	return va( "%02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x:%i",
	a.ipx[0], a.ipx[1], a.ipx[2], a.ipx[3], a.ipx[4], a.ipx[5], a.ipx[6], a.ipx[7], a.ipx[8], a.ipx[9], pNtohs( a.port ));
}

/*
====================
NET_BaseAdrToString
====================
*/
char *NET_BaseAdrToString( const netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return "loopback";
	else if( a.type == NA_IP )
		return va( "%i.%i.%i.%i", a.ip[0], a.ip[1], a.ip[2], a.ip[3] );
	return va( "%02x%02x%02x%02x:%02x%02x%02x%02x%02x%02x",
	a.ipx[0], a.ipx[1], a.ipx[2], a.ipx[3], a.ipx[4], a.ipx[5], a.ipx[6], a.ipx[7], a.ipx[8], a.ipx[9] );
}

/*
===================
NET_CompareBaseAdr

Compares without the port
===================
*/
qboolean NET_CompareBaseAdr( const netadr_t a, const netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( !memcmp( a.ip, b.ip, 4 ))
			return true;
		return false;
	}

	if( a.type == NA_IPX )
	{
		if( !memcmp( a.ipx, b.ipx, 10 ))
			return true;
		return false;
	}

	return false;
}

/*
====================
NET_CompareClassBAdr

Compare local masks
====================
*/
qboolean NET_CompareClassBAdr( netadr_t a, netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] )
			return true;
	}
	else if( a.type == NA_IPX )
	{
		return false;
	}

	return false;
}

/*
====================
NET_IsReservedAdr

Check for reserved ip's
====================
*/
qboolean NET_IsReservedAdr( netadr_t a )
{
	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if( a.ip[0] == 10 || a.ip[0] == 127 )
			return true;

		if( a.ip[0] == 172 && a.ip[1] >= 16 )
		{
			if( a.ip[1] >= 32 )
				return false;
			return true;
		}

		if( a.ip[0] == 192 && a.ip[1] >= 168 )
			return true;

		return false;
	}
	else if( a.type == NA_IPX )
	{
		return true;
	}

	return false;
}

/*
====================
NET_CompareAdr

Compare full address
====================
*/
qboolean NET_CompareAdr( const netadr_t a, const netadr_t b )
{
	if( a.type != b.type )
		return false;

	if( a.type == NA_LOOPBACK )
		return true;

	if( a.type == NA_IP )
	{
		if(!memcmp( a.ip, b.ip, 4 ) && a.port == b.port )
			return true;
		return false;
	}

	if( a.type == NA_IPX )
	{
		if(!memcmp( a.ipx, b.ipx, 10 ) && a.port == b.port )
			return true;
		return false;
	}

	MsgDev( D_ERROR, "NET_CompareAdr: bad address type\n" );
	return false;
}

/*
====================
NET_IsLocalAddress
====================
*/
qboolean NET_IsLocalAddress( netadr_t adr )
{
	return (adr.type == NA_LOOPBACK) ? true : false;
}

/*
=============
NET_StringToAdr

idnewt
192.246.40.70
=============
*/
qboolean NET_StringToAdr( const char *string, netadr_t *adr )
{
	struct sockaddr s;

	if( !Q_stricmp( string, "localhost" ))
	{
		memset( adr, 0, sizeof( netadr_t ));
		adr->type = NA_LOOPBACK;
		return true;
	}

	if( !NET_StringToSockaddr( string, &s ))
		return false;
	NET_SockadrToNetadr( &s, adr );

	return true;
}

/*
=============================================================================

LOOPBACK BUFFERS FOR LOCAL PLAYER

=============================================================================
*/
/*
====================
NET_GetLoopPacket
====================
*/
static qboolean NET_GetLoopPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	net_loopback_t	*loop;
	int		i;

	if( !data || !length )
		return false;

	loop = &net.loopbacks[sock];

	if( loop->send - loop->get > MAX_LOOPBACK )
		loop->get = loop->send - MAX_LOOPBACK;

	if( loop->get >= loop->send )
		return false;
	i = loop->get & MASK_LOOPBACK;
	loop->get++;

	memcpy( data, loop->msgs[i].data, loop->msgs[i].datalen );
	*length = loop->msgs[i].datalen;

	memset( from, 0, sizeof( *from ));
	from->type = NA_LOOPBACK;

	return true;
}

/*
====================
NET_SendLoopPacket
====================
*/
static void NET_SendLoopPacket( netsrc_t sock, size_t length, const void *data, netadr_t to )
{
	net_loopback_t	*loop;
	int		i;

	loop = &net.loopbacks[sock^1];

	i = loop->send & MASK_LOOPBACK;
	loop->send++;

	memcpy( loop->msgs[i].data, data, length );
	loop->msgs[i].datalen = length;
}

/*
====================
NET_ClearLoopback
====================
*/
static void NET_ClearLoopback( void )
{
	net.loopbacks[0].send = net.loopbacks[0].get = 0;
	net.loopbacks[1].send = net.loopbacks[1].get = 0;
}

/*
=============================================================================

LAG & LOSS SIMULATION SYSTEM (network debugging)

=============================================================================
*/
/*
==================
NET_RemoveFromPacketList

double linked list remove entry
==================
*/
static void NET_RemoveFromPacketList( packetlag_t *p )
{
	p->prev->next = p->next;
	p->next->prev = p->prev;
	p->prev = NULL;
	p->next = NULL;
}

/*
==================
NET_ClearLaggedList

double linked list remove queue
==================
*/
static void NET_ClearLaggedList( packetlag_t *list )
{
	packetlag_t	*p, *n;

	p = list->next;
	while( p && p != list )
	{
		n = p->next;

		NET_RemoveFromPacketList( p );

		if( p->data )
		{
			Mem_Free( p->data );
			p->data = NULL;
		}

		Mem_Free( p );
		p = n;
	}

	list->prev = list;
	list->next = list;
}

/*
==================
NET_AddToLagged

add lagged packet to stream
==================
*/
static void NET_AddToLagged( netsrc_t sock, packetlag_t *list, packetlag_t *packet, netadr_t *from, size_t length, const void *data, float timestamp )
{
	byte	*pStart;

	if( packet->prev || packet->next )
		return;

	packet->prev = list->prev;
	list->prev->next = packet;
	list->prev = packet;
	packet->next = list;

	pStart = (byte *)Z_Malloc( length );
	memcpy( pStart, data, length );
	packet->data = pStart;
	packet->size = length;
	packet->receivedtime = timestamp;
	memcpy( &packet->from, from, sizeof( netadr_t ));
}

/*
==================
NET_AdjustLag

adjust time to next fake lag
==================
*/
static void NET_AdjustLag( void )
{
	static double	lasttime = 0.0;
	float		diff, converge;
	double		dt;

	dt = host.realtime - lasttime;
	dt = bound( 0.0, dt, 0.1 );
	lasttime = host.realtime;

	if( host.developer >= D_ERROR || !net_fakelag->value )
	{
		if( net_fakelag->value != net.fakelag )
		{
			diff = net_fakelag->value - net.fakelag;
			converge = dt * 200.0f;
			if( fabs( diff ) < converge )
				converge = fabs( diff );
			if( diff < 0.0 )
				converge = -converge;
			net.fakelag += converge;
		}
	}
	else
	{
		MsgDev( D_INFO, "Server must enable dev-mode to activate fakelag\n" );
		Cvar_SetValue( "fakelag", 0.0 );
		net.fakelag = 0.0f;
	}
}

/*
==================
NET_LagPacket

add fake lagged packet into rececived message
==================
*/
static qboolean NET_LagPacket( qboolean newdata, netsrc_t sock, netadr_t *from, size_t *length, void *data )
{
	packetlag_t	*pNewPacketLag;
	packetlag_t	*pPacket;
	int		ninterval;
	float		curtime;

	if( net.fakelag <= 0.0f )
	{
		NET_ClearLagData( true, true );
		return newdata;
	}

	curtime = host.realtime;

	if( newdata )
	{
		if( net_fakeloss->value != 0.0f )
		{
			if( host.developer >= D_ERROR )
			{
				net.losscount[sock]++;
				if( net_fakeloss->value <= 0.0f )
				{
					ninterval = fabs( net_fakeloss->value );
					if( ninterval < 2 ) ninterval = 2;

					if(( net.losscount[sock] % ninterval ) == 0 )
						return false;
				}
				else
				{
					if( COM_RandomLong( 0, 100 ) <= net_fakeloss->value )
						return false;
				}
			}
			else
			{
				Cvar_SetValue( "fakeloss", 0.0 );
			}
		}

		pNewPacketLag = (packetlag_t *)Z_Malloc( sizeof( packetlag_t ));
		// queue packet to simulate fake lag
		NET_AddToLagged( sock, &net.lagdata[sock], pNewPacketLag, from, *length, data, curtime );
	}

	pPacket = net.lagdata[sock].next;

	while( pPacket != &net.lagdata[sock] )
	{
		if( pPacket->receivedtime <= curtime - ( net.fakelag / 1000.0 ))
			break;

		pPacket = pPacket->next;
	}

	if( pPacket == &net.lagdata[sock] )
		return false;

	NET_RemoveFromPacketList( pPacket );

	// delivery packet from fake lag queue
	memcpy( data, pPacket->data, pPacket->size );
	memcpy( &net_from, &pPacket->from, sizeof( netadr_t ));
	*length = pPacket->size;

	if( pPacket->data )
		Mem_Free( pPacket->data );

	Mem_Free( pPacket );

	return TRUE;
}

/*
==================
NET_GetLong

receive long packet from network
==================
*/
qboolean NET_GetLong( byte *pData, int size, int *outSize )
{
	uint		packet_number;
	uint		packet_count;
	int		i, sequence_number;
	SPLITPACKET	*pHeader = (SPLITPACKET *)pData;
	uint		packet_payload_size;
	byte		packet_id;

	sequence_number = pHeader->sequence_number;
	packet_id = pHeader->packet_id;
	packet_count = packet_id & 0xF;
	packet_number = (uint)packet_id >> 4;

	if( packet_number >= NET_MAX_FRAGMENTS || packet_count > NET_MAX_FRAGMENTS )
	{
		MsgDev( D_ERROR, "Malformed packet number (%i/%i)\n", packet_number + 1, packet_count );
		return false;
	}
	if (net.split.current_sequence == -1 || sequence_number != net.split.current_sequence )
	{
		net.split.current_sequence = pHeader->sequence_number;
		net.split.split_count = packet_count;
		net.split.total_size = 0;

		// clear part's sequence
		for( i = 0; i < NET_MAX_FRAGMENTS; i++ )
			net.split_flags[i] = -1;

		if( net_showpackets && net_showpackets->value == 4.0f )
			Msg( "<-- Split packet restart %i count %i seq\n", net.split.split_count, sequence_number );
	}

	packet_payload_size = size - sizeof( SPLITPACKET );

	if( net.split_flags[packet_number] == sequence_number )
	{
		MsgDev( D_REPORT, "NET_GetLong: Ignoring duplicated split packet %i of %i ( %i bytes )\n",
			packet_number + 1, packet_count, packet_payload_size );
	}
	else
	{
		if( packet_number == packet_count - 1 )
			net.split.total_size = packet_payload_size + SPLIT_SIZE * ( packet_count - 1 );

		net.split.split_count--;
		net.split_flags[packet_number] = sequence_number;

		if( net_showpackets && net_showpackets->value == 4.0f )
			Msg( "<-- Split packet %i of %i, %i bytes %i seq\n",
				packet_number + 1, packet_count, packet_payload_size, sequence_number );

		if( SPLIT_SIZE * packet_number + packet_payload_size > MAX_UDP_PACKET )
		{
			MsgDev( D_ERROR, "Malformed packet size (%i, %i)\n", SPLIT_SIZE * packet_number, packet_payload_size );
			net.split.current_sequence = -1;

			return false;
		}

		memcpy( &net.split.buffer[SPLIT_SIZE * packet_number], pHeader + 1, packet_payload_size );
	}

	if( net.split.split_count > 0 )
		return false;

	if( packet_count > 0 )
	{
		for( i = 0; i < packet_count; i++ )
		{
			if( net.split_flags[i] != net.split.current_sequence )
			{
				MsgDev( D_ERROR, "Split packet without all %i parts, part %i had wrong sequence %i/%i\n",
					packet_count, i + 1, net.split_flags[i], net.split.current_sequence );
				net.split.current_sequence = -1; // no more parts can be attached, clear it

				return false;
			}
		}
	}

	net.split.current_sequence = -1;

	if( net.split.total_size <= MAX_UDP_PACKET )
	{
		memcpy( pData, net.split.buffer, net.split.total_size );
		*outSize = net.split.total_size;

		return true;
	}
	else
	{
		MsgDev( D_ERROR, "Split packet too large! %d bytes\n", net.split.total_size );
		*outSize = 0;

		return FALSE;
	}
}

/*
==================
NET_QueuePacket

queue normal and lagged packets
==================
*/
qboolean NET_QueuePacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	struct sockaddr	addr;
	int		addr_len;
	int		net_socket;
	int		protocol;
	byte		buf[MAX_UDP_PACKET];
	int		ret = SOCKET_ERROR;

	*length = 0;

	for( protocol = 0; protocol < 2; protocol++ )
	{
		if( !protocol) net_socket = net.ip_sockets[sock];
		else net_socket = net.ipx_sockets[sock];

		if( net_socket == INVALID_SOCKET )
			continue;

		addr_len = sizeof( addr );
		ret = pRecvFrom( net_socket, buf, sizeof( buf ), 0, (struct sockaddr *)&addr, &addr_len );

		NET_SockadrToNetadr( &addr, from );

		if( ret == SOCKET_ERROR )
		{
			int	err = pWSAGetLastError();

			if( err == WSAENETRESET )
				continue;

			if( err != WSAEWOULDBLOCK && err != WSAECONNRESET && err != WSAECONNREFUSED )
			{
				if( err == WSAEMSGSIZE )
				{
					MsgDev( D_REPORT, "NET_QueuePacket: Ignoring oversized network message\n" );
				}
				else
				{
					if( host.type != HOST_DEDICATED )
						Sys_Error( "NET_QueuePacket: %s from %s\n", NET_ErrorString(), NET_AdrToString( *from ));
					else MsgDev( D_ERROR, "NET_QueuePacket: %s from %s\n", NET_ErrorString(), NET_AdrToString( *from ));
				}
			}

			continue;
		}


		if( ret != MAX_UDP_PACKET )
			break;

		MsgDev( D_REPORT, "NET_QueuePacket: oversize packet from %s\n", NET_AdrToString( *from ));
	}

	if( ret == SOCKET_ERROR || ret == MAX_UDP_PACKET )
		return NET_LagPacket( false, sock, from, length, data );

	memcpy( data, buf, ret );
	*length = ret;

	if( *(uint *)data == 0xFFFFFFFE )
	{
		if( *length >= 9 )
		{
			// only server can send split packets, there is no server<->server communication,
			// so server can't receive split packets
			if( sock == NS_SERVER )
			{
				MsgDev( D_ERROR, "Someone tries to send split packet to the server\n" );
				return false;
			}

			return NET_GetLong( data, ret, length );
		}
		else
		{
			MsgDev( D_ERROR, "Invalid split packet length %i\n", *length );
			return false;
		}
	}
	else
	{
		return NET_LagPacket( true, sock, from, length, data );
	}
}

/*
==================
NET_GetPacket

Never called by the game logic, just the system event queing
==================
*/
qboolean NET_GetPacket( netsrc_t sock, netadr_t *from, byte *data, size_t *length )
{
	if( !data || !length )
		return false;

	NET_AdjustLag();

	if( NET_GetLoopPacket( sock, from, data, length ))
	{
		return NET_LagPacket( true, sock, from, length, data );
	}
	else
	{
		return NET_QueuePacket( sock, from, data, length );
	}
}

/*
==================
NET_SendLong

Fragment long packets, send short directly
==================
*/
int NET_SendLong( netsrc_t sock, int net_socket, const char *buf, int len, int flags, const struct sockaddr *to, int tolen )
{
	// do we need to break this packet up?
	if( sock == NS_SERVER && len > MAX_ROUTEABLE_PACKET )
	{
		char		packet[MAX_ROUTEABLE_PACKET];
		int		total_sent, size, packet_count;
		int		ret, packet_number;
		SPLITPACKET	*pPacket;

		net.sequence_number++;
		if( net.sequence_number <= 0 )
			net.sequence_number = 1;

		pPacket = (SPLITPACKET *)packet;
		pPacket->sequence_number = net.sequence_number;
		pPacket->net_id = -2;
		packet_number = 0;
		total_sent = 0;
		packet_count = (len + SPLIT_SIZE - 1) / SPLIT_SIZE;

		while( len > 0 )
		{
			size = Q_min( SPLIT_SIZE, len );
			pPacket->packet_id = (packet_number << 4) + packet_count;
			memcpy( packet + sizeof( SPLITPACKET ), buf + ( packet_number * SPLIT_SIZE ), size );

			if( net_showpackets && net_showpackets->value == 3.0f )
			{
				netadr_t	adr;

				memset( &adr, 0, sizeof( adr ));
				NET_SockadrToNetadr((struct sockaddr *)to, &adr );

				Msg( "Sending split %i of %i with %i bytes and seq %i to %s\n",
					packet_number + 1, packet_count, size, net.sequence_number, NET_AdrToString( adr ));
			}

			ret = pSendTo( net_socket, packet, size + sizeof( SPLITPACKET ), flags, to, tolen );
			if( ret < 0 ) return ret; // error

			if( ret >= size )
				total_sent += size;
			len -= size;
			packet_number++;
		}

		return total_sent;
	}
	else
	{
		// no fragmenantion for client connection
		return pSendTo( net_socket, buf, len, flags, to, tolen );
	}
}

/*
==================
NET_SendPacket
==================
*/
void NET_SendPacket( netsrc_t sock, size_t length, const void *data, netadr_t to )
{
	int		ret, err;
	struct sockaddr	addr;
	SOCKET		net_socket;

	if( !net.initialized || to.type == NA_LOOPBACK )
	{
		NET_SendLoopPacket( sock, length, data, to );
		return;
	}
	else if( to.type == NA_BROADCAST )
	{
		net_socket = net.ip_sockets[sock];
		if( net_socket == INVALID_SOCKET )
			return;
	}
	else if( to.type == NA_IP )
	{
		net_socket = net.ip_sockets[sock];
		if( net_socket == INVALID_SOCKET )
			return;
	}
	else if( to.type == NA_IPX )
	{
		net_socket = net.ipx_sockets[sock];
		if( net_socket == INVALID_SOCKET )
			return;
	}
	else if( to.type == NA_BROADCAST_IPX )
	{
		net_socket = net.ipx_sockets[sock];
		if( net_socket == INVALID_SOCKET )
			return;
	}
	else Host_Error( "NET_SendPacket: bad address type %i\n", to.type );

	NET_NetadrToSockadr( &to, &addr );

	ret = NET_SendLong( sock, net_socket, data, length, 0, &addr, sizeof( addr ));

	if( ret == SOCKET_ERROR )
	{
		err = pWSAGetLastError();

		// WSAEWOULDBLOCK is silent
		if( err == WSAEWOULDBLOCK )
			return;

		// some PPP links don't allow broadcasts
		if(( err == WSAEADDRNOTAVAIL ) && (( to.type == NA_BROADCAST ) || ( to.type == NA_BROADCAST_IPX )))
			return;

		// let dedicated servers continue after errors
		if( host.type == HOST_DEDICATED )
		{
			MsgDev( D_ERROR, "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
		else if( err == WSAEADDRNOTAVAIL || err == WSAENOBUFS )
		{
			MsgDev( D_ERROR, "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
		else
		{
			Host_Error( "NET_SendPacket: %s to %s\n", NET_ErrorString(), NET_AdrToString( to ));
		}
	}
}

/*
====================
NET_IPSocket
====================
*/
static int NET_IPSocket( const char *net_interface, int port, qboolean multicast )
{
	int		err, net_socket;
	uint		optval = 1;
	struct sockaddr_in	addr;

	if(( net_socket = pSocket( PF_INET, SOCK_DGRAM, IPPROTO_UDP )) == SOCKET_ERROR )
	{
		err = pWSAGetLastError();
		if( err != WSAEAFNOSUPPORT )
			MsgDev( D_WARN, "NET_UDPSocket: port: %d socket: %s\n", port, NET_ErrorString( ));
		return INVALID_SOCKET;
	}

	if( pIoctlSocket( net_socket, FIONBIO, &optval ) == SOCKET_ERROR )
	{
		MsgDev( D_WARN, "NET_UDPSocket: port: %d ioctl FIONBIO: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	// make it broadcast capable
	if( pSetSockopt( net_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&optval, sizeof( optval )) == SOCKET_ERROR )
	{
		MsgDev( D_WARN, "NET_UDPSocket: port: %d setsockopt SO_BROADCAST: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	if( Sys_CheckParm( "-reuse" ) || multicast )
	{
		if( pSetSockopt( net_socket, SOL_SOCKET, SO_REUSEADDR, (const char *)&optval, sizeof( optval )) == SOCKET_ERROR )
		{
			MsgDev( D_WARN, "NET_UDPSocket: port: %d setsockopt SO_REUSEADDR: %s\n", port, NET_ErrorString( ));
			pCloseSocket( net_socket );
			return INVALID_SOCKET;
		}
	}

	if( Sys_CheckParm( "-tos" ))
	{
		optval = 16;
		MsgDev( D_INFO, "Enabling LOWDELAY TOS option\n" );
		if( pSetSockopt( net_socket, IPPROTO_IP, IP_TOS, (const char *)&optval, sizeof( optval )) == SOCKET_ERROR )
		{
			err = pWSAGetLastError();
			if( err != WSAENOPROTOOPT )
				MsgDev( D_WARN, "NET_UDPSocket: port: %d  setsockopt IP_TOS: %s\n", port, NET_ErrorString( ));
			pCloseSocket( net_socket );
			return INVALID_SOCKET;
		}
	}

	if( !net_interface[0] || !Q_stricmp( net_interface, "localhost" ))
		addr.sin_addr.s_addr = INADDR_ANY;
	else NET_StringToSockaddr( net_interface, (struct sockaddr *)&addr );

	if( port == PORT_ANY ) addr.sin_port = 0;
	else addr.sin_port = pHtons((short)port);

	addr.sin_family = AF_INET;

	if( pBind( net_socket, (void *)&addr, sizeof( addr )) == SOCKET_ERROR )
	{
		MsgDev( D_WARN, "NET_UDPSocket: port: %d bind: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	if( Sys_CheckParm( "-loopback" ))
	{
		optval = 1;
		if( pSetSockopt( net_socket, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&optval, sizeof( optval )) == SOCKET_ERROR )
			MsgDev( D_WARN, "NET_UDPSocket: port %d setsockopt IP_MULTICAST_LOOP: %s\n", port, NET_ErrorString( ));
	}

	return net_socket;
}

/*
====================
NET_OpenIP
====================
*/
static void NET_OpenIP( void )
{
	int		port, sv_port = 0, cl_port = 0;
	static qboolean	bFirst = true;

	if( net.ip_sockets[NS_SERVER] == INVALID_SOCKET )
	{
		port = net_iphostport->value;
		if( !port ) port = net_hostport->value;
		if( !port ) port = PORT_SERVER; // forcing to default
		net.ip_sockets[NS_SERVER] = NET_IPSocket( net_ipname->string, port, false );

		if( net.ip_sockets[NS_SERVER] == INVALID_SOCKET && host.type == HOST_DEDICATED )
			Host_Error( "Couldn't allocate dedicated server IP port %d.\n", port );
		sv_port = port;
	}

	// dedicated servers don't need client ports
	if( host.type == HOST_DEDICATED ) return;

	if( net.ip_sockets[NS_CLIENT] == INVALID_SOCKET )
	{
		port = net_ipclientport->value;
		if( !port ) port = net_clientport->value;
		if( !port ) port = PORT_ANY; // forcing to default
		net.ip_sockets[NS_CLIENT] = NET_IPSocket( net_ipname->string, port, false );

		if( net.ip_sockets[NS_CLIENT] == INVALID_SOCKET )
			net.ip_sockets[NS_CLIENT] = NET_IPSocket( net_ipname->string, PORT_ANY, false );
		cl_port = port;
	}

	if( bFirst )
	{
		MsgDev( D_INFO, "NET Ports:  server %i, client %i\n", sv_port, cl_port );
		bFirst = false;
	}
}

/*
====================
NET_IPXSocket
====================
*/
static int NET_IPXSocket( int port )
{
	int		err, net_socket;
	uint		optval = 1;
	struct sockaddr_ipx	addr;

	if(( net_socket = pSocket( PF_IPX, SOCK_DGRAM, NSPROTO_IPX )) == SOCKET_ERROR )
	{
		err = pWSAGetLastError();
		if( err != WSAEAFNOSUPPORT )
			MsgDev( D_ERROR, "NET_IPXSocket: port %d socket: %s\n", port, NET_ErrorString( ));
		return INVALID_SOCKET;
	}

	// make it non-blocking
	if( pIoctlSocket( net_socket, FIONBIO, &optval ) == SOCKET_ERROR )
	{
		MsgDev( D_ERROR, "NET_IPXSocket: port: %d ioctl FIONBIO: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	// make it broadcast capable
	if( pSetSockopt( net_socket, SOL_SOCKET, SO_BROADCAST, (const char *)&optval, sizeof( optval )) == SOCKET_ERROR )
	{
		MsgDev( D_ERROR, "NET_IPXSocket: port: %d  setsockopt SO_BROADCAST: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	addr.sa_family = AF_IPX;
	memset( addr.sa_netnum, 0, 4 );
	memset( addr.sa_nodenum, 0, 6 );

	if( port == PORT_ANY ) addr.sa_socket = 0;
	else addr.sa_socket = pHtons( (word)port );

	if( pBind( net_socket, (void *)&addr, sizeof( addr )) == SOCKET_ERROR )
	{
		MsgDev( D_WARN, "NET_IPXSocket: port: %d bind: %s\n", port, NET_ErrorString( ));
		pCloseSocket( net_socket );
		return INVALID_SOCKET;
	}

	return net_socket;
}


/*
====================
NET_OpenIPX
====================
*/
void NET_OpenIPX( void )
{
	int	port;

	if( net.ipx_sockets[NS_SERVER] == INVALID_SOCKET )
	{
		port = net_ipxhostport->value;
		if( !port ) port = net_hostport->value;
		if( !port ) port = PORT_SERVER; // forcing to default
		net.ipx_sockets[NS_SERVER] = NET_IPXSocket( port );
	}

	// dedicated servers don't need client ports
	if( host.type == HOST_DEDICATED ) return;

	if( net.ipx_sockets[NS_CLIENT] == INVALID_SOCKET )
	{
		port = net_ipxclientport->value;
		if( !port ) port = net_clientport->value;
		if( !port ) port = PORT_ANY; // forcing to default
		net.ipx_sockets[NS_CLIENT] = NET_IPXSocket( port );

		if( net.ipx_sockets[NS_CLIENT] == INVALID_SOCKET )
			net.ipx_sockets[NS_CLIENT] = NET_IPXSocket( PORT_ANY );
	}
}

/*
================
NET_GetLocalAddress

Returns the servers' ip address as a string.
================
*/
void NET_GetLocalAddress( void )
{
	char		buff[512];
	struct sockaddr_in	address;
	int		namelen;

	memset( &net_local, 0, sizeof( netadr_t ));
	memset( &net_local_ipx, 0, sizeof( netadr_t ));
	buff[0] = '\0';

	if( net.noip )
	{
		MsgDev( D_INFO, "TCP/IP Disabled.\n" );
	}
	else
	{
		// If we have changed the ip var from the command line, use that instead.
		if( Q_strcmp( net_ipname->string, "localhost" ))
		{
			Q_strcpy( buff, net_ipname->string );
		}
		else
		{
			pGetHostName( buff, 512 );
		}

		// ensure that it doesn't overrun the buffer
		buff[511] = 0;

		if( NET_StringToAdr( buff, &net_local ))
		{
			namelen = sizeof( address );

			if( pGetSockName( net.ip_sockets[NS_SERVER], (struct sockaddr *)&address, &namelen ) == SOCKET_ERROR )
			{
				MsgDev( D_ERROR, "Could not get TCP/IP address, TCP/IP disabled\nReason:  %s\n", NET_ErrorString( ));
				net.noip = true;
			}
			else
			{
				net_local.port = address.sin_port;
				Msg( "Server IP address %s\n", NET_AdrToString( net_local ));
				Cvar_FullSet( "net_address", va( NET_AdrToString( net_local )), FCVAR_READ_ONLY );
			}
		}
		else
		{
			MsgDev( D_ERROR, "Could not get TCP/IP address, Invalid hostname: '%s'\n", buff );
		}
	}

	if( net.noipx )
	{
		MsgDev( D_INFO, "No IPX Support.\n" );
	}
	else
	{
		namelen = 14;
		if( pGetSockName( net.ipx_sockets[NS_SERVER], (struct sockaddr *)&address, &namelen ) == SOCKET_ERROR )
		{
			MsgDev( D_ERROR, "Could not get IPX socket name, IPX disabled\nReason:  %s\n", NET_ErrorString( ));
			net.noipx = true;
		}
		else
		{
			NET_SockadrToNetadr((struct sockaddr *)&address, &net_local_ipx );
			Msg( "Server IPX address %s\n", NET_AdrToString( net_local_ipx ));
		}
	}
}

/*
====================
NET_Config

A single player game will only use the loopback code
====================
*/
void NET_Config( qboolean multiplayer )
{
	static qboolean	old_config;
	static qboolean	bFirst = true;

	if( !net.initialized )
		return;

	if( old_config == multiplayer )
		return;

	old_config = multiplayer;

	if( multiplayer )
	{	
		// open sockets
		if( !net.noip ) NET_OpenIP();
		if( !net.noipx ) NET_OpenIPX();

		// get our local address, if possible
		if( bFirst )
		{
			NET_GetLocalAddress();
			bFirst = false;
		}
	}
	else
	{	
		int	i;

		// shut down any existing sockets
		for( i = 0; i < NS_COUNT; i++ )
		{
			if( net.ip_sockets[i] != INVALID_SOCKET )
			{
				pCloseSocket( net.ip_sockets[i] );
				net.ip_sockets[i] = INVALID_SOCKET;
			}

			if( net.ipx_sockets[i] != INVALID_SOCKET )
			{
				pCloseSocket( net.ipx_sockets[i] );
				net.ipx_sockets[i] = INVALID_SOCKET;
			}
		}
	}

	NET_ClearLoopback ();

	net.configured = multiplayer ? true : false;
}

/*
====================
NET_IsConfigured

Is winsock ip initialized?
====================
*/
qboolean NET_IsConfigured( void )
{
	return net.configured;
}

/*
====================
NET_IsActive
====================
*/
qboolean NET_IsActive( void )
{
	return net.initialized;
}

/*
====================
NET_Sleep

sleeps msec or until net socket is ready
====================
*/
void NET_Sleep( int msec )
{
	struct timeval	timeout;
	fd_set		fdset;
	int		i = 0;

	if( !net.initialized || host.type == HOST_NORMAL )
		return; // we're not a dedicated server, just run full speed

	FD_ZERO( &fdset );

	if( net.ip_sockets[NS_SERVER] != INVALID_SOCKET )
	{
		FD_SET( net.ip_sockets[NS_SERVER], &fdset ); // network socket
		i = net.ip_sockets[NS_SERVER];
	}

	if( net.ipx_sockets[NS_SERVER] != INVALID_SOCKET )
	{
		FD_SET( net.ipx_sockets[NS_SERVER], &fdset ); // network socket
		if( net.ipx_sockets[NS_SERVER] > i )
			i = net.ipx_sockets[NS_SERVER];
	}

	timeout.tv_sec = msec / 1000;
	timeout.tv_usec = (msec % 1000) * 1000;
	pSelect( i+1, &fdset, NULL, NULL, &timeout );
}

/*
====================
NET_ClearLagData

clear fakelag list
====================
*/
void NET_ClearLagData( qboolean bClient, qboolean bServer )
{
	if( bClient ) NET_ClearLaggedList( &net.lagdata[NS_CLIENT] );
	if( bServer ) NET_ClearLaggedList( &net.lagdata[NS_SERVER] );
}

/*
====================
NET_Init
====================
*/
void NET_Init( void )
{
	char	cmd[64];
	int	i = 1;

	if( net.initialized ) return;

	net_clockwindow = Cvar_Get( "clockwindow", "0.5", 0, "timewindow to execute client moves" );
	net_address = Cvar_Get( "net_address", "0", FCVAR_READ_ONLY, "contain local address of current client" );
	net_ipname = Cvar_Get( "ip", "localhost", FCVAR_READ_ONLY, "network ip address" );
	net_iphostport = Cvar_Get( "ip_hostport", "0", FCVAR_READ_ONLY, "network ip host port" );
	net_ipxhostport = Cvar_Get( "ipx_hostport", "0", FCVAR_READ_ONLY, "network ipx host port" );
	net_hostport = Cvar_Get( "hostport", va( "%i", PORT_SERVER ), FCVAR_READ_ONLY, "network default host port" );
	net_ipclientport = Cvar_Get( "ip_clientport", "0", FCVAR_READ_ONLY, "network ip client port" );
	net_ipxclientport = Cvar_Get( "ipx_clientport", "0", FCVAR_READ_ONLY, "network ipx client port" );
	net_clientport = Cvar_Get( "clientport", va( "%i", PORT_CLIENT ), FCVAR_READ_ONLY, "network default client port" );
	net_fakelag = Cvar_Get( "fakelag", "0", 0, "lag all incoming network data (including loopback) by xxx ms." );
	net_fakeloss = Cvar_Get( "fakeloss", "0", 0, "act like we dropped the packet this % of the time." );

	// prepare some network data
	for( i = 0; i < NS_COUNT; i++ )
	{
		net.lagdata[i].prev = &net.lagdata[i];
		net.lagdata[i].next = &net.lagdata[i];
		net.ip_sockets[i] = INVALID_SOCKET;
		net.ipx_sockets[i] = INVALID_SOCKET;
	}

	if( !NET_OpenWinSock( ))	// loading wsock32.dll
	{
		MsgDev( D_ERROR, "network failed to load wsock32.dll.\n" );
		return;
	}

	i = pWSAStartup( MAKEWORD( 1, 1 ), &net.winsockdata );
	if( i )
	{
		MsgDev( D_ERROR, "network initialization failed.\n" );
		NET_FreeWinSock();
		return;
	}

	if( Sys_CheckParm( "-noip" ))
		net.noip = true;

	if( Sys_CheckParm( "-noipx" ))
		net.noipx = true;

	// specify custom host port
	if( Sys_GetParmFromCmdLine( "-port", cmd ) && Q_isdigit( cmd ))
		Cvar_FullSet( "hostport", cmd, FCVAR_READ_ONLY );

	// adjust clockwindow
	if( Sys_GetParmFromCmdLine( "-clockwindow", cmd ))
		Cvar_SetValue( "clockwindow", Q_atof( cmd ));

	net.sequence_number = 1;
	net.initialized = true;
	MsgDev( D_REPORT, "Base networking initialized.\n" );
}


/*
====================
NET_Shutdown
====================
*/
void NET_Shutdown( void )
{
	if( !net.initialized )
		return;

	NET_ClearLagData( true, true );

	NET_Config( false );
	pWSACleanup();
	NET_FreeWinSock();
	net.initialized = false;
}