/*
cl_main.c - client main loop
Copyright (C) 2009 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "common.h"
#include "client.h"
#include "net_encode.h"
#include "cl_tent.h"
#include "gl_local.h"
#include "input.h"
#include "../cl_dll/kbutton.h"
#include "vgui_draw.h"

#define MAX_TOTAL_CMDS		32
#define MAX_CMD_BUFFER		8000
#define CONNECTION_PROBLEM_TIME	15.0	// 15 seconds

CVAR_DEFINE_AUTO( mp_decals, "300", FCVAR_ARCHIVE, "decals limit in multiplayer" );
CVAR_DEFINE_AUTO( dev_overview, "0", 0, "draw level in overview-mode" );
convar_t	*rcon_client_password;
convar_t	*rcon_address;
convar_t	*cl_timeout;
convar_t	*cl_nopred;
convar_t	*cl_showfps;
convar_t	*cl_nodelta;
convar_t	*cl_crosshair;
convar_t	*cl_cmdbackup;
convar_t	*cl_showerror;
convar_t	*cl_bmodelinterp;
convar_t	*cl_draw_particles;
convar_t	*cl_draw_tracers;
convar_t	*cl_lightstyle_lerping;
convar_t	*cl_idealpitchscale;
convar_t	*cl_nosmooth;
convar_t	*cl_smoothtime;
convar_t	*cl_clockreset;
convar_t	*cl_fixtimerate;
convar_t	*cl_solid_players;
convar_t	*cl_draw_beams;
convar_t	*cl_updaterate;
convar_t	*cl_showevents;
convar_t	*cl_cmdrate;
convar_t	*cl_interp;
convar_t	*cl_dlmax;
convar_t	*cl_lw;

//
// userinfo
//
convar_t	*name;
convar_t	*model;
convar_t	*topcolor;
convar_t	*bottomcolor;
convar_t	*rate;

client_t		cl;
client_static_t	cls;
clgame_static_t	clgame;

//======================================================================
qboolean CL_Active( void )
{
	return ( cls.state == ca_active );
}

//======================================================================
qboolean CL_IsInGame( void )
{
	if( host.type == HOST_DEDICATED ) return true;	// always active for dedicated servers
	if( CL_GetMaxClients() > 1 ) return true;	// always active for multiplayer
	return ( cls.key_dest == key_game );		// active if not menu or console
}

qboolean CL_IsInMenu( void )
{
	return ( cls.key_dest == key_menu );
}

qboolean CL_IsInConsole( void )
{
	return ( cls.key_dest == key_console );
}

qboolean CL_IsIntermission( void )
{
	return cl.intermission;
}

qboolean CL_IsPlaybackDemo( void )
{
	return cls.demoplayback;
}

qboolean CL_IsRecordDemo( void )
{
	return cls.demorecording;
}

qboolean CL_DisableVisibility( void )
{
	return cls.envshot_disable_vis;
}

qboolean CL_IsBackgroundDemo( void )
{
	return ( cls.demoplayback && cls.demonum != -1 );
}

qboolean CL_IsBackgroundMap( void )
{
	return ( cl.background && !cls.demoplayback );
}

char *CL_Userinfo( void )
{
	return cls.userinfo;
}

int CL_IsDevOverviewMode( void )
{
	if( dev_overview.value > 0.0f )
	{
		if( host.developer > 0 || cls.spectator )
			return (int)dev_overview.value;
	}

	return 0;
}

/*
===============
CL_ChangeGame

This is experiment. Use with precaution
===============
*/
qboolean CL_ChangeGame( const char *gamefolder, qboolean bReset )
{
	if( host.type == HOST_DEDICATED )
		return false;

	if( Q_stricmp( host.gamefolder, gamefolder ))
	{
		kbutton_t	*mlook, *jlook;
		qboolean	mlook_active = false, jlook_active = false;
		string	mapname, maptitle;
		int	maxEntities;

		mlook = (kbutton_t *)clgame.dllFuncs.KB_Find( "in_mlook" );
		jlook = (kbutton_t *)clgame.dllFuncs.KB_Find( "in_jlook" );

		if( mlook && ( mlook->state & 1 )) 
			mlook_active = true;

		if( jlook && ( jlook->state & 1 ))
			jlook_active = true;
	
		// so reload all images (remote connect)
		Mod_ClearAll( true );
		R_ShutdownImages();
		FS_LoadGameInfo( (bReset) ? host.gamefolder : gamefolder );
		R_InitImages();

		// save parms
		maxEntities = clgame.maxEntities;
		Q_strncpy( mapname, clgame.mapname, MAX_STRING );
		Q_strncpy( maptitle, clgame.maptitle, MAX_STRING );

		if( !CL_LoadProgs( va( "%s/client.dll", GI->dll_path )))
			Host_Error( "can't initialize client.dll\n" );

		// restore parms
		clgame.maxEntities = maxEntities;
		Q_strncpy( clgame.mapname, mapname, MAX_STRING );
		Q_strncpy( clgame.maptitle, maptitle, MAX_STRING );

		// invalidate fonts so we can reloading them again
		memset( &cls.creditsFont, 0, sizeof( cls.creditsFont ));
		SCR_InstallParticlePalette();
		SCR_LoadCreditsFont();
		Con_InvalidateFonts();

		SCR_RegisterTextures ();
		CL_FreeEdicts ();
		SCR_VidInit ();

		if( cls.key_dest == key_game ) // restore mouse state
			clgame.dllFuncs.IN_ActivateMouse();

		// restore mlook state
		if( mlook_active ) Cmd_ExecuteString( "+mlook\n" );
		if( jlook_active ) Cmd_ExecuteString( "+jlook\n" );
		return true;
	}

	return false;
}

/*
===============
CL_CheckClientState

finalize connection process and begin new frame
with new cls.state
===============
*/
void CL_CheckClientState( void )
{
	if(( cls.state == ca_connected || cls.state == ca_validate ) && ( cls.signon == SIGNONS ))
	{	
		// first update is the final signon stage
		cls.state = ca_active;
		cls.changelevel = false;
		cls.changedemo = false;
		cl.first_frame = true;

		Cvar_SetValue( "scr_loading", 0.0f );
		Netchan_ReportFlow( &cls.netchan );
		SCR_MakeLevelShot();
	}
}

/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
static float CL_LerpPoint( void )
{
	float	f, frac = 1.0f;

	f = cl_serverframetime();
	
	if( f == 0.0f || cls.timedemo )
	{
		cl.time = cl.mtime[0];

		// g-cont. probably this is redundant
		if( cls.demoplayback )
			cl.oldtime = cl.mtime[0] - cl_clientframetime();

		return 1.0f;
	}

	if( cl_interp->value > 0.001f )
	{
		// manual lerp value (goldsrc mode)
		frac = ( cl.time - cl.mtime[0] ) / cl_interp->value;
	}
	else if( f > 0.001f )
	{
		// automatic lerp (classic mode)
		frac = ( cl.time - cl.mtime[1] ) / f;
	}

	return frac;
}

/*
===============
CL_DriftInterpolationAmount

Drift interpolation value (this is used for server unlag system)
===============
*/
int CL_DriftInterpolationAmount( int goal )
{
	float	fgoal, maxmove, diff;
	int	msec;

	fgoal = (float)goal / 1000.0f;

	if( fgoal != cl.local.interp_amount )
	{
		maxmove = host.frametime * 0.05;
		diff = fgoal - cl.local.interp_amount;
		diff = bound( -maxmove, diff, maxmove );
		cl.local.interp_amount += diff;
	}

	msec = cl.local.interp_amount * 1000.0f;
	msec = bound( 0, msec, 100 );

	return msec;
}

/*
===============
CL_ComputeClientInterpolationAmount

Validate interpolation cvars, calc interpolation window
===============
*/
void CL_ComputeClientInterpolationAmount( usercmd_t *cmd )
{
	int	min_interp = MIN_EX_INTERP;
	int	max_interp = MAX_EX_INTERP;
	int	interpolation_msec;
	qboolean	forced = false;

	if( cl_updaterate->value < MIN_UPDATERATE )
	{
		Con_Printf( "cl_updaterate minimum is %f, resetting to default (20)\n", MIN_UPDATERATE );
		Cvar_Reset( "cl_updaterate" );
	}

	if( cl_updaterate->value > MAX_UPDATERATE )
	{
		Con_Printf( "cl_updaterate clamped at maximum (%f)\n", MAX_UPDATERATE );
		Cvar_SetValue( "cl_updaterate", MAX_UPDATERATE );
	}

	if( cls.spectator )
		max_interp = 200;

	min_interp = 1000.0f / cl_updaterate->value;
	min_interp = Q_max( 1, min_interp );
	interpolation_msec = cl_interp->value * 1000.0f;

	if(( interpolation_msec + 1 ) < min_interp )
	{
		MsgDev( D_INFO, "ex_interp forced up to %i msec\n", interpolation_msec );
		interpolation_msec = min_interp;
		forced = true;
	}
	else if(( interpolation_msec - 1 ) > max_interp )
	{
		MsgDev( D_INFO, "ex_interp forced down to %i msec\n", interpolation_msec );
		interpolation_msec = max_interp;
		forced = true;
	}

	if( forced ) Cvar_SetValue( "ex_interp", (float)interpolation_msec * 0.001f );
	interpolation_msec = bound( min_interp, interpolation_msec, max_interp );	

	cmd->lerp_msec = CL_DriftInterpolationAmount( interpolation_msec );
}

