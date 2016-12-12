/*
cl_view.c - player rendering positioning
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
#include "const.h"
#include "entity_types.h"
#include "gl_local.h"
#include "vgui_draw.h"


/*
===============
V_MergeOverviewRefdef

merge refdef with overview settings
===============
*/
void V_MergeOverviewRefdef( void )
{
	ref_overview_t	*ov = &clgame.overView;
	float		aspect;
	float		size_x, size_y;
	vec2_t		mins, maxs;

	if( !gl_overview->integer ) return;

	// NOTE: Xash3D may use 16:9 or 16:10 aspects
	aspect = (float)glState.width / (float)glState.height;

	size_x = fabs( 8192.0f / ov->flZoom );
	size_y = fabs( 8192.0f / (ov->flZoom * aspect ));

	// compute rectangle
	ov->xLeft = -(size_x / 2);
	ov->xRight = (size_x / 2);
	ov->xTop = -(size_y / 2);
	ov->xBottom = (size_y / 2);

	if( gl_overview->integer == 1 )
	{
		Con_NPrintf( 0, " Overview: Zoom %.2f, Map Origin (%.2f, %.2f, %.2f), Z Min %.2f, Z Max %.2f, Rotated %i\n",
		ov->flZoom, ov->origin[0], ov->origin[1], ov->origin[2], ov->zNear, ov->zFar, ov->rotated );
	}

	VectorCopy( ov->origin, cl.refdef.vieworg );
	cl.refdef.vieworg[2] = ov->zFar + ov->zNear;
	Vector2Copy( cl.refdef.vieworg, mins );
	Vector2Copy( cl.refdef.vieworg, maxs );

	mins[!ov->rotated] += ov->xLeft;
	maxs[!ov->rotated] += ov->xRight;
	mins[ov->rotated] += ov->xTop;
	maxs[ov->rotated] += ov->xBottom;

	cl.refdef.viewangles[0] = 90.0f;
	cl.refdef.viewangles[1] = 90.0f;
	cl.refdef.viewangles[2] = (ov->rotated) ? (ov->flZoom < 0.0f) ? 180.0f : 0.0f : (ov->flZoom < 0.0f) ? -90.0f : 90.0f;

	Mod_SetOrthoBounds( mins, maxs );
}

/*
===============
V_CalcViewRect

calc frame rectangle (Quake1 style)
===============
*/
void V_CalcViewRect( void )
{
	int	size, sb_lines;

	if( scr_viewsize->integer >= 120 )
		sb_lines = 0;		// no status bar at all
	else if( scr_viewsize->integer >= 110 )
		sb_lines = 24;		// no inventory
	else sb_lines = 48;

	size = Q_min( scr_viewsize->integer, 100 );

	cl.refdef.viewport[2] = scr_width->integer * size / 100;
	cl.refdef.viewport[3] = scr_height->integer * size / 100;

	if( cl.refdef.viewport[3] > scr_height->integer - sb_lines )
		cl.refdef.viewport[3] = scr_height->integer - sb_lines;
	if( cl.refdef.viewport[3] > scr_height->integer )
		cl.refdef.viewport[3] = scr_height->integer;

	cl.refdef.viewport[0] = ( scr_width->integer - cl.refdef.viewport[2] ) / 2;
	cl.refdef.viewport[1] = ( scr_height->integer - sb_lines - cl.refdef.viewport[3] ) / 2;

}

/*
===============
V_SetupRefDef

update refdef values each frame
===============
*/
void V_SetupRefDef( void )
{
	cl_entity_t	*clent;

	// compute viewport rectangle
	V_CalcViewRect();

	clent = CL_GetLocalPlayer ();

	clgame.entities->curstate.scale = clgame.movevars.waveHeight;

	cl.refdef.movevars = &clgame.movevars;
	cl.refdef.health = cl.frame.client.health;
	cl.refdef.playernum = cl.playernum;
	cl.refdef.max_entities = clgame.maxEntities;
	cl.refdef.maxclients = cl.maxclients;
	cl.refdef.time = cl.time;
	cl.refdef.frametime = cl.time - cl.oldtime;
	cl.refdef.demoplayback = cls.demoplayback;
	cl.refdef.viewsize = scr_viewsize->integer;
	cl.refdef.onlyClientDraw = 0;	// reset clientdraw
	cl.refdef.hardware = true;	// always true
	cl.refdef.spectator = (clent->curstate.spectator != 0);
	cl.refdef.smoothing = cl.first_frame; // NOTE: currently this used to prevent ugly un-duck effect while level is changed
	cl.scr_fov = bound( 1.0f, cl.scr_fov, 179.0f );
	cl.refdef.nextView = 0;

	// calc FOV
	cl.refdef.fov_x = cl.scr_fov; // this is a final fov value
	cl.refdef.fov_y = V_CalcFov( &cl.refdef.fov_x, cl.refdef.viewport[2], cl.refdef.viewport[3] );

	// adjust FOV for widescreen
	if( glState.wideScreen && r_adjust_fov->integer )
		V_AdjustFov( &cl.refdef.fov_x, &cl.refdef.fov_y, cl.refdef.viewport[2], cl.refdef.viewport[3], false );

	if( CL_IsPredicted( ) && !cl.first_frame )
	{
		VectorCopy( cl.predicted.origin, cl.refdef.simorg );
		VectorCopy( cl.predicted.velocity, cl.refdef.simvel );
		VectorCopy( cl.predicted.viewofs, cl.refdef.viewheight );
		VectorCopy( cl.predicted.punchangle, cl.refdef.punchangle );
		cl.refdef.onground = ( cl.predicted.onground == -1 ) ? false : true;
		cl.refdef.waterlevel = cl.predicted.waterlevel;
	}
	else
	{
		VectorCopy( cl.frame.client.origin, cl.refdef.simorg );
		VectorCopy( cl.frame.client.view_ofs, cl.refdef.viewheight );
		VectorCopy( cl.frame.client.velocity, cl.refdef.simvel );
		VectorCopy( cl.frame.client.punchangle, cl.refdef.punchangle );
		cl.refdef.onground = (cl.frame.client.flags & FL_ONGROUND) ? 1 : 0;
		cl.refdef.waterlevel = cl.frame.client.waterlevel;
	}

	// setup the viewent variables
	if( cl_lw->value ) clgame.viewent.curstate.modelindex = cl.predicted.viewmodel;
	else clgame.viewent.curstate.modelindex = cl.frame.client.viewmodel;
	clgame.viewent.model = Mod_Handle( clgame.viewent.curstate.modelindex );
	clgame.viewent.curstate.number = cl.playernum + 1;
	clgame.viewent.curstate.entityType = ET_NORMAL;
	clgame.viewent.index = cl.playernum + 1;

	// calc refdef first so viewent can get an actual
	// player position, angles etc
	if( FBitSet( host.features, ENGINE_FIXED_FRAMERATE ))
	{
		clgame.dllFuncs.pfnCalcRefdef( &cl.refdef );
		V_MergeOverviewRefdef();

		R_ClearScene ();
		CL_AddEntities ();
	}
}