/*
=================
CL_ComputePacketLoss

=================
*/
void CL_ComputePacketLoss( void )
{
	int	i, frm;
	frame_t	*frame;
	int	count = 0;
	int	lost = 0;

	if( host.realtime < cls.packet_loss_recalc_time )
		return;

	// recalc every second
	cls.packet_loss_recalc_time = host.realtime + 1.0;

	// compuate packet loss
	for( i = cls.netchan.incoming_sequence - CL_UPDATE_BACKUP + 1; i <= cls.netchan.incoming_sequence; i++ )
	{
		frm = i;
		frame = &cl.frames[frm & CL_UPDATE_MASK];

		if( frame->receivedtime == -1.0 )
			lost++;
		count++;
	}

	if( count <= 0 ) cls.packet_loss = 0.0f;
	else cls.packet_loss = ( 100.0f * (float)lost ) / (float)count;
}

/*
=================
CL_UpdateFrameLerp

=================
*/
void CL_UpdateFrameLerp( void )
{
	if( cls.state != ca_active || !cl.validsequence )
		return;

	// compute last interpolation amount
	cl.lerpFrac = CL_LerpPoint();

	cl.commands[(cls.netchan.outgoing_sequence - 1) & CL_UPDATE_MASK].frame_lerp = cl.lerpFrac;
}

void CL_FindInterpolatedAddAngle( float t, float *frac, pred_viewangle_t **prev, pred_viewangle_t **next )
{
	int	i, i0, i1, imod;
	float	at;

	imod = cl.angle_position - 1;
	i0 = (imod + 1) & ANGLE_MASK;
	i1 = (imod + 0) & ANGLE_MASK;

	if( cl.predicted_angle[i0].starttime >= t )
	{
		for( i = 0; i < ANGLE_BACKUP - 2; i++ )
		{
			at = cl.predicted_angle[imod & ANGLE_MASK].starttime;
			if( at == 0.0f ) break;

			if( at < t )
			{
				i0 = (imod + 1) & ANGLE_MASK;
				i1 = (imod + 0) & ANGLE_MASK;
				break;
			}
			imod--;
		}
	}

	*next = &cl.predicted_angle[i0];
	*prev = &cl.predicted_angle[i1];

	// avoid division by zero (probably this should never happens)
	if((*prev)->starttime == (*next)->starttime )
	{
		*prev = *next;
		*frac = 0.0f;
		return;
	}

	// time spans the two entries
	*frac = ( t - (*prev)->starttime ) / ((*next)->starttime - (*prev)->starttime );
	*frac = bound( 0.0f, *frac, 1.0f );
}

void CL_ApplyAddAngle( void )
{
	float		curtime = cl.time - cl_serverframetime();
	pred_viewangle_t	*prev = NULL, *next = NULL;
	float		addangletotal = 0.0f;
	float		amove, frac = 0.0f;

	CL_FindInterpolatedAddAngle( curtime, &frac, &prev, &next );

	if( prev && next )
		addangletotal = prev->total + frac * ( next->total - prev->total );
	else addangletotal = cl.prevaddangletotal;

	amove = addangletotal - cl.prevaddangletotal;

	// update input angles
	cl.viewangles[YAW] += amove;

	// remember last total
	cl.prevaddangletotal = addangletotal;
}


/*
=======================================================================

CLIENT MOVEMENT COMMUNICATION

=======================================================================
*/
/*
===============
CL_ProcessShowTexturesCmds

navigate around texture atlas
===============
*/
qboolean CL_ProcessShowTexturesCmds( usercmd_t *cmd )
{
	static int	oldbuttons;
	int		changed;
	int		pressed, released;

	if( !gl_showtextures->value || CL_IsDevOverviewMode( ))
		return false;

	changed = (oldbuttons ^ cmd->buttons);
	pressed =  changed & cmd->buttons;
	released = changed & (~cmd->buttons);

	if( released & ( IN_RIGHT|IN_MOVERIGHT ))
		Cvar_SetValue( "r_showtextures", gl_showtextures->value + 1 );
	if( released & ( IN_LEFT|IN_MOVELEFT ))
		Cvar_SetValue( "r_showtextures", max( 1, gl_showtextures->value - 1 ));
	oldbuttons = cmd->buttons;

	return true;
}

/*
===============
CL_ProcessOverviewCmds

Transform user movement into overview adjust
===============
*/
qboolean CL_ProcessOverviewCmds( usercmd_t *cmd )
{
	ref_overview_t	*ov = &clgame.overView;
	int		sign = 1;
	float		size = world.size[!ov->rotated] / world.size[ov->rotated];
	float		step = (2.0f / size) * host.realframetime;
	float		step2 = step * 100.0f * (2.0f / ov->flZoom);

	if( !CL_IsDevOverviewMode() || gl_showtextures->value )
		return false;

	if( ov->flZoom < 0.0f ) sign = -1;

	if( cmd->upmove > 0.0f ) ov->zNear += step;
	else if( cmd->upmove < 0.0f ) ov->zNear -= step;

	if( cmd->buttons & IN_JUMP ) ov->zFar += step;
	else if( cmd->buttons & IN_DUCK ) ov->zFar -= step;

	if( cmd->buttons & IN_FORWARD ) ov->origin[ov->rotated] -= sign * step2;
	else if( cmd->buttons & IN_BACK ) ov->origin[ov->rotated] += sign * step2;

	if( ov->rotated )
	{
		if( cmd->buttons & ( IN_RIGHT|IN_MOVERIGHT ))
			ov->origin[0] -= sign * step2;
		else if( cmd->buttons & ( IN_LEFT|IN_MOVELEFT ))
			ov->origin[0] += sign * step2;
	}
	else
	{
		if( cmd->buttons & ( IN_RIGHT|IN_MOVERIGHT ))
			ov->origin[1] += sign * step2;
		else if( cmd->buttons & ( IN_LEFT|IN_MOVELEFT ))
			ov->origin[1] -= sign * step2;
	}

	if( cmd->buttons & IN_ATTACK ) ov->flZoom += step;
	else if( cmd->buttons & IN_ATTACK2 ) ov->flZoom -= step;

	if( ov->flZoom == 0.0f ) ov->flZoom = 0.0001f; // to prevent disivion by zero

	return true;
}

/*
=================
CL_UpdateClientData

tell the client.dll about player origin, angles, fov, etc
=================
*/
void CL_UpdateClientData( void )
{
	client_data_t	cdat;

	if( cls.state != ca_active )
		return;

	memset( &cdat, 0, sizeof( cdat ) );

	VectorCopy( cl.viewangles, cdat.viewangles );
	VectorCopy( clgame.entities[cl.viewentity].origin, cdat.origin );
	cdat.iWeaponBits = cl.local.weapons;
	cdat.fov = cl.local.scr_fov;

	if( clgame.dllFuncs.pfnUpdateClientData( &cdat, cl.time ))
	{
		// grab changes if successful
		VectorCopy( cdat.viewangles, cl.viewangles );
		cl.local.scr_fov = cdat.fov;
	}
}

/*
=================
CL_CreateCmd
=================
*/
void CL_CreateCmd( void )
{
	usercmd_t		cmd;
	runcmd_t		*pcmd;
	vec3_t		angles;
	qboolean		active;
	int		input_override;
	int		i, ms;

	if( cls.state < ca_connected || cls.state == ca_cinematic )
		return;

	// store viewangles in case it's will be freeze
	VectorCopy( cl.viewangles, angles );
	ms = bound( 1, host.frametime * 1000, 255 );
	memset( &cmd, 0, sizeof( cmd ));
	input_override = 0;

	CL_SetSolidEntities();
	CL_PushPMStates();
	CL_SetSolidPlayers( cl.playernum );

	// message we are constructing.
	i = cls.netchan.outgoing_sequence & CL_UPDATE_MASK;   
	pcmd = &cl.commands[i];

	if( !cls.demoplayback )
	{
		pcmd->senttime = host.realtime;      
		memset( &pcmd->cmd, 0, sizeof( pcmd->cmd ));
		pcmd->receivedtime = -1.0;
		pcmd->processedfuncs = false;
		pcmd->heldback = false;
		pcmd->sendsize = 0;
		CL_ApplyAddAngle();
	}

	active = ( cls.state == ca_active && !cl.paused && !cls.demoplayback );
	clgame.dllFuncs.CL_CreateMove( host.frametime, &pcmd->cmd, active );

	CL_PopPMStates();

	if( !cls.demoplayback )
	{
		CL_ComputeClientInterpolationAmount( &pcmd->cmd );
		pcmd->cmd.lightlevel = cl.local.light_level;
		pcmd->cmd.msec = ms;
	}

	input_override |= CL_ProcessOverviewCmds( &pcmd->cmd );
	input_override |= CL_ProcessShowTexturesCmds( &pcmd->cmd );

	if(( cl.background && !cls.demoplayback ) || input_override || cls.changelevel )
	{
		VectorCopy( angles, pcmd->cmd.viewangles );
		VectorCopy( angles, cl.viewangles );
		pcmd->cmd.msec = 0;
	}

	// demo always have commands so don't overwrite them
	if( !cls.demoplayback ) cl.cmd = &pcmd->cmd;

	// predict all unacknowledged movements
	CL_PredictMovement( false );
}

void CL_WriteUsercmd( sizebuf_t *msg, int from, int to )
{
	usercmd_t	nullcmd;
	usercmd_t	*f, *t;

	ASSERT( from == -1 || ( from >= 0 && from < MULTIPLAYER_BACKUP ));
	ASSERT( to >= 0 && to < MULTIPLAYER_BACKUP );

	if( from == -1 )
	{
		memset( &nullcmd, 0, sizeof( nullcmd ));
		f = &nullcmd;
	}
	else
	{
		f = &cl.commands[from].cmd;
	}

	t = &cl.commands[to].cmd;

	// write it into the buffer
	MSG_WriteDeltaUsercmd( msg, f, t );
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds
===================
*/
void CL_WritePacket( void )
{
	sizebuf_t		buf;
	qboolean		send_command = false;
	byte		data[MAX_CMD_BUFFER];
	int		i, from, to, key, size;
	int		numbackup = 2;
	int		numcmds;
	int		newcmds;
	int		cmdnumber;
	
	// don't send anything if playing back a demo
	if( cls.demoplayback || cls.state < ca_connected || cls.state == ca_cinematic )
		return;

	CL_ComputePacketLoss ();

	MSG_Init( &buf, "ClientData", data, sizeof( data ));

	// Determine number of backup commands to send along
	numbackup = bound( 0, cl_cmdbackup->value, MAX_BACKUP_COMMANDS );
	if( cls.state == ca_connected ) numbackup = 0;

	// clamp cmdrate
	if( cl_cmdrate->value < 0.0f ) Cvar_SetValue( "cl_cmdrate", 0.0f );
	else if( cl_cmdrate->value > 100.0f ) Cvar_SetValue( "cl_cmdrate", 100.0f );

	// Check to see if we can actually send this command

	// In single player, send commands as fast as possible
	// Otherwise, only send when ready and when not choking bandwidth
	if( cl.maxclients == 1 || ( NET_IsLocalAddress( cls.netchan.remote_address ) && !host_limitlocal->value ))
		send_command = true;

	if(( host.realtime >= cls.nextcmdtime ) && Netchan_CanPacket( &cls.netchan ))
		send_command = true;

	if( cl.send_reply )
	{
		cl.send_reply = false;
		send_command = true;
	}

	// spectator is not sending cmds to server
	if( cls.spectator && cls.state == ca_active && cl.delta_sequence == cl.validsequence )
	{
		if( !( cls.demorecording && cls.demowaiting ) && cls.nextcmdtime + 1.0f > host.realtime )
			return;
	}

	if(( cls.netchan.outgoing_sequence - cls.netchan.incoming_acknowledged ) >= CL_UPDATE_MASK )
	{
		if(( host.realtime - cls.netchan.last_received ) > CONNECTION_PROBLEM_TIME )
		{
			Con_NPrintf( 1, "^3Warning:^1 Connection Problem^7\n" );
			cl.validsequence = 0;
		}
	}

	if( cl_nodelta->value )
		cl.validsequence = 0;

	if( send_command )
	{
		int	outgoing_sequence;
	
		if( cl_cmdrate->value > 0 ) // clamped between 10 and 100 fps
			cls.nextcmdtime = host.realtime + bound( 0.1f, ( 1.0f / cl_cmdrate->value ), 0.01f );
		else cls.nextcmdtime = host.realtime; // always able to send right away

		if( cls.lastoutgoingcommand == -1 )
		{
			outgoing_sequence = cls.netchan.outgoing_sequence;
			cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;
		}
		else outgoing_sequence = cls.lastoutgoingcommand + 1;

		// begin a client move command
		MSG_BeginClientCmd( &buf, clc_move );

		// save the position for a checksum byte
		key = MSG_GetRealBytesWritten( &buf );
		MSG_WriteByte( &buf, 0 );

		// write packet lossage percentation
		MSG_WriteByte( &buf, cls.packet_loss );

		// say how many backups we'll be sending
		MSG_WriteByte( &buf, numbackup );

		// how many real commands have queued up
		newcmds = ( cls.netchan.outgoing_sequence - cls.lastoutgoingcommand );

		// put an upper/lower bound on this
		newcmds = bound( 0, newcmds, MAX_TOTAL_CMDS );
		if( cls.state == ca_connected ) newcmds = 0;
	
		MSG_WriteByte( &buf, newcmds );

		numcmds = newcmds + numbackup;
		from = -1;

		for( i = numcmds - 1; i >= 0; i-- )
		{
			cmdnumber = ( cls.netchan.outgoing_sequence - i ) & CL_UPDATE_MASK;

			to = cmdnumber;
			CL_WriteUsercmd( &buf, from, to );
			from = to;

			if( MSG_CheckOverflow( &buf ))
				Host_Error( "CL_WritePacket: overflowed command buffer (%i bytes)\n", MAX_CMD_BUFFER );
		}

		// calculate a checksum over the move commands
		size = MSG_GetRealBytesWritten( &buf ) - key - 1;
		buf.pData[key] = CRC32_BlockSequence( buf.pData + key + 1, size, cls.netchan.outgoing_sequence );

		// message we are constructing.
		i = cls.netchan.outgoing_sequence & CL_UPDATE_MASK;
	
		// determine if we need to ask for a new set of delta's.
		if( cl.validsequence && (cls.state == ca_active) && !( cls.demorecording && cls.demowaiting ))
		{
			cl.delta_sequence = cl.validsequence;

			MSG_BeginClientCmd( &buf, clc_delta );
			MSG_WriteByte( &buf, cl.validsequence & 0xFF );
		}
		else
		{
			// request delta compression of entities
			cl.delta_sequence = -1;
		}

		if( MSG_CheckOverflow( &buf ))
			Host_Error( "CL_WritePacket: overflowed command buffer (%i bytes)\n", MAX_CMD_BUFFER );

		// remember outgoing command that we are sending
		cls.lastoutgoingcommand = cls.netchan.outgoing_sequence;

		// update size counter for netgraph
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].sendsize = MSG_GetNumBytesWritten( &buf );
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].heldback = false;

		// composite the rest of the datagram..
		if( MSG_GetNumBitsWritten( &cls.datagram ) <= MSG_GetNumBitsLeft( &buf ))
			MSG_WriteBits( &buf, MSG_GetData( &cls.datagram ), MSG_GetNumBitsWritten( &cls.datagram ));
		MSG_Clear( &cls.datagram );

		// deliver the message (or update reliable)
		Netchan_Transmit( &cls.netchan, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ));
	}
	else
	{
		// mark command as held back so we'll send it next time
		cl.commands[cls.netchan.outgoing_sequence & CL_UPDATE_MASK].heldback = true;

		// increment sequence number so we can detect that we've held back packets.
		cls.netchan.outgoing_sequence++;
	}

	if( cls.demorecording && numbackup > 0 )
	{
		// Back up one because we've incremented outgoing_sequence each frame by 1 unit
		cmdnumber = ( cls.netchan.outgoing_sequence - 1 ) & CL_UPDATE_MASK;
		CL_WriteDemoUserCmd( cmdnumber );
	}

	// update download/upload slider.
	Netchan_UpdateProgress( &cls.netchan );
}

/*
=================
CL_SendCommand

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCommand( void )
{
	// we create commands even if a demo is playing,
	CL_CreateCmd();

	// clc_move, userinfo etc
	CL_WritePacket();
}

/*
==================
CL_Quit_f
==================
*/
void CL_Quit_f( void )
{
	CL_Disconnect();
	Sys_Quit();
}

/*
================
CL_Drop

Called after an Host_Error was thrown
================
*/
void CL_Drop( void )
{
	if( !cls.initialized )
		return;
	CL_Disconnect();
}

/*
=======================
CL_SendConnectPacket

We have gotten a challenge from the server, so try and
connect.
======================
*/
void CL_SendConnectPacket( void )
{
	netadr_t	adr;
	int	port;

	if( !NET_StringToAdr( cls.servername, &adr ))
	{
		MsgDev( D_INFO, "CL_SendConnectPacket: bad server address\n");
		cls.connect_time = 0;
		return;
	}

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );
	port = Cvar_VariableValue( "net_qport" );

	Netchan_OutOfBandPrint( NS_CLIENT, adr, "connect %i %i %i \"%s\"\n", PROTOCOL_VERSION, port, cls.challenge, cls.userinfo );
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void )
{
	netadr_t	adr;

	// if the local server is running and we aren't then connect
	if( cls.state == ca_disconnected && SV_Active( ))
	{
		cls.signon = 0;
		cls.state = ca_connecting;
		Q_strncpy( cls.servername, "localhost", sizeof( cls.servername ));
		// we don't need a challenge on the localhost
		CL_SendConnectPacket();
		return;
	}

	// resend if we haven't gotten a reply yet
	if( cls.demoplayback || cls.state != ca_connecting )
		return;

	if(( host.realtime - cls.connect_time ) < 10.0f )
		return;

	if( !NET_StringToAdr( cls.servername, &adr ))
	{
		MsgDev( D_ERROR, "CL_CheckForResend: bad server address\n" );
		cls.state = ca_disconnected;
		cls.signon = 0;
		return;
	}

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );
	cls.connect_time = host.realtime; // for retransmit requests

	MsgDev( D_NOTE, "Connecting to %s...\n", cls.servername );
	Netchan_OutOfBandPrint( NS_CLIENT, adr, "getchallenge\n" );
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f( void )
{
	string	server;

	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: connect <server>\n" );
		return;	
	}

	Q_strncpy( server, Cmd_Argv( 1 ), sizeof( server ));

	if( Host_ServerState())
	{	
		// if running a local server, kill it and reissue
		Q_strncpy( host.finalmsg, "Server quit", MAX_STRING );
		SV_Shutdown( false );
	}

	NET_Config( true ); // allow remote

	Msg( "server %s\n", server );
	CL_Disconnect();

	cls.state = ca_connecting;
	Q_strncpy( cls.servername, server, sizeof( cls.servername ));
	cls.connect_time = MAX_HEARTBEAT; // CL_CheckForResend() will fire immediately
	cls.spectator = false;
	cls.signon = 0;
}