/*
==================
V_PreRender

==================
*/
qboolean V_PreRender( void )
{
	// too early
	if( !glw_state.initialized )
		return false;

	if( host.state == HOST_NOFOCUS )
		return false;

	if( host.state == HOST_SLEEP )
		return false;

	// if the screen is disabled (loading plaque is up)
	if( cls.disable_screen )
	{
		if(( host.realtime - cls.disable_screen ) > cl_timeout->value )
		{
			MsgDev( D_ERROR, "V_PreRender: loading plaque timed out\n" );
			cls.disable_screen = 0.0f;
		}
		return false;
	}
	
	R_BeginFrame( !cl.refdef.paused );

	return true;
}

//============================================================================

/*
==================
V_RenderView

==================
*/
void V_RenderView( void )
{
	if( !cl.video_prepped || ( UI_IsVisible() && !cl.background ))
		return; // still loading

	if( !FBitSet( host.features, ENGINE_FIXED_FRAMERATE ))
	{
		if( cl.frame.valid && ( cl.force_refdef || !cl.refdef.paused ))
		{
			cl.force_refdef = false;

			R_ClearScene ();
			CL_AddEntities ();
			V_SetupRefDef ();
		}
	}

	R_Set2DMode( false );
	SCR_AddDirtyPoint( 0, 0 );
	SCR_AddDirtyPoint( scr_width->integer - 1, scr_height->integer - 1 );

	tr.framecount++;	// g-cont. keep actual frame for all viewpasses

	if( !FBitSet( host.features, ENGINE_FIXED_FRAMERATE ))
	{
		do
		{
			clgame.dllFuncs.pfnCalcRefdef( &cl.refdef );
			V_MergeOverviewRefdef();
			R_RenderFrame( &cl.refdef, true );
			cl.refdef.onlyClientDraw = false;
		} while( cl.refdef.nextView );
	}
	else R_RenderFrame( &cl.refdef, true );

	// draw debug triangles on a server
	SV_DrawDebugTriangles ();
}

/*
==================
V_PostRender

==================
*/
void V_PostRender( void )
{
	static double	oldtime;
	qboolean		draw_2d = false;

	R_Set2DMode( true );

	if( cls.state == ca_active && cls.scrshot_action != scrshot_mapshot )
	{
		SCR_TileClear();
		CL_DrawHUD( CL_ACTIVE );
		VGui_Paint();
	}

	switch( cls.scrshot_action )
	{
	case scrshot_inactive:
	case scrshot_normal:
	case scrshot_snapshot:
		draw_2d = true;
		break;
	}

	if( draw_2d )
	{
		SCR_RSpeeds();
		SCR_NetSpeeds();
		SCR_DrawNetGraph();
		SV_DrawOrthoTriangles();
		CL_DrawDemoRecording();
		CL_DrawHUD( CL_CHANGELEVEL );
		R_ShowTextures();
		Con_DrawConsole();
		UI_UpdateMenu( host.realtime );
		Con_DrawVersion();
		Con_DrawDebug(); // must be last

		if( !FBitSet( host.features, ENGINE_FIXED_FRAMERATE ))
			S_ExtraUpdate();
	}

	if( FBitSet( host.features, ENGINE_FIXED_FRAMERATE ))
	{
		// don't update sound too fast
		if(( host.realtime - oldtime ) >= HOST_FRAMETIME )
		{
			oldtime = host.realtime;
			CL_ExtraUpdate();
		}
	}

	SCR_MakeScreenShot();
	R_EndFrame();
}