/*
=====================
CL_Rcon_f

Send the rest of the command line over as
an unconnected command.
=====================
*/
void CL_Rcon_f( void )
{
	char	message[1024];
	netadr_t	to;
	int	i;

	if( !rcon_client_password->string )
	{
		Msg( "You must set 'rcon_password' before issuing an rcon command.\n" );
		return;
	}

	message[0] = (char)255;
	message[1] = (char)255;
	message[2] = (char)255;
	message[3] = (char)255;
	message[4] = 0;

	NET_Config( true );	// allow remote

	Q_strcat( message, "rcon " );
	Q_strcat( message, rcon_client_password->string );
	Q_strcat( message, " " );

	for( i = 1; i < Cmd_Argc(); i++ )
	{
		Q_strcat( message, Cmd_Argv( i ));
		Q_strcat( message, " " );
	}

	if( cls.state >= ca_connected )
	{
		to = cls.netchan.remote_address;
	}
	else
	{
		if( !Q_strlen( rcon_address->string ))
		{
			Msg( "You must either be connected or set the 'rcon_address' cvar to issue rcon commands\n" );
			return;
		}

		NET_StringToAdr( rcon_address->string, &to );
		if( to.port == 0 ) to.port = MSG_BigShort( PORT_SERVER );
	}
	
	NET_SendPacket( NS_CLIENT, Q_strlen( message ) + 1, message, to );
}


/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState( void )
{
	S_StopAllSounds ( true );
	CL_ClearEffects ();
	CL_FreeEdicts ();

	CL_ClearPhysEnts ();

	// wipe the entire cl structure
	memset( &cl, 0, sizeof( cl ));
	MSG_Clear( &cls.netchan.message );
	memset( &clgame.fade, 0, sizeof( clgame.fade ));
	memset( &clgame.shake, 0, sizeof( clgame.shake ));
	Cvar_FullSet( "cl_background", "0", FCVAR_READ_ONLY );
	cl.maxclients = 1; // allow to drawing player in menu
	cl.mtime[0] = cl.mtime[1] = 1.0f; // because level starts from 1.0f second
	NetAPI_CancelAllRequests();

	cl.local.interp_amount = 0.1f;
	cl.local.scr_fov = 90.0f;

	Cvar_SetValue( "scr_download", 0.0f );
	Cvar_SetValue( "scr_loading", 0.0f );

	// restore real developer level
	host.developer = host.old_developer;
}

/*
=====================
CL_SendDisconnectMessage

Sends a disconnect message to the server
=====================
*/
void CL_SendDisconnectMessage( void )
{
	sizebuf_t	buf;
	byte	data[32];

	if( cls.state == ca_disconnected ) return;

	MSG_Init( &buf, "LastMessage", data, sizeof( data ));
	MSG_BeginClientCmd( &buf, clc_stringcmd );
	MSG_WriteString( &buf, "disconnect" );

	if( !cls.netchan.remote_address.type )
		cls.netchan.remote_address.type = NA_LOOPBACK;

	// make sure message will be delivered
	Netchan_Transmit( &cls.netchan, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ));
	Netchan_Transmit( &cls.netchan, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ));
	Netchan_Transmit( &cls.netchan, MSG_GetNumBytesWritten( &buf ), MSG_GetData( &buf ));
}

/*
=====================
CL_Disconnect

Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect( void )
{
	if( cls.state == ca_disconnected )
		return;

	cls.connect_time = 0;
	cls.changedemo = false;
	CL_Stop_f();

	// send a disconnect message to the server
	CL_SendDisconnectMessage();
	CL_ClearState ();

	S_StopBackgroundTrack ();
	SCR_EndLoadingPlaque (); // get rid of loading plaque

	// clear the network channel, too.
	Netchan_Clear( &cls.netchan );

	cls.state = ca_disconnected;
	cls.signon = 0;

	// restore gamefolder here (in case client was connected to another game)
	CL_ChangeGame( GI->gamefolder, true );

	// back to menu if developer mode set to "player" or "mapper"
	if( host.developer > 2 || CL_IsInMenu( ))
		return;

	UI_SetActiveMenu( true );
}

void CL_Disconnect_f( void )
{
	Host_Error( "Disconnected from server\n" );
}

void CL_Crashed( void )
{
	// already freed
	if( host.state == HOST_CRASHED ) return;
	if( host.type != HOST_NORMAL ) return;
	if( !cls.initialized ) return;

	host.state = HOST_CRASHED;

	CL_Stop_f(); // stop any demos

	// send a disconnect message to the server
	CL_SendDisconnectMessage();

	Host_WriteOpenGLConfig();
	Host_WriteConfig();	// write config
}

/*
=================
CL_LocalServers_f
=================
*/
void CL_LocalServers_f( void )
{
	netadr_t	adr;

	MsgDev( D_INFO, "Scanning for servers on the local network area...\n" );
	NET_Config( true ); // allow remote
	
	// send a broadcast packet
	adr.type = NA_BROADCAST;
	adr.port = MSG_BigShort( PORT_SERVER );

	Netchan_OutOfBandPrint( NS_CLIENT, adr, "info %i", PROTOCOL_VERSION );
}

/*
=================
CL_InternetServers_f
=================
*/
void CL_InternetServers_f( void )
{
	netadr_t	adr;
	char	fullquery[512] = "1\xFF" "0.0.0.0:0\0" "\\gamedir\\";

	MsgDev( D_INFO, "Scanning for servers on the internet area...\n" );
	NET_Config( true ); // allow remote

	if( !NET_StringToAdr( MASTERSERVER_ADR, &adr ) )
		MsgDev( D_INFO, "Can't resolve adr: %s\n", MASTERSERVER_ADR );

	Q_strcpy( &fullquery[22], GI->gamedir );

	NET_SendPacket( NS_CLIENT, Q_strlen( GI->gamedir ) + 23, fullquery, adr );

	// now we clearing the vgui request
	if( clgame.master_request != NULL )
		memset( clgame.master_request, 0, sizeof( net_request_t ));
	clgame.request_type = NET_REQUEST_GAMEUI;
}

/*
====================
CL_Packet_f

packet <destination> <contents>
Contents allows \n escape character
====================
*/
void CL_Packet_f( void )
{
	char	send[2048];
	char	*in, *out;
	int	i, l;
	netadr_t	adr;

	if( Cmd_Argc() != 3 )
	{
		Msg( "packet <destination> <contents>\n" );
		return;
	}

	NET_Config( true ); // allow remote

	if( !NET_StringToAdr( Cmd_Argv( 1 ), &adr ))
	{
		Msg( "Bad address\n" );
		return;
	}

	if( adr.port == 0 ) adr.port = MSG_BigShort( PORT_SERVER );

	in = Cmd_Argv( 2 );
	out = send + 4;
	send[0] = send[1] = send[2] = send[3] = (char)0xFF;

	l = Q_strlen( in );

	for( i = 0; i < l; i++ )
	{
		if( in[i] == '\\' && in[i+1] == 'n' )
		{
			*out++ = '\n';
			i++;
		}
		else *out++ = in[i];
	}
	*out = 0;

	NET_SendPacket( NS_CLIENT, out - send, send, adr );
}

/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
void CL_Reconnect_f( void )
{
	if( cls.state == ca_disconnected )
		return;

	S_StopAllSounds ( true );

	if( cls.state == ca_connected )
	{
		cls.demonum = cls.movienum = -1;	// not in the demo loop now
		cls.state = ca_connected;
		cls.signon = 0;

		// clear channel and stuff
		Netchan_Clear( &cls.netchan );
		MSG_Clear( &cls.netchan.message );

		MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, "new" );

		cl.validsequence = 0;		// haven't gotten a valid frame update yet
		cl.delta_sequence = -1;		// we'll request a full delta from the baseline
		cls.lastoutgoingcommand = -1;		// we don't have a backed up cmd history yet
		cls.nextcmdtime = host.realtime;	// we can send a cmd right away
		cl.last_command_ack = -1;

		CL_StartupDemoHeader ();
		return;
	}

	if( cls.servername[0] )
	{
		if( cls.state >= ca_connected )
		{
			CL_Disconnect();
			cls.connect_time = host.realtime - 1.5;
		}
		else cls.connect_time = MAX_HEARTBEAT; // fire immediately

		cls.demonum = cls.movienum = -1;	// not in the demo loop now
		cls.state = ca_connecting;
		cls.signon = 0;

		Msg( "reconnecting...\n" );
	}
}

/*
=================
CL_FixupColorStringsForInfoString

all the keys and values must be ends with ^7
=================
*/
void CL_FixupColorStringsForInfoString( const char *in, char *out )
{
	qboolean	hasPrefix = false;
	qboolean	endOfKeyVal = false;
	int	color = 7;
	int	count = 0;

	if( *in == '\\' )
	{
		*out++ = *in++;
		count++;
	}

	while( *in && count < MAX_INFO_STRING )
	{
		if( IsColorString( in ))
			color = ColorIndex( *(in+1));

		// color the not reset while end of key (or value) was found!
		if( *in == '\\' && color != 7 )
		{
			if( IsColorString( out - 2 ))
			{
				*(out - 1) = '7';
			}
			else
			{
				*out++ = '^';
				*out++ = '7';
				count += 2;
			}
			color = 7;
		}

		*out++ = *in++;
		count++;
	}

	// check the remaining value
	if( color != 7 )
	{
		// if the ends with another color rewrite it
		if( IsColorString( out - 2 ))
		{
			*(out - 1) = '7';
		}
		else
		{
			*out++ = '^';
			*out++ = '7';
			count += 2;
		}
	}

	*out = '\0';
}

/*
=================
CL_ParseStatusMessage

Handle a reply from a info
=================
*/
void CL_ParseStatusMessage( netadr_t from, sizebuf_t *msg )
{
	static char	infostring[MAX_INFO_STRING+8];
	char		*s = MSG_ReadString( msg );

	CL_FixupColorStringsForInfoString( s, infostring );

	// more info about servers
	MsgDev( D_INFO, "Server: %s, Game: %s\n", NET_AdrToString( from ), Info_ValueForKey( infostring, "gamedir" ));

	UI_AddServerToList( from, infostring );
}

/*
=================
CL_ParseNETInfoMessage

Handle a reply from a netinfo
=================
*/
void CL_ParseNETInfoMessage( netadr_t from, sizebuf_t *msg, const char *s )
{
	net_request_t	*nr;
	static char	infostring[MAX_INFO_STRING+8];
	int		i, context, type;
	int		errorBits = 0;
	char		*val;

	context = Q_atoi( Cmd_Argv( 1 ));
	type = Q_atoi( Cmd_Argv( 2 ));
	while( *s != '\\' ) s++; // fetching infostring

	// check for errors
	val = Info_ValueForKey( s, "neterror" );

	if( !Q_stricmp( val, "protocol" ))
		SetBits( errorBits, NET_ERROR_PROTO_UNSUPPORTED );
	else if( !Q_stricmp( val, "undefined" ))
		SetBits( errorBits, NET_ERROR_UNDEFINED );

	CL_FixupColorStringsForInfoString( s, infostring );

	// find a request with specified context
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];

		if( nr->resp.context == context && nr->resp.type == type )
		{
			// setup the answer
			nr->resp.response = infostring;
			nr->resp.remote_address = from;
			nr->resp.error = NET_SUCCESS;
			nr->resp.ping = host.realtime - nr->timesend;

			if( nr->timeout <= host.realtime )
				SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
			SetBits( nr->resp.error, errorBits ); // misc error bits

			nr->pfnFunc( &nr->resp );

			if( !FBitSet( nr->flags, FNETAPI_MULTIPLE_RESPONSE ))
				memset( nr, 0, sizeof( *nr )); // done
			return;
		}
	}
}

/*
=================
CL_ProcessNetRequests

check for timeouts
=================
*/
void CL_ProcessNetRequests( void )
{
	net_request_t	*nr;
	int		i;

	// find a request with specified context
	for( i = 0; i < MAX_REQUESTS; i++ )
	{
		nr = &clgame.net_requests[i];
		if( !nr->pfnFunc ) continue;	// not used

		if( nr->timeout <= host.realtime )
		{
			// setup the answer
			SetBits( nr->resp.error, NET_ERROR_TIMEOUT );
			nr->resp.ping = host.realtime - nr->timesend;

			nr->pfnFunc( &nr->resp );
			memset( nr, 0, sizeof( *nr )); // done
		}
	}
}

//===================================================================
/*
===============
CL_SetupOverviewParams

Get initial overview values
===============
*/
void CL_SetupOverviewParams( void )
{
	ref_overview_t	*ov = &clgame.overView;
	float		mapAspect, screenAspect, aspect;

	ov->rotated = ( world.size[1] <= world.size[0] ) ? true : false;

	// calculate nearest aspect
	mapAspect = world.size[!ov->rotated] / world.size[ov->rotated];
	screenAspect = (float)glState.width / (float)glState.height;
	aspect = Q_max( mapAspect, screenAspect );

	ov->zNear = world.maxs[2];
	ov->zFar = world.mins[2];
	ov->flZoom = ( 8192.0f / world.size[ov->rotated] ) / aspect;

	VectorAverage( world.mins, world.maxs, ov->origin );

	memset( &cls.spectator_state, 0, sizeof( cls.spectator_state ));

	if( cls.spectator )
	{
		cls.spectator_state.playerstate.friction = 1;
		cls.spectator_state.playerstate.gravity = 1;
		cls.spectator_state.playerstate.number = cl.playernum + 1;
		cls.spectator_state.playerstate.usehull = 1;
		cls.spectator_state.playerstate.movetype = MOVETYPE_NOCLIP;
		cls.spectator_state.client.maxspeed = clgame.movevars.spectatormaxspeed;
	}
}

/*
======================
CL_PrepSound

Call before entering a new level, or after changing dlls
======================
*/
void CL_PrepSound( void )
{
	int	i, sndcount;

	MsgDev( D_NOTE, "CL_PrepSound: %s\n", clgame.mapname );
	for( i = 0, sndcount = 0; i < MAX_SOUNDS && cl.sound_precache[i+1][0]; i++ )
		sndcount++; // total num sounds

	S_BeginRegistration();

	for( i = 0; i < MAX_SOUNDS && cl.sound_precache[i+1][0]; i++ )
	{
		cl.sound_index[i+1] = S_RegisterSound( cl.sound_precache[i+1] );
		Cvar_SetValue( "scr_loading", scr_loading->value + 5.0f / sndcount );
		if( cl_allow_levelshots->value || host.developer > 3 || cl.background )
			SCR_UpdateScreen();
	}

	S_EndRegistration();
	cl.audio_prepped = true;
}

/*
=================
CL_PrepVideo

Call before entering a new level, or after changing dlls
=================
*/
void CL_PrepVideo( void )
{
	string	name, mapname;
	int	i, mdlcount;
	int	map_checksum; // dummy

	if( !cl.model_precache[1][0] )
		return; // no map loaded

	Cvar_SetValue( "scr_loading", 0.0f ); // reset progress bar
	MsgDev( D_NOTE, "CL_PrepVideo: %s\n", clgame.mapname );

	// let the render dll load the map
	Q_strncpy( mapname, cl.model_precache[1], MAX_STRING ); 
	Mod_LoadWorld( mapname, &map_checksum, cl.maxclients > 1 );
	cl.worldmodel = Mod_Handle( 1 ); // get world pointer
	Cvar_SetValue( "scr_loading", 25.0f );

	SCR_UpdateScreen();

	// make sure what map is valid
	if( !cls.demoplayback && map_checksum != cl.checksum )
		Host_Error( "Local map version differs from server: %i != '%i'\n", map_checksum, cl.checksum );

	for( i = 0, mdlcount = 0; i < MAX_MODELS && cl.model_precache[i+1][0]; i++ )
		mdlcount++; // total num models

	for( i = 0; i < MAX_MODELS && cl.model_precache[i+1][0]; i++ )
	{
		Q_strncpy( name, cl.model_precache[i+1], MAX_STRING );
		Mod_RegisterModel( name, i+1 );
		Cvar_SetValue( "scr_loading", scr_loading->value + 75.0f / mdlcount );
		if( cl_allow_levelshots->value || host.developer > 3 || cl.background )
			SCR_UpdateScreen();
	}

	// load tempent sprites (glowshell, muzzleflashes etc)
	CL_LoadClientSprites ();

	// invalidate all decal indexes
	memset( cl.decal_index, 0, sizeof( cl.decal_index ));

	CL_ClearWorld ();

	R_NewMap(); // tell the render about new map

	CL_SetupOverviewParams(); // set overview bounds

	// must be called after lightmap loading!
	clgame.dllFuncs.pfnVidInit();

	// release unused SpriteTextures
	for( i = 1; i < MAX_IMAGES; i++ )
	{
		if( !clgame.sprites[i].name[0] ) continue; // free slot
		if( clgame.sprites[i].needload != clgame.load_sequence )
			Mod_UnloadSpriteModel( &clgame.sprites[i] );
	}

	Mod_FreeUnused ();

	Cvar_SetValue( "scr_loading", 100.0f );	// all done

	if( host.developer <= 2 )
		Con_ClearNotify(); // clear any lines of console text

	SCR_UpdateScreen ();

	cl.video_prepped = true;
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, sizebuf_t *msg )
{
	char	*args;
	char	*c, buf[MAX_SYSPATH];
	int	len = sizeof( buf );
	int	dataoffset = 0;
	netadr_t	servadr;
	
	MSG_Clear( msg );
	MSG_ReadLong( msg ); // skip the -1

	args = MSG_ReadStringLine( msg );

	Cmd_TokenizeString( args );
	c = Cmd_Argv( 0 );

	MsgDev( D_NOTE, "CL_ConnectionlessPacket: %s : %s\n", NET_AdrToString( from ), c );

	// server connection
	if( !Q_strcmp( c, "client_connect" ))
	{
		if( cls.state == ca_connected )
		{
			MsgDev( D_INFO, "dup connect received. ignored\n");
			return;
		}

		Netchan_Setup( NS_CLIENT, &cls.netchan, from, Cvar_VariableValue( "net_qport" ), NULL, NULL );
		MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, "new" );
		cls.state = ca_connected;
		cls.signon = 0;

		cl.validsequence = 0;		// haven't gotten a valid frame update yet
		cl.delta_sequence = -1;		// we'll request a full delta from the baseline
		cls.lastoutgoingcommand = -1;		// we don't have a backed up cmd history yet
		cls.nextcmdtime = host.realtime;	// we can send a cmd right away
		cl.last_command_ack = -1;

		CL_StartupDemoHeader ();

		UI_SetActiveMenu( false );
	}
	else if( !Q_strcmp( c, "info" ))
	{
		// server responding to a status broadcast
		CL_ParseStatusMessage( from, msg );
	}
	else if( !Q_strcmp( c, "netinfo" ))
	{
		// server responding to a status broadcast
		CL_ParseNETInfoMessage( from, msg, args );
	}
	else if( !Q_strcmp( c, "cmd" ))
	{
		// remote command from gui front end
		if( !NET_IsLocalAddress( from ))
		{
			Msg( "Command packet from remote host. Ignored.\n" );
			return;
		}

		ShowWindow( host.hWnd, SW_RESTORE );
		SetForegroundWindow ( host.hWnd );
		args = MSG_ReadString( msg );
		Cbuf_AddText( args );
		Cbuf_AddText( "\n" );
	}
	else if( !Q_strcmp( c, "print" ))
	{
		// print command from somewhere
		Msg( "remote: %s\n", MSG_ReadString( msg ));
	}
	else if( !Q_strcmp( c, "ping" ))
	{
		// ping from somewhere
		Netchan_OutOfBandPrint( NS_CLIENT, from, "ack" );
	}
	else if( !Q_strcmp( c, "challenge" ))
	{
		// challenge from the server we are connecting to
		cls.challenge = Q_atoi( Cmd_Argv( 1 ));
		CL_SendConnectPacket();
		return;
	}
	else if( !Q_strcmp( c, "echo" ))
	{
		// echo request from server
		Netchan_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv( 1 ));
	}
	else if( !Q_strcmp( c, "disconnect" ))
	{
		// a disconnect message from the server, which will happen if the server
		// dropped the connection but it is still getting packets from us
		CL_Disconnect();
	}
	else if( !Q_strcmp( c, "f" ))
	{
		// serverlist got from masterserver
		while( MSG_GetNumBitsLeft( msg ) > 8 )
		{
			MSG_ReadBytes( msg, servadr.ip, sizeof( servadr.ip ));	// 4 bytes for IP
			servadr.port = MSG_ReadShort( msg );			// 2 bytes for Port
			servadr.type = NA_IP;

			// list is ends here
			if( !servadr.port )
			{
				if( clgame.request_type == NET_REQUEST_CLIENT && clgame.master_request != NULL )
				{
					net_request_t	*nr = clgame.master_request;
					net_adrlist_t	*list, **prev;

					// setup the answer
					nr->resp.remote_address = from;
					nr->resp.error = NET_SUCCESS;
					nr->resp.ping = host.realtime - nr->timesend;

					if( nr->timeout <= host.realtime )
						SetBits( nr->resp.error, NET_ERROR_TIMEOUT );

					MsgDev( D_INFO, "serverlist call: %s\n", NET_AdrToString( from ));
					nr->pfnFunc( &nr->resp );

					// throw the list, now it will be stored in user area
					prev = &((net_adrlist_t *)nr->resp.response);

					while( 1 )
					{
						list = *prev;
						if( !list ) break;

						// throw out any variables the game created
						*prev = list->next;
						Mem_Free( list );
					}
					memset( nr, 0, sizeof( *nr )); // done
					clgame.request_type = NET_REQUEST_CANCEL;
					clgame.master_request = NULL;
				}
				break;
			}

			if( clgame.request_type == NET_REQUEST_CLIENT && clgame.master_request != NULL )
			{
				net_request_t	*nr = clgame.master_request;
				net_adrlist_t	*list;

				// adding addresses into list
				list = Z_Malloc( sizeof( *list ));
				list->remote_address = servadr;
				list->next = nr->resp.response;
				nr->resp.response = list;
			}
			else if( clgame.request_type == NET_REQUEST_GAMEUI )
			{
				NET_Config( true ); // allow remote
				Netchan_OutOfBandPrint( NS_CLIENT, servadr, "info %i", PROTOCOL_VERSION );
			}
		}
	}
	else if( clgame.dllFuncs.pfnConnectionlessPacket( &from, args, buf, &len ))
	{
		// user out of band message (must be handled in CL_ConnectionlessPacket)
		if( len > 0 ) Netchan_OutOfBand( NS_SERVER, from, len, buf );
	}
	else MsgDev( D_ERROR, "bad connectionless packet from %s:\n%s\n", NET_AdrToString( from ), args );
}

/*
====================
CL_GetMessage

Handles recording and playback of demos, on top of NET_ code
====================
*/
int CL_GetMessage( byte *data, size_t *length )
{
	if( cls.demoplayback )
	{
		if( CL_DemoReadMessage( data, length ))
			return true;
		return false;
	}

	if( NET_GetPacket( NS_CLIENT, &net_from, data, length ))
		return true;
	return false;
}

/*
=================
CL_ReadNetMessage
=================
*/
void CL_ReadNetMessage( void )
{
	size_t	curSize;

	while( CL_GetMessage( net_message_buffer, &curSize ))
	{
		MSG_Init( &net_message, "ServerData", net_message_buffer, curSize );

		// check for connectionless packet (0xffffffff) first
		if( MSG_GetMaxBytes( &net_message ) >= 4 && *(int *)net_message.pData == -1 )
		{
			CL_ConnectionlessPacket( net_from, &net_message );
			continue;
		}

		// can't be a valid sequenced packet	
		if( cls.state < ca_connected ) continue;

		if( MSG_GetMaxBytes( &net_message ) < 8 )
		{
			MsgDev( D_WARN, "%s: runt packet\n", NET_AdrToString( net_from ));
			continue;
		}

		// packet from server
		if( !cls.demoplayback && !NET_CompareAdr( net_from, cls.netchan.remote_address ))
		{
			MsgDev( D_ERROR, "CL_ReadPackets: %s:sequenced packet without connection\n", NET_AdrToString( net_from ));
			continue;
		}

		if( !cls.demoplayback && !Netchan_Process( &cls.netchan, &net_message ))
			continue;	// wasn't accepted for some reason

		CL_ParseServerMessage( &net_message, true );
		cl.send_reply = true;
	}

	// build list of all solid entities per next frame (exclude clients)
	CL_SetSolidEntities();

	// check for fragmentation/reassembly related packets.
	if( cls.state != ca_disconnected && Netchan_IncomingReady( &cls.netchan ))
	{
		// process the incoming buffer(s)
		if( Netchan_CopyNormalFragments( &cls.netchan, &net_message, &curSize ))
		{
			MSG_Init( &net_message, "ServerData", net_message_buffer, curSize );
			CL_ParseServerMessage( &net_message, false );
		}
		
		if( Netchan_CopyFileFragments( &cls.netchan, &net_message ))
		{
			// remove from resource request stuff.
			CL_ProcessFile( true, cls.netchan.incomingfilename );
		}
	}

	Netchan_UpdateProgress( &cls.netchan );

	// check requests for time-expire
	CL_ProcessNetRequests();
}

/*
=================
CL_ReadPackets

Updates the local time and reads/handles messages
on client net connection.
=================
*/
void CL_ReadPackets( void )
{
	// decide the simulation time
	cl.oldtime = cl.time;

	if( !cls.demoplayback && !cl.paused )
		cl.time += host.frametime;

	// demo time
	if( cls.demorecording && !cls.demowaiting )
		cls.demotime += host.frametime;

	CL_ReadNetMessage();
#if 0
	// keep cheat cvars are unchanged
	if( cl.maxclients > 1 && cls.state == ca_active && host.developer <= 1 )
		Cvar_SetCheatState();
#endif
	// singleplayer never has connection timeout
	if( NET_IsLocalAddress( cls.netchan.remote_address ))
		return;

	// if in the debugger last frame, don't timeout
	if( host.frametime > 5.0f ) cls.netchan.last_received = Sys_DoubleTime();
          
	// check timeout
	if( cls.state >= ca_connected && !cls.demoplayback && cls.state != ca_cinematic )
	{
		if( host.realtime - cls.netchan.last_received > cl_timeout->value )
		{
			if( ++cl.timeoutcount > 5 ) // timeoutcount saves debugger
			{
				Msg( "\nServer connection timed out.\n" );
				CL_Disconnect();
				return;
			}
		}
	}
	else cl.timeoutcount = 0;
	
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply( void )
{
	// g-cont. my favorite message :-)
	MsgDev( D_INFO, "CL_SignonReply: %i\n", cls.signon );

	switch( cls.signon )
	{
	case 1:
		CL_ServerCommand( true, "sendents" );
		Mem_PrintStats();
		break;
	case 2:
		SCR_EndLoadingPlaque(); // allow normal screen updates
		if( cl.proxy_redirect && !cls.spectator )
		{
			MsgDev( D_ERROR, "CL_SignonReply: redirected to invalid server\n" );
			CL_Disconnect();
		}
		cl.proxy_redirect = false;
#if 0
		if( cls.demoplayback )
			Sequence_OnLevelLoad( clgame.mapname );
#endif
		break;
	}
}

/*
====================
CL_ProcessFile

A file has been received via the fragmentation/reassembly layer, put it in the right spot and
 see if we have finished downloading files.
====================
*/
void CL_ProcessFile( qboolean successfully_received, const char *filename )
{
	if( successfully_received )
		MsgDev( D_INFO, "received %s\n", filename );
	else MsgDev( D_WARN, "failed to download %s", filename );

	if( cls.downloadfileid == cls.downloadcount - 1 )
	{
		MsgDev( D_INFO, "Download completed, resuming connection\n" );
		FS_Rescan();
		MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, "continueloading" );
		cls.downloadfileid = 0;
		cls.downloadcount = 0;
		return;
	}

	cls.downloadfileid++;
}

/*
====================
CL_ServerCommand

send command to a server
====================
*/
void CL_ServerCommand( qboolean reliable, char *fmt, ... )
{
	char		string[MAX_SYSPATH];
	va_list		argptr;

	if( cls.state < ca_connecting )
		return;

	va_start( argptr, fmt );
	Q_vsprintf( string, fmt, argptr );
	va_end( argptr );

	if( reliable )
	{
		MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
		MSG_WriteString( &cls.netchan.message, string );
	}
	else
	{
		MSG_BeginClientCmd( &cls.datagram, clc_stringcmd );
		MSG_WriteString( &cls.datagram, string );
	}
}

//=============================================================================
/*
==============
CL_SetInfo_f
==============
*/
void CL_SetInfo_f( void )
{
	convar_t	*var;

	if( Cmd_Argc() == 1 )
	{
		Msg( "User info settings:\n" );
		Info_Print( cls.userinfo );
		Msg( "Total %i symbols\n", Q_strlen( cls.userinfo ));
		return;
	}

	if( Cmd_Argc() != 3 )
	{
		Msg( "usage: setinfo [ <key> <value> ]\n" );
		return;
	}

	// NOTE: some userinfo comed from cvars, e.g. cl_lw but we can call "setinfo cl_lw 1"
	// without real cvar changing. So we need to lookup for cvar first to make sure what
	// our key is not linked with console variable
	var = Cvar_FindVar( Cmd_Argv( 1 ));

	// make sure what cvar is existed and really part of userinfo
	if( var && FBitSet( var->flags, FCVAR_USERINFO ))
	{
		Cvar_DirectSet( var, Cmd_Argv( 2 ));
	}
	else if( Info_SetValueForKey( cls.userinfo, Cmd_Argv( 1 ), Cmd_Argv( 2 ), MAX_INFO_STRING ))
	{
		// send update only on successfully changed userinfo
		Cmd_ForwardToServer ();
	}
}

/*
==============
CL_Physinfo_f
==============
*/
void CL_Physinfo_f( void )
{
	Msg( "Phys info settings:\n" );
	Info_Print( cls.physinfo );
	Msg( "Total %i symbols\n", Q_strlen( cls.physinfo ));
}

/*
=================
CL_Precache_f

The server will send this command right
before allowing the client into the server
=================
*/
void CL_Precache_f( void )
{
	int	spawncount;

	spawncount = Q_atoi( Cmd_Argv( 1 ));

	CL_PrepSound();
	CL_PrepVideo();

	MSG_BeginClientCmd( &cls.netchan.message, clc_stringcmd );
	MSG_WriteString( &cls.netchan.message, va( "begin %i\n", spawncount ));
}

/*
==================
CL_FullServerinfo_f

Sent by server when serverinfo changes
==================
*/
void CL_FullServerinfo_f( void )
{
	if( Cmd_Argc() != 2 )
	{
		Msg( "Usage: fullserverinfo <complete info string>\n" );
		return;
	}

	Q_strncpy( cl.serverinfo, Cmd_Argv( 1 ), sizeof( cl.serverinfo ));
}

/*
=================
CL_Escape_f

Escape to menu from game
=================
*/
void CL_Escape_f( void )
{
	if( cls.key_dest == key_menu )
		return;

	// the final credits is running
	if( UI_CreditsActive( )) return;

	if( cls.state == ca_cinematic )
		SCR_NextMovie(); // jump to next movie
	else UI_SetActiveMenu( true );
}

/*
=================
CL_InitLocal
=================
*/
void CL_InitLocal( void )
{
	cls.state = ca_disconnected;
	cls.signon = 0;

	Cvar_RegisterVariable( &mp_decals );
	Cvar_RegisterVariable( &dev_overview );

	// register our variables
	cl_crosshair = Cvar_Get( "crosshair", "1", FCVAR_ARCHIVE, "show weapon chrosshair" );
	cl_nodelta = Cvar_Get ("cl_nodelta", "0", 0, "disable delta-compression for server messages" );
	cl_idealpitchscale = Cvar_Get( "cl_idealpitchscale", "0.8", 0, "how much to look up/down slopes and stairs when not using freelook" );
	cl_solid_players = Cvar_Get( "cl_solid_players", "1", 0, "Make all players not solid (can't traceline them)" );
	cl_interp = Cvar_Get( "ex_interp", "0.1", FCVAR_ARCHIVE, "Interpolate object positions starting this many seconds in past" ); 
	cl_timeout = Cvar_Get( "cl_timeout", "60", 0, "connect timeout (in-seconds)" );

	rcon_client_password = Cvar_Get( "rcon_password", "", 0, "remote control client password" );
	rcon_address = Cvar_Get( "rcon_address", "", 0, "remote control address" );

	// userinfo
	cl_nopred = Cvar_Get( "cl_nopred", "0", FCVAR_ARCHIVE|FCVAR_USERINFO, "disable client movement prediction" );
	name = Cvar_Get( "name", Sys_GetCurrentUser(), FCVAR_USERINFO|FCVAR_ARCHIVE|FCVAR_PRINTABLEONLY, "player name" );
	model = Cvar_Get( "model", "", FCVAR_USERINFO|FCVAR_ARCHIVE, "player model ('player' is a singleplayer model)" );
	cl_updaterate = Cvar_Get( "cl_updaterate", "20", FCVAR_USERINFO|FCVAR_ARCHIVE, "refresh rate of server messages" );
	cl_dlmax = Cvar_Get( "cl_dlmax", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "max allowed fragment size on download resources" );
	rate = Cvar_Get( "rate", "3500", FCVAR_USERINFO|FCVAR_ARCHIVE, "player network rate" );
	topcolor = Cvar_Get( "topcolor", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "player top color" );
	bottomcolor = Cvar_Get( "bottomcolor", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "player bottom color" );
	cl_lw = Cvar_Get( "cl_lw", "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable client weapon predicting" );
	Cvar_Get( "cl_lc", "1", FCVAR_ARCHIVE|FCVAR_USERINFO, "enable lag compensation" );
	Cvar_Get( "msg", "0", FCVAR_USERINFO|FCVAR_ARCHIVE, "message filter for server notifications" );
	Cvar_Get( "team", "", FCVAR_USERINFO, "player team" );
	Cvar_Get( "skin", "", FCVAR_USERINFO, "player skin" );

	cl_showfps = Cvar_Get( "cl_showfps", "1", FCVAR_ARCHIVE, "show client fps" );
	cl_nosmooth = Cvar_Get( "cl_nosmooth", "0", FCVAR_ARCHIVE, "disable smooth up stair climbing and interpolate position in multiplayer" );
	cl_smoothtime = Cvar_Get( "cl_smoothtime", "0.1", FCVAR_ARCHIVE, "time to smooth up" );
	cl_cmdbackup = Cvar_Get( "cl_cmdbackup", "10", FCVAR_ARCHIVE, "how many additional history commands are sent" );
	cl_cmdrate = Cvar_Get( "cl_cmdrate", "30", FCVAR_ARCHIVE, "Max number of command packets sent to server per second" );
	cl_draw_particles = Cvar_Get( "r_drawparticles", "1", FCVAR_CHEAT|FCVAR_ARCHIVE, "render particles" );
	cl_draw_tracers = Cvar_Get( "r_drawtracers", "1", FCVAR_CHEAT|FCVAR_ARCHIVE, "render tracers" );
	cl_draw_beams = Cvar_Get( "r_drawbeams", "1", FCVAR_CHEAT|FCVAR_ARCHIVE, "render beams" );
	cl_lightstyle_lerping = Cvar_Get( "cl_lightstyle_lerping", "0", FCVAR_ARCHIVE, "enables animated light lerping (perfomance option)" );
	cl_showerror = Cvar_Get( "cl_showerror", "0", FCVAR_ARCHIVE, "show prediction error" );
	cl_bmodelinterp = Cvar_Get( "cl_bmodelinterp", "1", FCVAR_ARCHIVE, "enable bmodel interpolation" );
	cl_clockreset = Cvar_Get( "cl_clockreset", "0.1", FCVAR_ARCHIVE, "frametime delta maximum value before reset" );
	cl_fixtimerate = Cvar_Get( "cl_fixtimerate", "7.5", FCVAR_ARCHIVE, "time in msec to client clock adjusting" );
	Cvar_Get( "hud_scale", "0", FCVAR_ARCHIVE|FCVAR_LATCH, "scale hud at current resolution" );
	Cvar_Get( "cl_background", "0", FCVAR_READ_ONLY, "indicate what background map is running" );
	cl_showevents = Cvar_Get( "cl_showevents", "0", FCVAR_ARCHIVE, "show events playback" );

	// these two added to shut up CS 1.5 about 'unknown' commands
	Cvar_Get( "lightgamma", "1", FCVAR_ARCHIVE, "ambient lighting level (legacy, unused)" );
	Cvar_Get( "direct", "1", FCVAR_ARCHIVE, "direct lighting level (legacy, unused)" );
	Cvar_Get( "voice_serverdebug", "0", 0, "debug voice (legacy, unused)" );

	// server commands
	Cmd_AddCommand ("noclip", NULL, "enable or disable no clipping mode" );
	Cmd_AddCommand ("notarget", NULL, "notarget mode (monsters do not see you)" );
	Cmd_AddCommand ("fullupdate", NULL, "re-init HUD on start demo recording" );
	Cmd_AddCommand ("give", NULL, "give specified item or weapon" );
	Cmd_AddCommand ("drop", NULL, "drop current/specified item or weapon" );
	Cmd_AddCommand ("gametitle", NULL, "show game logo" );
	Cmd_AddCommand ("god", NULL, "enable godmode" );
	Cmd_AddCommand ("fov", NULL, "set client field of view" );
		
	// register our commands
	Cmd_AddCommand ("pause", NULL, "pause the game (if the server allows pausing)" );
	Cmd_AddCommand ("localservers", CL_LocalServers_f, "collect info about local servers" );
	Cmd_AddCommand ("internetservers", CL_InternetServers_f, "collect info about internet servers" );
	Cmd_AddCommand ("cd", CL_PlayCDTrack_f, "Play cd-track (not real cd-player of course)" );

	Cmd_AddCommand ("setinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of userinfo)" );
	Cmd_AddCommand ("userinfo", CL_SetInfo_f, "examine or change the userinfo string (alias of setinfo)" );
	Cmd_AddCommand ("physinfo", CL_Physinfo_f, "print current client physinfo" );
	Cmd_AddCommand ("disconnect", CL_Disconnect_f, "disconnect from server" );
	Cmd_AddCommand ("record", CL_Record_f, "record a demo" );
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f, "play a demo" );
	Cmd_AddCommand ("killdemo", CL_DeleteDemo_f, "delete a specified demo file and demoshot" );
	Cmd_AddCommand ("startdemos", CL_StartDemos_f, "start playing back the selected demos sequentially" );
	Cmd_AddCommand ("demos", CL_Demos_f, "restart looping demos defined by the last startdemos command" );
	Cmd_AddCommand ("movie", CL_PlayVideo_f, "play a movie" );
	Cmd_AddCommand ("stop", CL_Stop_f, "stop playing or recording a demo" );
	Cmd_AddCommand ("info", NULL, "collect info about local servers with specified protocol" );
	Cmd_AddCommand ("escape", CL_Escape_f, "escape from game to menu" );
	Cmd_AddCommand ("togglemenu", CL_Escape_f, "toggle between game and menu" );
	Cmd_AddCommand ("pointfile", CL_ReadPointFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("linefile", CL_ReadLineFile_f, "show leaks on a map (if present of course)" );
	Cmd_AddCommand ("fullserverinfo", CL_FullServerinfo_f, "sent by server when serverinfo changes" );
	
	Cmd_AddCommand ("quit", CL_Quit_f, "quit from game" );
	Cmd_AddCommand ("exit", CL_Quit_f, "quit from game" );

	Cmd_AddCommand ("screenshot", CL_ScreenShot_f, "takes a screenshot of the next rendered frame" );
	Cmd_AddCommand ("snapshot", CL_SnapShot_f, "takes a snapshot of the next rendered frame" );
	Cmd_AddCommand ("envshot", CL_EnvShot_f, "takes a six-sides cubemap shot with specified name" );
	Cmd_AddCommand ("skyshot", CL_SkyShot_f, "takes a six-sides envmap (skybox) shot with specified name" );
	Cmd_AddCommand ("levelshot", CL_LevelShot_f, "same as \"screenshot\", used for create plaque images" );
	Cmd_AddCommand ("saveshot", CL_SaveShot_f, "used for create save previews with LoadGame menu" );
	Cmd_AddCommand ("demoshot", CL_DemoShot_f, "used for create demo previews with PlayDemo menu" );

	Cmd_AddCommand ("connect", CL_Connect_f, "connect to a server by hostname" );
	Cmd_AddCommand ("reconnect", CL_Reconnect_f, "reconnect to current level" );

	Cmd_AddCommand ("rcon", CL_Rcon_f, "sends a command to the server console (rcon_password and rcon_address required)" );

	// this is dangerous to leave in
// 	Cmd_AddCommand ("packet", CL_Packet_f, "send a packet with custom contents" );

	Cmd_AddCommand ("precache", CL_Precache_f, "precache specified resource (by index)" );
}

//============================================================================
/*
==================
CL_AdjustClock

slowly adjuct client clock
to smooth lag effect
==================
*/
void CL_AdjustClock( void )
{
	if( cl.timedelta == 0.0f || !cl_fixtimerate->value )
		return;

	if( cl_fixtimerate->value < 0.0f )
		Cvar_SetValue( "cl_fixtimerate", 7.5f );

	if( fabs( cl.timedelta ) >= 0.001f )
	{
		double	msec, adjust, sign;

		msec = ( cl.timedelta * 1000.0 );
		sign = ( msec < 0 ) ? 1.0 : -1.0;
		msec = fabs( msec );
		adjust = sign * ( cl_fixtimerate->value / 1000.0 );

		if( fabs( adjust ) < fabs( cl.timedelta ))
		{
			cl.timedelta += adjust;
			cl.time += adjust;
		}

		if( cl.oldtime > cl.time )
			cl.oldtime = cl.time;
	}
}

/*
==================
Host_ClientBegin

==================
*/
void Host_ClientBegin( void )
{
	// if client is not active, do nothing
	if( !cls.initialized ) return;

	// evaluate console animation
	Con_RunConsole ();

	// exec console commands
	Cbuf_Execute ();

	// finalize connection process if needs
	CL_CheckClientState();

	// tell the client.dll about client data
	CL_UpdateClientData();

	// if running the server locally, make intentions now
	if( SV_Active( )) CL_SendCommand ();
}

/*
==================
Host_ClientFrame

==================
*/
void Host_ClientFrame( void )
{
	// if client is not active, do nothing
	if( !cls.initialized ) return;

	// if running the server remotely, send intentions now after
	// the incoming messages have been read
	if( !SV_Active( )) CL_SendCommand ();

	clgame.dllFuncs.pfnFrame( host.frametime );

	// remember last received framenum
	CL_SetLastUpdate ();

	// read updates from server
	CL_ReadPackets ();

	// do prediction again in case we got
	// a new portion updates from server
	CL_RedoPrediction ();

//	Voice_Idle( host.frametime );

	// emit visible entities
	CL_EmitEntities ();

	// in case we lost connection
	CL_CheckForResend ();

//	while( CL_RequestMissingResources( ));

//	CL_HTTPUpdate ();

	// handle spectator movement
	CL_MoveSpectatorCamera();

	// catch changes video settings
	VID_CheckChanges();

	// process VGUI
	VGui_RunFrame ();

	// update the screen
	SCR_UpdateScreen ();

	// update audio
	SND_UpdateSound ();

	// animate lightestyles
	CL_RunLightStyles ();

	// decay dynamic lights
	CL_DecayLights ();

	// play avi-files
	SCR_RunCinematic ();

	// adjust client time
	CL_AdjustClock ();
}

//============================================================================

/*
====================
CL_Init
====================
*/
void CL_Init( void )
{
	if( host.type == HOST_DEDICATED )
		return; // nothing running on the client

	CL_InitLocal();

	R_Init();	// init renderer
	S_Init();	// init sound

	// unreliable buffer. unsed for unreliable commands and voice stream
	MSG_Init( &cls.datagram, "cls.datagram", cls.datagram_buf, sizeof( cls.datagram_buf ));

	if( !CL_LoadProgs( va( "%s/client.dll", GI->dll_path )))
		Host_Error( "can't initialize client.dll\n" );

	cls.initialized = true;
	cl.maxclients = 1; // allow to drawing player in menu
	cls.olddemonum = -1;
	cls.demonum = -1;
}

/*
===============
CL_Shutdown

===============
*/
void CL_Shutdown( void )
{
	// already freed
	if( !cls.initialized ) return;
	cls.initialized = false;

	MsgDev( D_INFO, "CL_Shutdown()\n" );

	Host_WriteOpenGLConfig ();
	Host_WriteVideoConfig ();

	CL_CloseDemoHeader();
	IN_Shutdown ();
	SCR_Shutdown ();
	CL_UnloadProgs ();

	FS_Delete( "demoheader.tmp" ); // remove tmp file
	SCR_FreeCinematic (); // release AVI's *after* client.dll because custom renderer may use them
	S_Shutdown ();
	R_Shutdown ();

	Con_Shutdown ();
}