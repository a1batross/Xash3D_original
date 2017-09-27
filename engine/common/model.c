/*
model.c - modelloader
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

#include "mod_local.h"
#include "sprite.h"
#include "mathlib.h"
#include "alias.h"
#include "studio.h"
#include "wadfile.h"
#include "world.h"
#include "gl_local.h"
#include "features.h"
#include "client.h"
#include "server.h"			// LUMP_ error codes

#define MAX_SIDE_VERTS		512	// per one polygon

world_static_t	world;

byte		*mod_base;
byte		*com_studiocache;		// cache for submodels
static model_t	*com_models[MAX_MODELS];	// shared replacement modeltable
static model_t	cm_models[MAX_MODELS];
static int	cm_nummodels = 0;
static byte	visdata[MAX_MAP_LEAFS/8];	// intermediate buffer
int		bmodel_version;		// global stuff to detect bsp version
char		modelname[64];		// short model name (without path and ext)
convar_t		*mod_studiocache;
convar_t		*r_wadtextures;
static wadlist_t	wadlist;
		
model_t		*loadmodel;
model_t		*worldmodel;

/*
===============================================================================

			MOD COMMON UTILS

===============================================================================
*/
/*
================
Mod_SetupHulls
================
*/
int Mod_ArrayUsage( const char *szItem, int items, int maxitems, int itemsize )
{
	float	percentage = maxitems ? (items * 100.0f / maxitems) : 0.0f;

	Msg( "%-12s  %7i/%-7i  %7i/%-7i  (%4.1f%%)", szItem, items, maxitems, items * itemsize, maxitems * itemsize, percentage );

	if( percentage > 99.9f )
		Msg( "^1SIZE OVERFLOW!!!^7\n" );
	else if( percentage > 95.0f )
		Msg( "^3SIZE DANGER!^7\n" );
	else if( percentage > 80.0f )
		Msg( "^2VERY FULL!^7\n" );
	else Msg( "\n" );

	return items * itemsize;
}

/*
================
Mod_SetupHulls
================
*/
int Mod_GlobUsage( const char *szItem, int itemstorage, int maxstorage )
{
	float	percentage = maxstorage ? (itemstorage * 100.0f / maxstorage) : 0.0f;

	Msg( "%-12s     [variable]    %7i/%-7i  (%4.1f%%)", szItem, itemstorage, maxstorage, percentage );

	if( percentage > 99.9f )
		Msg( "^1SIZE OVERFLOW!!!^7\n" );
	else if( percentage > 95.0f )
		Msg( "^3SIZE DANGER!^7\n" );
	else if( percentage > 80.0f )
		Msg( "^2VERY FULL!^7\n" );
	else Msg( "\n" );

	return itemstorage;
}

/*
=============
Mod_PrintBSPFileSizes

Dumps info about current file
=============
*/
void Mod_PrintBSPFileSizes_f( void )
{
	int	totalmemory = 0;
	model_t	*w = worldmodel;

	if( !w || !w->numsubmodels )
	{
		Msg( "No map loaded\n" );
		return;
	}

	Msg( "\n" );
	Msg( "Object names  Objects/Maxobjs  Memory / Maxmem  Fullness\n" );
	Msg( "------------  ---------------  ---------------  --------\n" );

	totalmemory += Mod_ArrayUsage( "models",	w->numsubmodels,	MAX_MAP_MODELS,		sizeof( dmodel_t ));
	totalmemory += Mod_ArrayUsage( "planes",	w->numplanes,	MAX_MAP_PLANES,		sizeof( dplane_t ));
	totalmemory += Mod_ArrayUsage( "vertexes",	w->numvertexes,	MAX_MAP_VERTS,		sizeof( dvertex_t ));
	totalmemory += Mod_ArrayUsage( "nodes",		w->numnodes,	MAX_MAP_NODES,		sizeof( dnode_t ));
	totalmemory += Mod_ArrayUsage( "texinfos",	w->numtexinfo,	MAX_MAP_TEXINFO,		sizeof( dtexinfo_t ));
	totalmemory += Mod_ArrayUsage( "faces",		w->numsurfaces,	MAX_MAP_FACES,		sizeof( dface_t ));
	if( world.version == XTBSP_VERSION )
		totalmemory += Mod_ArrayUsage( "clipnodes", w->numclipnodes, MAX_MAP_CLIPNODES * 3,	sizeof( dclipnode_t ));
	else
		totalmemory += Mod_ArrayUsage( "clipnodes", w->numclipnodes, MAX_MAP_CLIPNODES,		sizeof( dclipnode_t ));
	totalmemory += Mod_ArrayUsage( "leaves",	w->numleafs,	MAX_MAP_LEAFS,		sizeof( dleaf_t ));
	totalmemory += Mod_ArrayUsage( "marksurfaces",	w->nummarksurfaces,	MAX_MAP_MARKSURFACES,	sizeof( dmarkface_t ));
	totalmemory += Mod_ArrayUsage( "surfedges",	w->numsurfedges,	MAX_MAP_SURFEDGES,		sizeof( dsurfedge_t ));
	totalmemory += Mod_ArrayUsage( "edges",		w->numedges,	MAX_MAP_EDGES,		sizeof( dedge_t ));

	totalmemory += Mod_GlobUsage( "texdata",	world.texdatasize,	MAX_MAP_MIPTEX );
	totalmemory += Mod_GlobUsage( "lightdata",	world.litdatasize,	MAX_MAP_LIGHTING );
	totalmemory += Mod_GlobUsage( "deluxemap",	world.vecdatasize,	MAX_MAP_LIGHTING );
	totalmemory += Mod_GlobUsage( "visdata",	world.visdatasize,	MAX_MAP_VISIBILITY );
	totalmemory += Mod_GlobUsage( "entdata",	world.entdatasize,	MAX_MAP_ENTSTRING );

	Msg( "=== Total BSP file data space used: %s ===\n", Q_memprint( totalmemory ));
	Msg( "World size ( %g %g %g ) units\n", world.size[0], world.size[1], world.size[2] );
	Msg( "Supports transparency world water: %s\n", world.water_alpha ? "Yes" : "No" );
	Msg( "original name: ^1%s\n", worldmodel->name );
	Msg( "internal name: %s\n", (world.message[0]) ? va( "^2%s", world.message ) : "none" );
	Msg( "map compiler: %s\n", (world.compiler[0]) ? va( "^3%s", world.compiler ) : "unknown" );
}

/*
================
Mod_Modellist_f
================
*/
void Mod_Modellist_f( void )
{
	int	i, nummodels;
	model_t	*mod;

	Msg( "\n" );
	Msg( "-----------------------------------\n" );

	for( i = nummodels = 0, mod = cm_models; i < cm_nummodels; i++, mod++ )
	{
		if( !mod->name[0] )
			continue; // free slot

		Msg( "%s%s\n", mod->name, (mod->type == mod_bad) ? " (DEFAULTED)" : "" );
		nummodels++;
	}

	Msg( "-----------------------------------\n" );
	Msg( "%i total models\n", nummodels );
	Msg( "\n" );
}

/*
===================
Mod_DecompressVis
===================
*/
static void Mod_DecompressVis( const byte *in, const byte *inend, byte *out, byte *outend )
{
	byte	*outstart = out;
	int	c;

	while( out < outend )
	{
		if( in == inend )
		{
			MsgDev( D_WARN, "Mod_DecompressVis: input underrun (decompressed %i of %i output bytes)\n",
			(int)(out - outstart), (int)(outend - outstart));
			return;
		}

		c = *in++;

		if( c )
		{
			*out++ = c;
		}
		else
		{
			if( in == inend )
			{
				MsgDev( D_NOTE, "Mod_DecompressVis: input underrun (during zero-run) (decompressed %i of %i output bytes)\n",
				(int)(out - outstart), (int)(outend - outstart));
				return;
			}

			for( c = *in++; c > 0; c-- )
			{
				if( out == outend )
				{
					MsgDev( D_NOTE, "Mod_DecompressVis: output overrun (decompressed %i of %i output bytes)\n",
					(int)(out - outstart), (int)(outend - outstart));
					return;
				}
				*out++ = 0;
			}
		}
	}
}

/*
==================
Mod_PointInLeaf

==================
*/
mleaf_t *Mod_PointInLeaf( const vec3_t p, mnode_t *node )
{
	ASSERT( node != NULL );

	while( 1 )
	{
		if( node->contents < 0 )
			return (mleaf_t *)node;
		node = node->children[PlaneDiff( p, node->plane ) <= 0];
	}

	// never reached
	return NULL;
}

/*
==================
Mod_GetPVSForPoint

Returns PVS data for a given point
NOTE: can return NULL
==================
*/
byte *Mod_GetPVSForPoint( const vec3_t p )
{
	mnode_t	*node;
	mleaf_t	*leaf = NULL;

	ASSERT( worldmodel != NULL );

	node = worldmodel->nodes;

	while( 1 )
	{
		if( node->contents < 0 )
		{
			leaf = (mleaf_t *)node;
			break; // we found a leaf
		}
		node = node->children[PlaneDiff( p, node->plane ) <= 0];
	}

	if( leaf && leaf->cluster >= 0 )
		return world.visdata + leaf->cluster * world.visbytes;
	return NULL;
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

==================
*/
static void Mod_FatPVS_RecursiveBSPNode( const vec3_t org, float radius, byte *visbuffer, int visbytes, mnode_t *node )
{
	int	i;

	while( node->contents >= 0 )
	{
		float d = PlaneDiff( org, node->plane );

		if( d > radius )
			node = node->children[0];
		else if( d < -radius )
			node = node->children[1];
		else
		{
			// go down both sides
			Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, visbytes, node->children[0] );
			node = node->children[1];
		}
	}

	// if this leaf is in a cluster, accumulate the vis bits
	if(((mleaf_t *)node)->cluster >= 0 )
	{
		byte	*vis = world.visdata + ((mleaf_t *)node)->cluster * world.visbytes;

		for( i = 0; i < visbytes; i++ )
			visbuffer[i] |= vis[i];
	}
}

/*
==================
Mod_FatPVS_RecursiveBSPNode

Calculates a PVS that is the inclusive or of all leafs
within radius pixels of the given point.
==================
*/
int Mod_FatPVS( const vec3_t org, float radius, byte *visbuffer, int visbytes, qboolean merge, qboolean fullvis )
{
	mleaf_t	*leaf = Mod_PointInLeaf( org, worldmodel->nodes );
	int	bytes = world.visbytes;

	bytes = Q_min( bytes, visbytes );

	// enable full visibility for some reasons
	if( fullvis || !world.visclusters || !leaf || leaf->cluster < 0 )
	{
		memset( visbuffer, 0xFF, bytes );
		return bytes;
	}

	if( !merge ) memset( visbuffer, 0x00, bytes );

	Mod_FatPVS_RecursiveBSPNode( org, radius, visbuffer, bytes, worldmodel->nodes );

	return bytes;
}

/*
======================================================================

LEAF LISTING

======================================================================
*/
static void Mod_BoxLeafnums_r( leaflist_t *ll, mnode_t *node )
{
	int	sides;

	while( 1 )
	{
		if( node->contents == CONTENTS_SOLID )
			return;

		if( node->contents < 0 )
		{
			mleaf_t	*leaf = (mleaf_t *)node;

			// it's a leaf!
			if( ll->count >= ll->maxcount )
			{
				ll->overflowed = true;
				return;
			}

			ll->list[ll->count++] = leaf->cluster;
			return;
		}
	
		sides = BOX_ON_PLANE_SIDE( ll->mins, ll->maxs, node->plane );

		if( sides == 1 )
		{
			node = node->children[0];
		}
		else if( sides == 2 )
		{
			node = node->children[1];
		}
		else
		{
			// go down both
			if( ll->topnode == -1 )
				ll->topnode = node - worldmodel->nodes;
			Mod_BoxLeafnums_r( ll, node->children[0] );
			node = node->children[1];
		}
	}
}

/*
==================
Mod_BoxLeafnums
==================
*/
int Mod_BoxLeafnums( const vec3_t mins, const vec3_t maxs, short *list, int listsize, int *topnode )
{
	leaflist_t	ll;

	if( !worldmodel ) return 0;

	VectorCopy( mins, ll.mins );
	VectorCopy( maxs, ll.maxs );

	ll.maxcount = listsize;
	ll.overflowed = false;
	ll.topnode = -1;
	ll.list = list;
	ll.count = 0;

	Mod_BoxLeafnums_r( &ll, worldmodel->nodes );

	if( topnode ) *topnode = ll.topnode;
	return ll.count;
}

/*
=============
Mod_BoxVisible

Returns true if any leaf in boxspace
is potentially visible
=============
*/
qboolean Mod_BoxVisible( const vec3_t mins, const vec3_t maxs, const byte *visbits )
{
	short	leafList[MAX_BOX_LEAFS];
	int	i, count;

	if( !visbits || !mins || !maxs )
		return true;

	count = Mod_BoxLeafnums( mins, maxs, leafList, MAX_BOX_LEAFS, NULL );

	for( i = 0; i < count; i++ )
	{
		if( CHECKVISBIT( visbits, leafList[i] ))
			return true;
	}
	return false;
}

/*
=============
Mod_HeadnodeVisible
=============
*/
qboolean Mod_HeadnodeVisible( mnode_t *node, const byte *visbits, short *lastleaf )
{
	if( !node || node->contents == CONTENTS_SOLID )
		return false;

	if( node->contents < 0 )
	{
		if( !CHECKVISBIT( visbits, ((mleaf_t *)node)->cluster ))
			return false;

		if( lastleaf )
			*lastleaf = ((mleaf_t *)node)->cluster;
		return true;
	}

	if( Mod_HeadnodeVisible( node->children[0], visbits, lastleaf ))
		return true;

	if( Mod_HeadnodeVisible( node->children[1], visbits, lastleaf ))
		return true;

	return false;
}

/*
==================
Mod_AmbientLevels

grab the ambient sound levels for current point
==================
*/
void Mod_AmbientLevels( const vec3_t p, byte *pvolumes )
{
	mleaf_t	*leaf;

	if( !worldmodel || !p || !pvolumes )
		return;	

	leaf = Mod_PointInLeaf( p, worldmodel->nodes );
	*(int *)pvolumes = *(int *)leaf->ambient_sound_level;
}

/*
==================
Mod_SampleSizeForFace

return the current lightmap resolution per face
==================
*/
int Mod_SampleSizeForFace( msurface_t *surf )
{
	if( !surf || !surf->texinfo || !surf->texinfo->faceinfo )
		return LM_SAMPLE_SIZE;

	return surf->texinfo->faceinfo->texture_step;
}

/*
==================
Mod_CheckWaterAlphaSupport

converted maps potential may don't
support water transparency
==================
*/
static qboolean Mod_CheckWaterAlphaSupport( void )
{
	mleaf_t		*leaf;
	int		i, j;
	const byte	*pvs;

	if( world.visdatasize <= 0 ) return true;

	// check all liquid leafs to see if they can see into empty leafs, if any
	// can we can assume this map supports r_wateralpha
	for( i = 0, leaf = loadmodel->leafs; i < loadmodel->numleafs; i++, leaf++ )
	{
		if(( leaf->contents == CONTENTS_WATER || leaf->contents == CONTENTS_SLIME ) && leaf->cluster >= 0 )
		{
			pvs = world.visdata + leaf->cluster * world.visbytes;

			for( j = 0; j < loadmodel->numleafs; j++ )
			{
				if( CHECKVISBIT( pvs, loadmodel->leafs[j].cluster ) && loadmodel->leafs[j].contents == CONTENTS_EMPTY )
					return true;
			}
		}
	}

	return false;
}

/*
================
Mod_FreeUserData
================
*/
static void Mod_FreeUserData( model_t *mod )
{
	// already freed?
	if( !mod || !mod->name[0] )
		return;

	if( host.type == HOST_DEDICATED )
	{
		if( svgame.physFuncs.Mod_ProcessUserData != NULL )
		{
			// let the server.dll free custom data
			svgame.physFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
	else
	{
		if( clgame.drawFuncs.Mod_ProcessUserData != NULL )
		{
			// let the client.dll free custom data
			clgame.drawFuncs.Mod_ProcessUserData( mod, false, NULL );
		}
	}
}

/*
================
Mod_FreeModel
================
*/
static void Mod_FreeModel( model_t *mod )
{
	// already freed?
	if( !mod || !mod->name[0] )
		return;

	Mod_FreeUserData( mod );

	// select the properly unloader
	switch( mod->type )
	{
	case mod_sprite:
		Mod_UnloadSpriteModel( mod );
		break;
	case mod_studio:
		Mod_UnloadStudioModel( mod );
		break;
	case mod_brush:
		Mod_UnloadBrushModel( mod );
		break;
	case mod_alias:
		Mod_UnloadAliasModel( mod );
		break;
	}
}

/*
===============================================================================

			MODEL INITALIZE\SHUTDOWN

===============================================================================
*/

void Mod_Init( void )
{
	com_studiocache = Mem_AllocPool( "Studio Cache" );
	mod_studiocache = Cvar_Get( "r_studiocache", "1", FCVAR_ARCHIVE, "enables studio cache for speedup tracing hitboxes" );
	r_wadtextures = Cvar_Get( "r_wadtextures", "0", 0, "completely ignore textures in the wad-files if disabled" );

	Cmd_AddCommand( "mapstats", Mod_PrintBSPFileSizes_f, "show stats for currently loaded map" );
	Cmd_AddCommand( "modellist", Mod_Modellist_f, "display loaded models list" );

	Mod_ResetStudioAPI ();
	Mod_InitStudioHull ();
}

void Mod_ClearAll( qboolean keep_playermodel )
{
	model_t	*plr = com_models[MAX_MODELS-1];
	model_t	*mod;
	int	i;

	for( i = 0, mod = cm_models; i < cm_nummodels; i++, mod++ )
	{
		if( keep_playermodel && mod == plr )
			continue;

		Mod_FreeModel( mod );
		memset( mod, 0, sizeof( *mod ));
	}

	// g-cont. may be just leave unchanged?
	if( !keep_playermodel ) cm_nummodels = 0;
}

void Mod_ClearUserData( void )
{
	int	i;

	for( i = 0; i < cm_nummodels; i++ )
		Mod_FreeUserData( &cm_models[i] );
}

void Mod_Shutdown( void )
{
	Mod_ClearAll( false );
	Mem_FreePool( &com_studiocache );
}

/*
===============================================================================

			MAP LOADING

===============================================================================
*/
/*
=================
Mod_LoadSubmodels
=================
*/
static void Mod_LoadSubmodels( const dlump_t *l )
{
	dmodel_t	*in;
	dmodel_t	*out;
	int	i, j, count;
	
	in = (void *)(mod_base + l->fileofs);
	if( l->filelen % sizeof( *in )) Host_Error( "Mod_LoadBModel: funny lump size\n" );
	count = l->filelen / sizeof( *in );

	if( count < 1 ) Host_Error( "Map %s without models\n", loadmodel->name );
	if( count > MAX_MAP_MODELS ) Host_Error( "Map %s has too many models\n", loadmodel->name );

	// allocate extradata
	out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));
	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;
	if( world.loading ) world.max_surfaces = 0;

	for( i = 0; i < count; i++, in++, out++ )
	{
		for( j = 0; j < 3; j++ )
		{
			// spread the mins / maxs by a unit
			out->mins[j] = in->mins[j] - 1.0f;
			out->maxs[j] = in->maxs[j] + 1.0f;
			out->origin[j] = in->origin[j];
		}

		for( j = 0; j < MAX_MAP_HULLS; j++ )
			out->headnode[j] = in->headnode[j];

		out->visleafs = in->visleafs;
		out->firstface = in->firstface;
		out->numfaces = in->numfaces;

		if( i == 0 || !world.loading )
			continue; // skip the world

		if( VectorIsNull( out->origin ))
		{
			// NOTE: zero origin after recalculating is indicated included origin brush
			VectorAverage( out->mins, out->maxs, out->origin );
		}

		world.max_surfaces = Q_max( world.max_surfaces, out->numfaces ); 
	}

	if( world.loading )
		world.draw_surfaces = Mem_Alloc( loadmodel->mempool, world.max_surfaces * sizeof( msurface_t* ));
}

/*
=================
Mod_LoadTextures
=================
*/
static void Mod_LoadTextures( const dlump_t *l )
{
	dmiptexlump_t	*in;
	texture_t		*tx, *tx2;
	texture_t		*anims[10];
	texture_t		*altanims[10];
	int		num, max, altmax;
	char		texname[64];
	imgfilter_t	*filter;
	mip_t		*mt;
	int 		i, j; 

	if( world.loading )
	{
		// release old sky layers first
		GL_FreeTexture( tr.solidskyTexture );
		GL_FreeTexture( tr.alphaskyTexture );
		tr.solidskyTexture = tr.alphaskyTexture = 0;
		world.texdatasize = l->filelen;
		world.custom_skybox = false;
		world.has_mirrors = false;
		world.sky_sphere = false;
	}

	if( !l->filelen )
	{
		// no textures
		loadmodel->textures = NULL;
		return;
	}

	in = (void *)(mod_base + l->fileofs);

	loadmodel->numtextures = in->nummiptex;
	loadmodel->textures = (texture_t **)Mem_Alloc( loadmodel->mempool, loadmodel->numtextures * sizeof( texture_t* ));

	for( i = 0; i < loadmodel->numtextures; i++ )
	{
		if( in->dataofs[i] == -1 )
		{
			// create default texture (some mods requires this)
			tx = Mem_Alloc( loadmodel->mempool, sizeof( *tx ));
			loadmodel->textures[i] = tx;
		
			Q_strncpy( tx->name, "*default", sizeof( tx->name ));
			tx->gl_texturenum = tr.defaultTexture;
			tx->width = tx->height = 16;
			continue; // missed
		}

		mt = (mip_t *)((byte *)in + in->dataofs[i] );

		if( !mt->name[0] )
		{
			MsgDev( D_WARN, "unnamed texture in %s\n", loadmodel->name );
			Q_snprintf( mt->name, sizeof( mt->name ), "miptex_%i", i );
		}

		tx = Mem_Alloc( loadmodel->mempool, sizeof( *tx ));
		loadmodel->textures[i] = tx;

		// convert to lowercase
		Q_strnlwr( mt->name, mt->name, sizeof( mt->name ));
		Q_strncpy( tx->name, mt->name, sizeof( tx->name ));
		filter = R_FindTexFilter( tx->name ); // grab texture filter

		tx->width = mt->width;
		tx->height = mt->height;

		// check for multi-layered sky texture
		if( world.loading && !Q_strncmp( mt->name, "sky", 3 ) && mt->width == 256 && mt->height == 128 )
		{	
			R_InitSky( mt, tx ); // loadq quake sky

			if( tr.solidskyTexture && tr.alphaskyTexture )
				world.sky_sphere = true;
		}
		else 
		{
			// texture loading order:
			// 1. from wad
			// 2. internal from map

			// trying wad texture (force while r_wadtextures is 1)
			if( r_wadtextures->value || mt->offsets[0] <= 0 )
			{
				Q_snprintf( texname, sizeof( texname ), "%s.mip", mt->name );

				// check wads in reverse order
				for( j = wadlist.count - 1; j >= 0; j-- )
				{
					char	*texpath = va( "%s.wad/%s", wadlist.wadnames[j], texname );

					if( FS_FileExists( texpath, false ))
					{
						tx->gl_texturenum = GL_LoadTexture( texpath, NULL, 0, 0, filter );
						break;
					}
				}
			}

			// wad failed, so use internal texture (if present)
			if( mt->offsets[0] > 0 && !tx->gl_texturenum )
			{
				// NOTE: imagelib detect miptex version by size
				// 770 additional bytes is indicated custom palette
				int size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);
				if( bmodel_version == HLBSP_VERSION || bmodel_version == XTBSP_VERSION )
					size += sizeof( short ) + 768;

				Q_snprintf( texname, sizeof( texname ), "#%s.mip", mt->name );
				tx->gl_texturenum = GL_LoadTexture( texname, (byte *)mt, size, 0, filter );
			}
		}

		// set the emo-texture for missed
		if( !tx->gl_texturenum ) tx->gl_texturenum = tr.defaultTexture;

		// check for luma texture
		if( FBitSet( R_GetTexture( tx->gl_texturenum )->flags, TF_HAS_LUMA ))
		{
			Q_snprintf( texname, sizeof( texname ), "#%s_luma.mip", mt->name );

			if( mt->offsets[0] > 0 )
			{
				// NOTE: imagelib detect miptex version by size
				// 770 additional bytes is indicated custom palette
				int size = (int)sizeof( mip_t ) + ((mt->width * mt->height * 85)>>6);
				if( bmodel_version == HLBSP_VERSION || bmodel_version == XTBSP_VERSION )
					size += sizeof( short ) + 768;

				tx->fb_texturenum = GL_LoadTexture( texname, (byte *)mt, size, TF_MAKELUMA, NULL );
			}
			else
			{
				size_t srcSize = 0;
				byte *src = NULL;

				// NOTE: we can't loading it from wad as normal because _luma texture doesn't exist
				// and not be loaded. But original texture is already loaded and can't be modified
				// So load original texture manually and convert it to luma

				// check wads in reverse order
				for( j = wadlist.count - 1; j >= 0; j-- )
				{
					char	*texpath = va( "%s.wad/%s.mip", wadlist.wadnames[j], tx->name );

					if( FS_FileExists( texpath, false ))
					{
						src = FS_LoadFile( texpath, &srcSize, false );
						break;
					}
				}

				// okay, loading it from wad or hi-res version
				tx->fb_texturenum = GL_LoadTexture( texname, src, srcSize, TF_MAKELUMA, NULL );
				if( src ) Mem_Free( src );
			}
		}
	}

	// sequence the animations and detail textures
	for( i = 0; i < loadmodel->numtextures; i++ )
	{
		tx = loadmodel->textures[i];

		if( !tx || ( tx->name[0] != '-' && tx->name[0] != '+' ))
			continue;

		if( tx->anim_next )
			continue;	// already sequenced

		// find the number of frames in the animation
		memset( anims, 0, sizeof( anims ));
		memset( altanims, 0, sizeof( altanims ));

		max = tx->name[1];
		altmax = 0;

		if( max >= '0' && max <= '9' )
		{
			max -= '0';
			altmax = 0;
			anims[max] = tx;
			max++;
		}
		else if( max >= 'a' && max <= 'j' )
		{
			altmax = max - 'a';
			max = 0;
			altanims[altmax] = tx;
			altmax++;
		}
		else MsgDev( D_ERROR, "Mod_LoadTextures: bad animating texture %s\n", tx->name );

		for( j = i + 1; j < loadmodel->numtextures; j++ )
		{
			tx2 = loadmodel->textures[j];

			if( !tx2 || ( tx2->name[0] != '-' && tx2->name[0] != '+' ))
				continue;

			if( Q_strcmp( tx2->name + 2, tx->name + 2 ))
				continue;

			num = tx2->name[1];

			if( num >= '0' && num <= '9' )
			{
				num -= '0';
				anims[num] = tx2;
				if( num + 1 > max )
					max = num + 1;
			}
			else if( num >= 'a' && num <= 'j' )
			{
				num = num - 'a';
				altanims[num] = tx2;
				if( num + 1 > altmax )
					altmax = num + 1;
			}
			else MsgDev( D_ERROR, "Mod_LoadTextures: bad animating texture %s\n", tx->name );
		}

		// link them all together
		for( j = 0; j < max; j++ )
		{
			tx2 = anims[j];

			if( !tx2 )
			{
				MsgDev( D_ERROR, "Mod_LoadTextures: missing frame %i of %s\n", j, tx->name );
				tx->anim_total = 0;
				break;
			}

			tx2->anim_total = max * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j + 1) * ANIM_CYCLE;
			tx2->anim_next = anims[(j + 1) % max];
			if( altmax ) tx2->alternate_anims = altanims[0];
		}

		for( j = 0; j < altmax; j++ )
		{
			tx2 = altanims[j];

			if( !tx2 )
			{
				MsgDev( D_ERROR, "Mod_LoadTextures: missing frame %i of %s\n", j, tx->name );
				tx->anim_total = 0;
				break;
			}

			tx2->anim_total = altmax * ANIM_CYCLE;
			tx2->anim_min = j * ANIM_CYCLE;
			tx2->anim_max = (j+1) * ANIM_CYCLE;
			tx2->anim_next = altanims[(j + 1) % altmax];
			if( max ) tx2->alternate_anims = anims[0];
		}
	}
}

/*
=================
Mod_LoadTexInfo
=================
*/
static mfaceinfo_t *Mod_LoadFaceInfo( const dlump_t *l, int *numfaceinfo )
{
	dfaceinfo_t	*in;
	mfaceinfo_t	*out, *faceinfo;
	int		i, count;

	in = (void *)(mod_base + l->fileofs);
	if( l->filelen % sizeof( *in ))
		Host_Error( "Mod_LoadFaceInfo: funny lump size in %s\n", loadmodel->name );

	count = l->filelen / sizeof( *in );
          faceinfo = out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));

	for( i = 0; i < count; i++, in++, out++ )
	{
		Q_strncpy( out->landname, in->landname, sizeof( out->landname ));
		out->texture_step = in->texture_step;
		out->max_extent = in->max_extent;
		out->groupid = in->groupid;
	}

	*numfaceinfo = count;

	return faceinfo;
}

/*
=================
Mod_LoadTexInfo
=================
*/
static void Mod_LoadTexInfo( const dlump_t *l, dextrahdr_t *extrahdr )
{
	dtexinfo_t	*in;
	mtexinfo_t	*out;
	int		miptex;
	mfaceinfo_t	*fi = NULL;
	int		fi_count;
	int		i, j, count;

	if( extrahdr != NULL )
		fi = Mod_LoadFaceInfo( &extrahdr->lumps[LUMP_FACEINFO], &fi_count );

	in = (void *)(mod_base + l->fileofs);
	if( l->filelen % sizeof( *in ))
		Host_Error( "Mod_LoadTexInfo: funny lump size in %s\n", loadmodel->name );

	count = l->filelen / sizeof( *in );
          out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));
	
	loadmodel->texinfo = out;
	loadmodel->numtexinfo = count;

	for( i = 0; i < count; i++, in++, out++ )
	{
		for( j = 0; j < 8; j++ )
			out->vecs[0][j] = in->vecs[0][j];

		miptex = in->miptex;
		if( miptex < 0 || miptex > loadmodel->numtextures )
			Host_Error( "Mod_LoadTexInfo: bad miptex number %i in '%s'\n", miptex, loadmodel->name );

		out->texture = loadmodel->textures[miptex];
		out->flags = in->flags;

		// make sure what faceinfo is really exist
		if( extrahdr != NULL && in->faceinfo != -1 && in->faceinfo < fi_count )
			out->faceinfo = &fi[in->faceinfo];
	}
}

/*
=================
Mod_LoadDeluxemap
=================
*/
static void Mod_LoadDeluxemap( void )
{
	char	path[64];
	int	iCompare;
	byte	*in;

	if( !world.loading ) return;	// only world can have deluxedata

	if( !( host.features & ENGINE_LOAD_DELUXEDATA ))
	{
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		return;
	}

	Q_snprintf( path, sizeof( path ), "maps/%s.dlit", modelname );

	// make sure what deluxemap is actual
	if( !COM_CompareFileTime( path, loadmodel->name, &iCompare ))
	{
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		return;
          }

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		MsgDev( D_WARN, "Mod_LoadDeluxemap: %s probably is out of date\n", path );

	in = FS_LoadFile( path, &world.vecdatasize, false );

	ASSERT( in != NULL );

	if( *(uint *)in != IDDELUXEMAPHEADER || *((uint *)in + 1) != DELUXEMAP_VERSION )
	{
		MsgDev( D_ERROR, "Mod_LoadDeluxemap: %s is not a deluxemap file\n", path );
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		Mem_Free( in );
		return;
	}

	// skip header bytes
	world.vecdatasize -= 8;

	if( world.vecdatasize != world.litdatasize )
	{
		MsgDev( D_ERROR, "Mod_LoadDeluxemap: %s has mismatched size (%i should be %i)\n", path, world.vecdatasize, world.litdatasize );
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		Mem_Free( in );
		return;
	}

	MsgDev( D_INFO, "Mod_LoadDeluxemap: %s loaded\n", path );
	world.deluxedata = Mem_Alloc( loadmodel->mempool, world.vecdatasize );
	memcpy( world.deluxedata, in + 8, world.vecdatasize );
	Mem_Free( in );
}

/*
=================
Mod_LoadLightVecs
=================
*/
static void Mod_LoadLightVecs( const dlump_t *l )
{
	byte	*in;

	in = (void *)(mod_base + l->fileofs);
	world.vecdatasize = l->filelen;

	if( world.vecdatasize != world.litdatasize )
	{
		MsgDev( D_ERROR, "Mod_LoadLightVecs: has mismatched size (%i should be %i)\n", world.vecdatasize, world.litdatasize );
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		return;
	}

	world.deluxedata = Mem_Alloc( loadmodel->mempool, world.vecdatasize );
	memcpy( world.deluxedata, in, world.vecdatasize );
	MsgDev( D_INFO, "Mod_LoadLightVecs: loaded\n" );
}

/*
=================
Mod_LoadColoredLighting
=================
*/
static qboolean Mod_LoadColoredLighting( void )
{
	char	path[64];
	int	iCompare;
	size_t	litdatasize;
	byte	*in;

	Q_snprintf( path, sizeof( path ), "maps/%s.lit", modelname );

	// make sure what deluxemap is actual
	if( !COM_CompareFileTime( path, loadmodel->name, &iCompare ))
		return false;

	if( iCompare < 0 ) // this may happens if level-designer used -onlyents key for hlcsg
		MsgDev( D_WARN, "Mod_LoadColoredLighting: %s probably is out of date\n", path );

	in = FS_LoadFile( path, &litdatasize, false );

	ASSERT( in != NULL );

	if( *(uint *)in != IDDELUXEMAPHEADER || *((uint *)in + 1) != DELUXEMAP_VERSION )
	{
		MsgDev( D_ERROR, "Mod_LoadColoredLighting: %s is not a lightmap file\n", path );
		Mem_Free( in );
		return false;
	}

	// skip header bytes
	litdatasize -= 8;

	MsgDev( D_INFO, "Mod_LoadColoredLighting: %s loaded\n", path );
	loadmodel->lightdata = Mem_Alloc( loadmodel->mempool, litdatasize );
	memcpy( loadmodel->lightdata, in + 8, litdatasize );
	if( world.loading ) world.litdatasize = litdatasize;
	Mem_Free( in );

	return true;
}

/*
=================
Mod_LoadLighting
=================
*/
static void Mod_LoadLighting( const dlump_t *l, dextrahdr_t *extrahdr )
{
	byte	d, *in;
	color24	*out;
	int	i;

	if( !l->filelen )
	{
		if( world.loading )
		{
			MsgDev( D_WARN, "map ^2%s^7 has no lighting\n", loadmodel->name );
			loadmodel->lightdata = NULL;
			world.deluxedata = NULL;
			world.vecdatasize = 0;
			world.litdatasize = 0;
		}
		return;
	}
	in = (void *)(mod_base + l->fileofs);
	if( world.loading ) world.litdatasize = l->filelen;

	switch( bmodel_version )
	{
	case Q1BSP_VERSION:
	case QBSP2_VERSION:
		if( !Mod_LoadColoredLighting( ))
		{
			// expand the white lighting data
			loadmodel->lightdata = (color24 *)Mem_Alloc( loadmodel->mempool, l->filelen * sizeof( color24 ));
			out = loadmodel->lightdata;

			for( i = 0; i < l->filelen; i++, out++ )
			{
				d = *in++;
				out->r = d;
				out->g = d;
				out->b = d;
			}
		}
		break;
	case HLBSP_VERSION:
	case XTBSP_VERSION:
		// load colored lighting
		loadmodel->lightdata = Mem_Alloc( loadmodel->mempool, l->filelen );
		memcpy( loadmodel->lightdata, in, l->filelen );
		SetBits( loadmodel->flags, MODEL_COLORED_LIGHTING );
		break;
	}

	if( !world.loading ) return;	// only world can have deluxedata (FIXME: what about quake models?)

	// not supposed to be load ?
	if( !FBitSet( host.features, ENGINE_LOAD_DELUXEDATA ))
	{
		world.deluxedata = NULL;
		world.vecdatasize = 0;
		return;
	}

	// try to loading deluxemap too
	if( extrahdr != NULL )
		Mod_LoadLightVecs( &extrahdr->lumps[LUMP_LIGHTVECS] );
	else Mod_LoadDeluxemap (); // old method
}

/*
=================
Mod_CalcSurfaceExtents

Fills in surf->texturemins[] and surf->extents[]
=================
*/
static void Mod_CalcSurfaceExtents( msurface_t *surf )
{
	float		mins[2], maxs[2], val;
	int		bmins[2], bmaxs[2];
	int		i, j, e, sample_size;
	mtexinfo_t	*tex;
	mvertex_t		*v;

	sample_size = Mod_SampleSizeForFace( surf );
	tex = surf->texinfo;

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -999999;

	for( i = 0; i < surf->numedges; i++ )
	{
		e = loadmodel->surfedges[surf->firstedge + i];

		if( e >= loadmodel->numedges || e <= -loadmodel->numedges )
			Host_Error( "Mod_CalcSurfaceExtents: bad edge\n" );

		if( e >= 0 ) v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];

		for( j = 0; j < 2; j++ )
		{
			val = DotProduct( v->position, surf->texinfo->vecs[j] ) + surf->texinfo->vecs[j][3];
			if( val < mins[j] ) mins[j] = val;
			if( val > maxs[j] ) maxs[j] = val;
		}
	}

	for( i = 0; i < 2; i++ )
	{
		bmins[i] = floor( mins[i] / sample_size );
		bmaxs[i] = ceil( maxs[i] / sample_size );

		surf->texturemins[i] = bmins[i] * sample_size;
		surf->extents[i] = (bmaxs[i] - bmins[i]) * sample_size;

		if( !FBitSet( tex->flags, TEX_SPECIAL ) && surf->extents[i] > 4096 )
			MsgDev( D_ERROR, "Bad surface extents %i\n", surf->extents[i] );
	}
}

/*
=================
Mod_CalcSurfaceBounds

fills in surf->mins and surf->maxs
=================
*/
static void Mod_CalcSurfaceBounds( msurface_t *surf )
{
	int	i, e;
	mvertex_t	*v;

	ClearBounds( surf->info->mins, surf->info->maxs );

	for( i = 0; i < surf->numedges; i++ )
	{
		e = loadmodel->surfedges[surf->firstedge + i];

		if( e >= loadmodel->numedges || e <= -loadmodel->numedges )
			Host_Error( "Mod_CalcSurfaceBounds: bad edge\n" );

		if( e >= 0 ) v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		else v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		AddPointToBounds( v->position, surf->info->mins, surf->info->maxs );
	}

	VectorAverage( surf->info->mins, surf->info->maxs, surf->info->origin );
}

/*
=================
Mod_LoadSurfaces
=================
*/
static void Mod_LoadSurfaces( const dlump_t *l )
{
	dface_t		*in16;
	dface2_t		*in32;
	msurface_t	*out;
	mextrasurf_t	*info;
	int		i, j, count;
	int		lightofs;

	if( bmodel_version == QBSP2_VERSION )
	{
		in32 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in32 ))
			Host_Error( "Mod_LoadSurfaces: funny lump size in '%s'\n", loadmodel->name );
		count = l->filelen / sizeof( *in32 );
		in16 = NULL;
	}
	else
	{
		in16 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in16 ))
			Host_Error( "Mod_LoadSurfaces: funny lump size in '%s'\n", loadmodel->name );
		count = l->filelen / sizeof( *in16 );
		in32 = NULL;
	}

	loadmodel->numsurfaces = count;
	loadmodel->surfaces = out = Mem_Alloc( loadmodel->mempool, count * sizeof( msurface_t ));
	info = Mem_Alloc( loadmodel->mempool, count * sizeof( mextrasurf_t ));

	for( i = 0; i < count; i++, out++, info++ )
	{
		texture_t	*tex;

		// setup crosslinks between two parts of msurface_t
		out->info = info;
		info->surf = out;

		if( bmodel_version == QBSP2_VERSION )
		{
			if(( in32->firstedge + in32->numedges ) > loadmodel->numsurfedges )
			{
				MsgDev( D_ERROR, "Bad surface %i from %i\n", i, count );
				continue;
			}

			out->firstedge = in32->firstedge;
			out->numedges = in32->numedges;
			out->flags = 0;

			if( in32->side ) out->flags |= SURF_PLANEBACK;
			out->plane = loadmodel->planes + in32->planenum;
			out->texinfo = loadmodel->texinfo + in32->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in32->styles[j];

			lightofs = in32->lightofs;
			in32++;
		}
		else
		{
			if(( in16->firstedge + in16->numedges ) > loadmodel->numsurfedges )
			{
				MsgDev( D_ERROR, "Bad surface %i from %i\n", i, count );
				continue;
			}

			out->firstedge = in16->firstedge;
			out->numedges = in16->numedges;
			out->flags = 0;

			if( in16->side ) out->flags |= SURF_PLANEBACK;
			out->plane = loadmodel->planes + in16->planenum;
			out->texinfo = loadmodel->texinfo + in16->texinfo;

			for( j = 0; j < MAXLIGHTMAPS; j++ )
				out->styles[j] = in16->styles[j];

			lightofs = in16->lightofs;
			in16++;
		}

		tex = out->texinfo->texture;

		if( !Q_strncmp( tex->name, "sky", 3 ))
			out->flags |= (SURF_DRAWTILED|SURF_DRAWSKY);

		if(( tex->name[0] == '*' && Q_stricmp( tex->name, "*default" )) || tex->name[0] == '!' )
			out->flags |= (SURF_DRAWTURB|SURF_DRAWTILED|SURF_NOCULL);

		if( !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
		{
			if( !Q_strncmp( tex->name, "water", 5 ) || !Q_strnicmp( tex->name, "laser", 5 ))
				out->flags |= (SURF_DRAWTURB|SURF_DRAWTILED|SURF_NOCULL);
		}

		if( !Q_strncmp( tex->name, "scroll", 6 ))
			out->flags |= SURF_CONVEYOR;

		// g-cont. added a combined conveyor-transparent
		if( !Q_strncmp( tex->name, "{scroll", 7 ))
			out->flags |= SURF_CONVEYOR|SURF_TRANSPARENT;

		// g-cont this texture from decals.wad he-he
		// support !reflect for reflected water
		if( !Q_strcmp( tex->name, "reflect1" ) || !Q_strncmp( tex->name, "!reflect", 8 ))
		{
			out->flags |= SURF_REFLECT;
			world.has_mirrors = true;
		}

		if( tex->name[0] == '{' )
			out->flags |= SURF_TRANSPARENT;

		if( out->texinfo->flags & TEX_SPECIAL )
			out->flags |= SURF_DRAWTILED;

		Mod_CalcSurfaceBounds( out );
		Mod_CalcSurfaceExtents( out );

		if( loadmodel->lightdata && lightofs != -1 )
		{
			if( bmodel_version == HLBSP_VERSION || bmodel_version == XTBSP_VERSION )
				out->samples = loadmodel->lightdata + (lightofs / 3);
			else out->samples = loadmodel->lightdata + lightofs;

			// if deluxemap is present setup it too
			if( world.deluxedata )
				out->info->deluxemap = world.deluxedata + (lightofs / 3);
		}

		if( out->flags & SURF_DRAWTURB )
			GL_SubdivideSurface( out ); // cut up polygon for warps
	}
}

/*
=================
Mod_LoadVertexes
=================
*/
static void Mod_LoadVertexes( const dlump_t *l )
{
	dvertex_t	*in;
	mvertex_t	*out;
	int	i, count;

	in = (void *)( mod_base + l->fileofs );
	if( l->filelen % sizeof( *in ))
		Host_Error( "Mod_LoadVertexes: funny lump size in %s\n", loadmodel->name );
	count = l->filelen / sizeof( *in );

	loadmodel->numvertexes = count;
	out = loadmodel->vertexes = Mem_Alloc( loadmodel->mempool, count * sizeof( mvertex_t ));

	if( world.loading ) ClearBounds( world.mins, world.maxs );

	for( i = 0; i < count; i++, in++, out++ )
	{
		VectorCopy( in->point, out->position );
		if( world.loading ) AddPointToBounds( in->point, world.mins, world.maxs );
	}

	if( !world.loading ) return;

	VectorSubtract( world.maxs, world.mins, world.size );

	for( i = 0; i < 3; i++ )
	{
		// spread the mins / maxs by a pixel
		world.mins[i] -= 1.0f;
		world.maxs[i] += 1.0f;
	}
}

/*
=================
Mod_LoadEdges
=================
*/
static void Mod_LoadEdges( const dlump_t *l )
{
	medge_t	*out;
	int	i, count;

	if( bmodel_version == QBSP2_VERSION )
	{
		dedge2_t	*in = (void *)( mod_base + l->fileofs );	

		if( l->filelen % sizeof( *in ))
			Host_Error( "Mod_LoadEdges: funny lump size in %s\n", loadmodel->name );

		count = l->filelen / sizeof( *in );
		loadmodel->edges = out = Mem_Alloc( loadmodel->mempool, count * sizeof( medge_t ));
		loadmodel->numedges = count;

		for( i = 0; i < count; i++, in++, out++ )
		{
			out->v[0] = in->v[0];
			out->v[1] = in->v[1];
		}
	}
	else
	{
		dedge_t	*in = (void *)( mod_base + l->fileofs );	

		if( l->filelen % sizeof( *in ))
			Host_Error( "Mod_LoadEdges: funny lump size in %s\n", loadmodel->name );

		count = l->filelen / sizeof( *in );
		loadmodel->edges = out = Mem_Alloc( loadmodel->mempool, count * sizeof( medge_t ));
		loadmodel->numedges = count;

		for( i = 0; i < count; i++, in++, out++ )
		{
			out->v[0] = (word)in->v[0];
			out->v[1] = (word)in->v[1];
		}
	}
}

/*
=================
Mod_LoadSurfEdges
=================
*/
static void Mod_LoadSurfEdges( const dlump_t *l )
{
	dsurfedge_t	*in;
	int		count;

	in = (void *)( mod_base + l->fileofs );	
	if( l->filelen % sizeof( *in ))
		Host_Error( "Mod_LoadSurfEdges: funny lump size in %s\n", loadmodel->name );

	count = l->filelen / sizeof( dsurfedge_t );
	loadmodel->surfedges = Mem_Alloc( loadmodel->mempool, count * sizeof( dsurfedge_t ));
	loadmodel->numsurfedges = count;

	memcpy( loadmodel->surfedges, in, count * sizeof( dsurfedge_t ));
}

/*
=================
Mod_LoadMarkSurfaces
=================
*/
static void Mod_LoadMarkSurfaces( const dlump_t *l )
{
	dmarkface_t	*in16;
	dmarkface2_t	*in32;
	int		i, j, count;
	msurface_t	**out;

	if( bmodel_version == QBSP2_VERSION )
	{
		in32 = (void *)(mod_base + l->fileofs);	
		if( l->filelen % sizeof( *in32 ))
			Host_Error( "Mod_LoadMarkFaces: funny lump size in %s\n", loadmodel->name );
		count = l->filelen / sizeof( *in32 );
		in16 = NULL;
	}
	else
	{
		in16 = (void *)(mod_base + l->fileofs);	
		if( l->filelen % sizeof( *in16 ))
			Host_Error( "Mod_LoadMarkFaces: funny lump size in %s\n", loadmodel->name );
		count = l->filelen / sizeof( *in16 );
		in32 = NULL;
	}

	loadmodel->marksurfaces = out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));
	loadmodel->nummarksurfaces = count;

	for( i = 0; i < count; i++ )
	{
		if( bmodel_version == QBSP2_VERSION )
			j = in32[i];
		else j = in16[i];

		if( j < 0 || j >= loadmodel->numsurfaces )
			Host_Error( "Mod_LoadMarkFaces: bad surface number in '%s'\n", loadmodel->name );
		out[i] = loadmodel->surfaces + j;
	}
}

/*
=================
Mod_SetParent
=================
*/
static void Mod_SetParent( mnode_t *node, mnode_t *parent )
{
	node->parent = parent;

	if( node->contents < 0 ) return; // it's node
	Mod_SetParent( node->children[0], node );
	Mod_SetParent( node->children[1], node );
}

/*
=================
Mod_LoadNodes
=================
*/
static void Mod_LoadNodes( const dlump_t *l )
{
	dnode_t	*in16;
	dnode2_t	*in32;
	mnode_t	*out;
	int	i, j, p;

	if( bmodel_version == QBSP2_VERSION )
	{	
		in32 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in32 )) Host_Error( "Mod_LoadNodes: funny lump size\n" );
		loadmodel->numnodes = l->filelen / sizeof( *in32 );
		in16 = NULL;
	}
	else
	{
		in16 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in16 )) Host_Error( "Mod_LoadNodes: funny lump size\n" );
		loadmodel->numnodes = l->filelen / sizeof( *in16 );
		in32 = NULL;
	}

	if( loadmodel->numnodes < 1 ) Host_Error( "Map %s has no nodes\n", loadmodel->name );
	out = loadmodel->nodes = (mnode_t *)Mem_Alloc( loadmodel->mempool, loadmodel->numnodes * sizeof( *out ));

	for( i = 0; i < loadmodel->numnodes; i++, out++ )
	{
		if( bmodel_version == QBSP2_VERSION )
		{
			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in32->mins[j];
				out->minmaxs[j+3] = in32->maxs[j];
			}

			p = in32->planenum;
			out->plane = loadmodel->planes + p;
			out->firstsurface = in32->firstface;
			out->numsurfaces = in32->numfaces;

			for( j = 0; j < 2; j++ )
			{
				p = in32->children[j];
				if( p >= 0 ) out->children[j] = loadmodel->nodes + p;
				else out->children[j] = (mnode_t *)(loadmodel->leafs + ( -1 - p ));
			}
			in32++;
		}
		else
		{
			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in16->mins[j];
				out->minmaxs[j+3] = in16->maxs[j];
			}

			p = in16->planenum;
			out->plane = loadmodel->planes + p;
			out->firstsurface = in16->firstface;
			out->numsurfaces = in16->numfaces;

			for( j = 0; j < 2; j++ )
			{
				p = in16->children[j];
				if( p >= 0 ) out->children[j] = loadmodel->nodes + p;
				else out->children[j] = (mnode_t *)(loadmodel->leafs + ( -1 - p ));
			}
			in16++;
		}
	}

	// sets nodes and leafs
	Mod_SetParent( loadmodel->nodes, NULL );
}

/*
=================
Mod_LoadLeafs
=================
*/
static void Mod_LoadLeafs( const dlump_t *l )
{
	dleaf_t 	*in16;
	dleaf2_t	*in32;
	mleaf_t	*out;
	int	i, j, p, count;

	if( bmodel_version == QBSP2_VERSION )
	{		
		in32 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in32 )) Host_Error( "Mod_LoadLeafs: funny lump size\n" );
		count = l->filelen / sizeof( *in32 );
		in16 = NULL;
	}
	else
	{
		in16 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in16 )) Host_Error( "Mod_LoadLeafs: funny lump size\n" );
		count = l->filelen / sizeof( *in16 );
		in32 = NULL;
	}

	if( count < 1 ) Host_Error( "Map %s has no leafs\n", loadmodel->name );
	out = (mleaf_t *)Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	if( world.loading )
	{
		// get visleafs from the submodel data
		world.visclusters = loadmodel->submodels[0].visleafs;
		world.visbytes = (world.visclusters + 7) >> 3;
		world.visdata = (byte *)Mem_Alloc( loadmodel->mempool, world.visclusters * world.visbytes );

		// enable full visibility as default
		memset( world.visdata, 0xFF, world.visclusters * world.visbytes );
	}

	for( i = 0; i < count; i++, out++ )
	{
		if( bmodel_version == QBSP2_VERSION )
		{
			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in32->mins[j];
				out->minmaxs[j+3] = in32->maxs[j];
			}

			out->contents = in32->contents;
			p = in32->visofs;

			for( j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in32->ambient_level[j];

			out->firstmarksurface = loadmodel->marksurfaces + in32->firstmarksurface;
			out->nummarksurfaces = in32->nummarksurfaces;
			in32++;
		}
		else
		{
			for( j = 0; j < 3; j++ )
			{
				out->minmaxs[j+0] = in16->mins[j];
				out->minmaxs[j+3] = in16->maxs[j];
			}

			out->contents = in16->contents;
			p = in16->visofs;

			for( j = 0; j < 4; j++ )
				out->ambient_sound_level[j] = in16->ambient_level[j];

			out->firstmarksurface = loadmodel->marksurfaces + in16->firstmarksurface;
			out->nummarksurfaces = in16->nummarksurfaces;
			in16++;
		}

		if( world.loading )
		{
			out->cluster = ( i - 1 ); // solid leaf 0 has no visdata
			if( out->cluster >= world.visclusters )
				out->cluster = -1;

			// ignore visofs errors on leaf 0 (solid)
			if( p >= 0 && out->cluster >= 0 && loadmodel->visdata )
			{
				if( p < world.visdatasize )
				{
					byte	*inrow =  loadmodel->visdata + p;
					byte	*inrowend = loadmodel->visdata + world.visdatasize;
					byte	*outrow = world.visdata + out->cluster * world.visbytes;
					byte	*outrowend = world.visdata + (out->cluster + 1) * world.visbytes;

					Mod_DecompressVis( inrow, inrowend, outrow, outrowend );
				}
				else MsgDev( D_WARN, "Mod_LoadLeafs: invalid visofs for leaf #%i\n", i );
			}
	          }
		else out->cluster = -1; // no visclusters on bmodels

		if( p == -1 ) out->compressed_vis = NULL;
		else out->compressed_vis = loadmodel->visdata + p;

		// gl underwater warp
		if( out->contents != CONTENTS_EMPTY )
		{
			for( j = 0; j < out->nummarksurfaces; j++ )
			{
				// underwater surfaces can't have reflection (perfomance)
				out->firstmarksurface[j]->flags |= SURF_UNDERWATER;
				out->firstmarksurface[j]->flags &= ~SURF_REFLECT;
			}
		}
	}

	if( loadmodel->leafs[0].contents != CONTENTS_SOLID )
		Host_Error( "Mod_LoadLeafs: Map %s has leaf 0 is not CONTENTS_SOLID\n", loadmodel->name );

	// do some final things for world
	if( world.loading )
	{
		// store size of fat pvs
		world.fatbytes = (world.visclusters + 31) >> 3;
		world.water_alpha = Mod_CheckWaterAlphaSupport();
	}
}

/*
=================
Mod_LoadPlanes
=================
*/
static void Mod_LoadPlanes( const dlump_t *l )
{
	dplane_t	*in;
	mplane_t	*out;
	int	i, j, count;
	
	in = (void *)(mod_base + l->fileofs);
	if( l->filelen % sizeof( *in )) Host_Error( "Mod_LoadPlanes: funny lump size\n" );
	count = l->filelen / sizeof( *in );

	if( count < 1 ) Host_Error( "Map %s has no planes\n", loadmodel->name );
	out = (mplane_t *)Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));

	loadmodel->planes = out;
	loadmodel->numplanes = count;

	for( i = 0; i < count; i++, in++, out++ )
	{
		for( j = 0; j < 3; j++ )
		{
			out->normal[j] = in->normal[j];
			if( out->normal[j] < 0.0f )
				out->signbits |= 1<<j;
		}

		if( VectorIsNull( out->normal ))
			MsgDev( D_ERROR, "Mod_LoadPlanes: zero normal for plane #%i\n", i );

		out->dist = in->dist;
		out->type = in->type;
	}
}

/*
=================
Mod_LoadVisibility
=================
*/
static void Mod_LoadVisibility( const dlump_t *l )
{
	// bmodels has no visibility
	if( !world.loading ) return;

	if( !l->filelen )
	{
		MsgDev( D_WARN, "map ^2%s^7 has no visibility\n", loadmodel->name );
		loadmodel->visdata = NULL;
		world.visdatasize = 0;
		return;
	}

	loadmodel->visdata = Mem_Alloc( loadmodel->mempool, l->filelen );
	memcpy( loadmodel->visdata, (void *)(mod_base + l->fileofs), l->filelen );
	world.visdatasize = l->filelen; // save it for PHS allocation
}

/*
=================
Mod_LoadEntities
=================
*/
static void Mod_LoadEntities( const dlump_t *l )
{
	char	*pfile;
	string	keyname;
	char	token[2048];
	char	wadstring[2048];

	// make sure what we really has terminator
	loadmodel->entities = Mem_Alloc( loadmodel->mempool, l->filelen + 1 );
	memcpy( loadmodel->entities, mod_base + l->fileofs, l->filelen );
	if( !world.loading ) return;

	world.entdatasize = l->filelen;
	pfile = (char *)loadmodel->entities;
	world.compiler[0] = '\0';
	world.message[0] = '\0';
	wadlist.count = 0;

	// parse all the wads for loading textures in right ordering
	while(( pfile = COM_ParseFile( pfile, token )) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "Mod_LoadEntities: found %s when expecting {\n", token );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token )) == NULL )
				Host_Error( "Mod_LoadEntities: EOF without closing brace\n" );
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value	
			if(( pfile = COM_ParseFile( pfile, token )) == NULL ) 
				Host_Error( "Mod_LoadEntities: EOF without closing brace\n" );

			if( token[0] == '}' )
				Host_Error( "Mod_LoadEntities: closing brace without data\n" );

			if( !Q_stricmp( keyname, "wad" ))
			{
				char	*pszWadFile;

				Q_strncpy( wadstring, token, 2046 );
				wadstring[2046] = 0;

				if( !Q_strchr( wadstring, ';' ))
					Q_strcat( wadstring, ";" );

				// parse wad pathes
				for (pszWadFile = strtok( wadstring, ";" ); pszWadFile != NULL; pszWadFile = strtok( NULL, ";" ))
				{
					COM_FixSlashes( pszWadFile );
					FS_FileBase( pszWadFile, wadlist.wadnames[wadlist.count++] );
					if( wadlist.count >= 256 ) break; // too many wads...
				}
			}
			else if( !Q_stricmp( keyname, "mapversion" ))
				world.mapversion = Q_atoi( token );
			else if( !Q_stricmp( keyname, "message" ))
				Q_strncpy( world.message, token, sizeof( world.message ));
			else if( !Q_stricmp( keyname, "compiler" ) || !Q_stricmp( keyname, "_compiler" ))
				Q_strncpy( world.compiler, token, sizeof( world.compiler ));
		}
		return;	// all done
	}
}

/*
=================
Mod_LoadClipnodes
=================
*/
static void Mod_LoadClipnodes( const dlump_t *l )
{
	dclipnode_t	*in16;
	dclipnode2_t	*in32;
	mclipnode_t	*out;
	int		i, count;
	hull_t		*hull;

	if( bmodel_version == QBSP2_VERSION )
	{
		in32 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in32 )) Host_Error( "Mod_LoadClipnodes: funny lump size\n" );
		count = l->filelen / sizeof( *in32 );
		in16 = NULL;
	}
	else
	{
		in16 = (void *)(mod_base + l->fileofs);
		if( l->filelen % sizeof( *in16 )) Host_Error( "Mod_LoadClipnodes: funny lump size\n" );
		count = l->filelen / sizeof( *in16 );
		in32 = NULL;
	}

	out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));	
	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count;

	// hulls[0] is a point hull, always zeroed

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[0], hull->clip_mins ); // copy human hull
	VectorCopy( host.player_maxs[0], hull->clip_maxs );

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[3], hull->clip_mins ); // copy large hull
	VectorCopy( host.player_maxs[3], hull->clip_maxs );

	hull = &loadmodel->hulls[3];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[1], hull->clip_mins ); // copy head hull
	VectorCopy( host.player_maxs[1], hull->clip_maxs );

	if( bmodel_version == QBSP2_VERSION )
	{
		for( i = 0; i < count; i++, out++, in32++ )
		{
			out->planenum = in32->planenum;
			out->children[0] = in32->children[0];
			out->children[1] = in32->children[1];
		}
	}
	else
	{
		for( i = 0; i < count; i++, out++, in16++ )
		{
			out->planenum = in16->planenum;
#ifdef SUPPORT_BSP2_FORMAT
			out->children[0] = (unsigned short)in16->children[0];
			out->children[1] = (unsigned short)in16->children[1];

			// Arguire QBSP 'broken' clipnodes
			if( out->children[0] >= count )
				out->children[0] -= 65536;
			if( out->children[1] >= count )
				out->children[1] -= 65536;
#else
			out->children[0] = in16->children[0];
			out->children[1] = in16->children[1];
#endif
		}
	}
}

/*
=================
Mod_LoadClipnodes31
=================
*/
static void Mod_LoadClipnodes31( const dlump_t *l, const dlump_t *l2, const dlump_t *l3 )
{
	dclipnode_t	*in, *in2, *in3;
	mclipnode_t	*out, *out2, *out3;
	int		i, count, count2, count3;
	hull_t		*hull;

	in = (void *)(mod_base + l->fileofs);
	if( l->filelen % sizeof( *in )) Host_Error( "Mod_LoadClipnodes: funny lump size\n" );
	count = l->filelen / sizeof( *in );
	out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));	

	in2 = (void *)(mod_base + l2->fileofs);
	if( l2->filelen % sizeof( *in2 )) Host_Error( "Mod_LoadClipnodes2: funny lump size\n" );
	count2 = l2->filelen / sizeof( *in2 );
	out2 = Mem_Alloc( loadmodel->mempool, count2 * sizeof( *out2 ));

	in3 = (void *)(mod_base + l3->fileofs);
	if( l3->filelen % sizeof( *in3 )) Host_Error( "Mod_LoadClipnodes3: funny lump size\n" );
	count3 = l3->filelen / sizeof( *in3 );
	out3 = Mem_Alloc( loadmodel->mempool, count3 * sizeof( *out3 ));

	loadmodel->clipnodes = out;
	loadmodel->numclipnodes = count + count2 + count3;

	// hulls[0] is a point hull, always zeroed

	hull = &loadmodel->hulls[1];
	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[0], hull->clip_mins ); // copy human hull
	VectorCopy( host.player_maxs[0], hull->clip_maxs );

	hull = &loadmodel->hulls[2];
	hull->clipnodes = out2;
	hull->firstclipnode = 0;
	hull->lastclipnode = count2 - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[3], hull->clip_mins ); // copy large hull
	VectorCopy( host.player_maxs[3], hull->clip_maxs );

	hull = &loadmodel->hulls[3];
	hull->clipnodes = out3;
	hull->firstclipnode = 0;
	hull->lastclipnode = count3 - 1;
	hull->planes = loadmodel->planes;
	VectorCopy( host.player_mins[1], hull->clip_mins ); // copy head hull
	VectorCopy( host.player_maxs[1], hull->clip_maxs );

	for( i = 0; i < count; i++, out++, in++ )
	{
		out->planenum = in->planenum;
		out->children[0] = in->children[0];
		out->children[1] = in->children[1];
	}

	for( i = 0; i < count2; i++, out2++, in2++ )
	{
		out2->planenum = in2->planenum;
		out2->children[0] = in2->children[0];
		out2->children[1] = in2->children[1];
	}

	for( i = 0; i < count3; i++, out3++, in3++ )
	{
		out3->planenum = in3->planenum;
		out3->children[0] = in3->children[0];
		out3->children[1] = in3->children[1];
	}
}

/*
=================
Mod_FindModelOrigin

routine to detect bmodels with origin-brush
=================
*/
static void Mod_FindModelOrigin( const char *entities, const char *modelname, vec3_t origin )
{
	char	*pfile;
	string	keyname;
	char	token[2048];
	qboolean	model_found;
	qboolean	origin_found;

	if( !entities || !modelname || !*modelname || !origin )
		return;

	pfile = (char *)entities;

	while(( pfile = COM_ParseFile( pfile, token )) != NULL )
	{
		if( token[0] != '{' )
			Host_Error( "Mod_FindModelOrigin: found %s when expecting {\n", token );

		model_found = origin_found = false;
		VectorClear( origin );

		while( 1 )
		{
			// parse key
			if(( pfile = COM_ParseFile( pfile, token )) == NULL )
				Host_Error( "Mod_FindModelOrigin: EOF without closing brace\n" );
			if( token[0] == '}' ) break; // end of desc

			Q_strncpy( keyname, token, sizeof( keyname ));

			// parse value	
			if(( pfile = COM_ParseFile( pfile, token )) == NULL ) 
				Host_Error( "Mod_FindModelOrigin: EOF without closing brace\n" );

			if( token[0] == '}' )
				Host_Error( "Mod_FindModelOrigin: closing brace without data\n" );

			if( !Q_stricmp( keyname, "model" ) && !Q_stricmp( modelname, token ))
				model_found = true;

			if( !Q_stricmp( keyname, "origin" ))
			{
				Q_atov( origin, token, 3 );
				origin_found = true;
			}
		}

		if( model_found ) break;
	}	
}

/*
=================
Mod_MakeHull0

Duplicate the drawing hull structure as a clipping hull
=================
*/
static void Mod_MakeHull0( void )
{
	mnode_t		*in, *child;
	mclipnode_t	*out;
	hull_t		*hull;
	int		i, j, count;
	
	hull = &loadmodel->hulls[0];	
	
	in = loadmodel->nodes;
	count = loadmodel->numnodes;
	out = Mem_Alloc( loadmodel->mempool, count * sizeof( *out ));	

	hull->clipnodes = out;
	hull->firstclipnode = 0;
	hull->lastclipnode = count - 1;
	hull->planes = loadmodel->planes;

	for( i = 0; i < count; i++, out++, in++ )
	{
		out->planenum = in->plane - loadmodel->planes;

		for( j = 0; j < 2; j++ )
		{
			child = in->children[j];

			if( child->contents < 0 )
				out->children[j] = child->contents;
			else out->children[j] = child - loadmodel->nodes;
		}
	}
}

/*
=================
Mod_UnloadBrushModel

Release all uploaded textures
=================
*/
void Mod_UnloadBrushModel( model_t *mod )
{
	texture_t	*tx;
	int	i;

	ASSERT( mod != NULL );

	if( mod->type != mod_brush )
		return; // not a bmodel

	if( mod->name[0] != '*' )
	{
		for( i = 0; i < mod->numtextures; i++ )
		{
			tx = mod->textures[i];
			if( !tx || tx->gl_texturenum == tr.defaultTexture )
				continue;	// free slot

			GL_FreeTexture( tx->gl_texturenum );	// main texture
			GL_FreeTexture( tx->fb_texturenum );	// luma texture
		}

		Mem_FreePool( &mod->mempool );
	}

	memset( mod, 0, sizeof( *mod ));
}

/*
=================
Mod_LoadBrushModel
=================
*/
static void Mod_LoadBrushModel( model_t *mod, const void *buffer, qboolean *loaded )
{
	int		i, j;
	int		sample_size;
	char		*ents;
	dheader_t		*header;
	dextrahdr_t	*extrahdr;
	dmodel_t		*bm;

	if( loaded ) *loaded = false;	
	header = (dheader_t *)buffer;
	loadmodel->type = mod_brush;
	i = header->version;

	// BSP31 and BSP30 have different offsets
	if( i == XTBSP_VERSION )
		extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader31_t ));	
	else extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	switch( i )
	{
	case 28:	// get support for quake1 beta
		i = Q1BSP_VERSION;
		sample_size = 16;
		break;
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case QBSP2_VERSION:
		sample_size = 16;
		break;
	case XTBSP_VERSION:
		sample_size = 8;
		break;
	default:
		MsgDev( D_ERROR, "%s has wrong version number (%i should be %i)", loadmodel->name, i, HLBSP_VERSION );
		return;
	}

	// will be merged later
	if( world.loading )
	{
		world.lm_sample_size = sample_size;
		world.version = i;
	}
	else if( world.lm_sample_size != sample_size )
	{
		// can't mixing world and bmodels with different sample sizes!
		MsgDev( D_ERROR, "%s has wrong version number (%i should be %i)", loadmodel->name, i, world.version );
		return;		
	}

	loadmodel->numframes = sample_size; // NOTE: world store sample size into model_t->numframes
	bmodel_version = i;	// share it

	// swap all the lumps
	mod_base = (byte *)header;
	loadmodel->mempool = Mem_AllocPool( va( "^2%s^7", loadmodel->name ));

	// make sure what extrahdr is valid
	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
		extrahdr = NULL; // no extra header

	// load into heap
	if( header->lumps[LUMP_ENTITIES].fileofs <= 1024 && (header->lumps[LUMP_ENTITIES].filelen % sizeof( dplane_t )) == 0 )
	{
		// blue-shift swapped lumps
		Mod_LoadEntities( &header->lumps[LUMP_PLANES] );
		Mod_LoadPlanes( &header->lumps[LUMP_ENTITIES] );
	}
	else
	{
		// normal half-life lumps
		Mod_LoadEntities( &header->lumps[LUMP_ENTITIES] );
		Mod_LoadPlanes( &header->lumps[LUMP_PLANES] );
	}

	if( !FBitSet( host.features, ENGINE_QUAKE_COMPATIBLE ))
	{
		// Half-Life: alpha version has BSP version 29 and map version 220 (and lightdata is RGB)
		if( world.version <= 29 && world.mapversion == 220 && (header->lumps[LUMP_LIGHTING].filelen % 3) == 0 )
			world.version = bmodel_version = HLBSP_VERSION;
	}

	Mod_LoadSubmodels( &header->lumps[LUMP_MODELS] );
	Mod_LoadVertexes( &header->lumps[LUMP_VERTEXES] );
	Mod_LoadEdges( &header->lumps[LUMP_EDGES] );
	Mod_LoadSurfEdges( &header->lumps[LUMP_SURFEDGES] );
	Mod_LoadTextures( &header->lumps[LUMP_TEXTURES] );
	Mod_LoadLighting( &header->lumps[LUMP_LIGHTING], extrahdr );
	Mod_LoadVisibility( &header->lumps[LUMP_VISIBILITY] );
	Mod_LoadTexInfo( &header->lumps[LUMP_TEXINFO], extrahdr );
	Mod_LoadSurfaces( &header->lumps[LUMP_FACES] );
	Mod_LoadMarkSurfaces( &header->lumps[LUMP_MARKSURFACES] );
	Mod_LoadLeafs( &header->lumps[LUMP_LEAFS] );
	Mod_LoadNodes( &header->lumps[LUMP_NODES] );

	if( bmodel_version == XTBSP_VERSION )
		Mod_LoadClipnodes31( &header->lumps[LUMP_CLIPNODES], &header->lumps[LUMP_CLIPNODES2], &header->lumps[LUMP_CLIPNODES3] );
	else Mod_LoadClipnodes( &header->lumps[LUMP_CLIPNODES] );

	Mod_MakeHull0 ();

	ents = loadmodel->entities;
	
	// set up the submodels
	for( i = 0; i < mod->numsubmodels; i++ )
	{
		bm = &mod->submodels[i];

		mod->hulls[0].firstclipnode = bm->headnode[0];
		for( j = 1; j < MAX_MAP_HULLS; j++ )
		{
			mod->hulls[j].firstclipnode = bm->headnode[j];
//			mod->hulls[j].lastclipnode = mod->numclipnodes - 1;
		}
		
		mod->firstmodelsurface = bm->firstface;
		mod->nummodelsurfaces = bm->numfaces;

		VectorCopy( bm->mins, mod->mins );		
		VectorCopy( bm->maxs, mod->maxs );

		mod->radius = RadiusFromBounds( mod->mins, mod->maxs );
		mod->numleafs = bm->visleafs;
		mod->flags = 0;

		if( i != 0 )
		{
			// HACKHACK: c2a1 issues
			if( !bm->origin[0] && !bm->origin[1] )
				SetBits( mod->flags, MODEL_HAS_ORIGIN );

			Mod_FindModelOrigin( ents, va( "*%i", i ), bm->origin );

			// flag 2 is indicated model with origin brush!
			if( !VectorIsNull( bm->origin ))
				SetBits( mod->flags, MODEL_HAS_ORIGIN );
		}

		for( j = 0; i != 0 && j < mod->nummodelsurfaces; j++ )
		{
			msurface_t	*surf = mod->surfaces + mod->firstmodelsurface + j;

			if( surf->flags & SURF_CONVEYOR )
				mod->flags |= MODEL_CONVEYOR;

			if( surf->flags & SURF_TRANSPARENT )
				mod->flags |= MODEL_TRANSPARENT;

			// kill water backplanes for submodels (half-life rules)
			if( surf->flags & SURF_DRAWTURB )
			{
				mod->flags |= MODEL_LIQUID;

				if( surf->plane->type == PLANE_Z )
				{
					// kill bottom plane too
					if( surf->info->mins[2] == bm->mins[2] + 1.0f )
						surf->flags |= SURF_WATERCSG;
				}
				else
				{
					// kill side planes
					surf->flags |= SURF_WATERCSG;
				}
			}
		}

		if( i < mod->numsubmodels - 1 )
		{
			char	name[8];

			// duplicate the basic information
			Q_snprintf( name, sizeof( name ), "*%i", i + 1 );
			loadmodel = Mod_FindName( name, true );
			*loadmodel = *mod;
			Q_strncpy( loadmodel->name, name, sizeof( loadmodel->name ));
			loadmodel->mempool = NULL;
			mod = loadmodel;
		}
	}

	if( loaded ) *loaded = true;	// all done
}

/*
==================
Mod_FindName

==================
*/
model_t *Mod_FindName( const char *filename, qboolean create )
{
	model_t	*mod;
	char	name[64];
	int	i;
	
	if( !filename || !filename[0] )
		return NULL;

	if( *filename == '!' ) filename++;
	Q_strncpy( name, filename, sizeof( name ));
	COM_FixSlashes( name );
		
	// search the currently loaded models
	for( i = 0, mod = cm_models; i < cm_nummodels; i++, mod++ )
	{
		if( !mod->name[0] ) continue;
		if( !Q_stricmp( mod->name, name ))
		{
			// prolonge registration
			mod->needload = world.load_sequence;
			return mod;
		}
	}

	if( !create ) return NULL;			

	// find a free model slot spot
	for( i = 0, mod = cm_models; i < cm_nummodels; i++, mod++ )
		if( !mod->name[0] ) break; // this is a valid spot

	if( i == cm_nummodels )
	{
		if( cm_nummodels == MAX_MODELS )
			Host_Error( "Mod_ForName: MAX_MODELS limit exceeded\n" );
		cm_nummodels++;
	}

	// copy name, so model loader can find model file
	Q_strncpy( mod->name, name, sizeof( mod->name ));

	return mod;
}

/*
==================
Mod_LoadModel

Loads a model into the cache
==================
*/
model_t *Mod_LoadModel( model_t *mod, qboolean crash )
{
	byte	*buf;
	char	tempname[64];
	qboolean	loaded;

	if( !mod )
	{
		if( crash ) Host_Error( "Mod_ForName: NULL model\n" );
		else MsgDev( D_ERROR, "Mod_ForName: NULL model\n" );
		return NULL;		
	}

	// check if already loaded (or inline bmodel)
	if( mod->mempool || mod->name[0] == '*' )
		return mod;

	// store modelname to show error
	Q_strncpy( tempname, mod->name, sizeof( tempname ));
	COM_FixSlashes( tempname );

	buf = FS_LoadFile( tempname, NULL, false );

	if( !buf )
	{
		memset( mod, 0, sizeof( model_t ));

		if( crash ) Host_Error( "Mod_ForName: %s couldn't load\n", tempname );
		else MsgDev( D_ERROR, "Mod_ForName: %s couldn't load\n", tempname );

		return NULL;
	}

	FS_FileBase( mod->name, modelname );

	MsgDev( D_NOTE, "Mod_LoadModel: %s\n", mod->name );
	mod->needload = world.load_sequence; // register mod
	mod->type = mod_bad;
	loadmodel = mod;

	// call the apropriate loader
	switch( *(uint *)buf )
	{
	case IDSTUDIOHEADER:
		Mod_LoadStudioModel( mod, buf, &loaded );
		break;
	case IDSPRITEHEADER:
		Mod_LoadSpriteModel( mod, buf, &loaded, 0 );
		break;
	case IDALIASHEADER:
		Mod_LoadAliasModel( mod, buf, &loaded );
		break;
	case Q1BSP_VERSION:
	case HLBSP_VERSION:
	case XTBSP_VERSION:
#ifdef SUPPORT_BSP2_FORMAT
	case QBSP2_VERSION:
#endif
		Mod_LoadBrushModel( mod, buf, &loaded );
		break;
	default:
		Mem_Free( buf );
		if( crash ) Host_Error( "Mod_ForName: %s unknown format\n", tempname );
		else MsgDev( D_ERROR, "Mod_ForName: %s unknown format\n", tempname );
		return NULL;
	}

	if( !loaded )
	{
		Mod_FreeModel( mod );
		Mem_Free( buf );

		if( crash ) Host_Error( "Mod_ForName: %s couldn't load\n", tempname );
		else MsgDev( D_ERROR, "Mod_ForName: %s couldn't load\n", tempname );

		return NULL;
	}
	else
	{
		if( host.type == HOST_DEDICATED )
		{
			if( svgame.physFuncs.Mod_ProcessUserData != NULL )
			{
				// let the server.dll load custom data
				svgame.physFuncs.Mod_ProcessUserData( mod, true, buf );
			}
		}
		else
		{
			if( clgame.drawFuncs.Mod_ProcessUserData != NULL )
			{
				// let the client.dll load custom data
				clgame.drawFuncs.Mod_ProcessUserData( mod, true, buf );
			}
		}
	}

	Mem_Free( buf );

	return mod;
}

/*
==================
Mod_ForName

Loads in a model for the given name
==================
*/
model_t *Mod_ForName( const char *name, qboolean crash )
{
	model_t	*mod;
	
	mod = Mod_FindName( name, true );
	return Mod_LoadModel( mod, crash );
}

/*
==================
Mod_LoadWorld

Loads in the map and all submodels
==================
*/
void Mod_LoadWorld( const char *name, uint *checksum, qboolean multiplayer )
{
	int	i;

	// now replacement table is invalidate
	memset( com_models, 0, sizeof( com_models ));
	com_models[1] = cm_models; // make link to world

	// update the lightmap blocksize
	if( host.features & ENGINE_LARGE_LIGHTMAPS )
		world.block_size = BLOCK_SIZE_MAX;
	else world.block_size = BLOCK_SIZE_DEFAULT;

	if( !Q_stricmp( cm_models[0].name, name ))
	{
		// recalc the checksum in force-mode
		CRC32_MapFile( &world.checksum, worldmodel->name, multiplayer );

		// singleplayer mode: server already loaded map
		if( checksum ) *checksum = world.checksum;

		// still have the right version
		return;
	}

	// clear all studio submodels on restart
	for( i = 1; i < cm_nummodels; i++ )
	{
		if( cm_models[i].type == mod_studio )
			cm_models[i].submodels = NULL;
		else if( cm_models[i].type == mod_brush )
			Mod_FreeModel( cm_models + i );
	}

	// purge all submodels
	Mod_FreeModel( &cm_models[0] );
	Mem_EmptyPool( com_studiocache );
	world.load_sequence++;	// now all models are invalid

	// load the newmap
	world.loading = true;
	worldmodel = Mod_ForName( name, true );
	CRC32_MapFile( &world.checksum, worldmodel->name, multiplayer );
	world.loading = false;

	if( checksum ) *checksum = world.checksum;
}

/*
==================
Mod_FreeUnused

Purge all unused models
==================
*/
void Mod_FreeUnused( void )
{
	model_t	*mod;
	int	i;

	for( i = 0, mod = cm_models; i < cm_nummodels; i++, mod++ )
	{
		if( !mod->name[0] ) continue;
		if( mod->needload != world.load_sequence )
			Mod_FreeModel( mod );
	}
}

/*
===================
Mod_GetType
===================
*/
modtype_t Mod_GetType( int handle )
{
	model_t	*mod = Mod_Handle( handle );

	if( !mod ) return mod_bad;
	return mod->type;
}

/*
===================
Mod_GetFrames
===================
*/
void Mod_GetFrames( int handle, int *numFrames )
{
	model_t	*mod = Mod_Handle( handle );

	if( !numFrames ) return;
	if( !mod )
	{
		*numFrames = 1;
		return;
	}

	if( mod->type == mod_brush )
		*numFrames = 2; // regular and alternate animation
	else *numFrames = mod->numframes;
	if( *numFrames < 1 ) *numFrames = 1;
}

/*
===================
Mod_FrameCount

model_t as input
===================
*/
int Mod_FrameCount( model_t *mod )
{
	if( !mod ) return 1;

	switch( mod->type )
	{
	case mod_sprite:
	case mod_studio:
		return mod->numframes;
	case mod_brush:
		return 2; // regular and alternate animation
	default:
		return 1;
	}
}

/*
===================
Mod_GetBounds
===================
*/
void Mod_GetBounds( int handle, vec3_t mins, vec3_t maxs )
{
	model_t	*cmod;

	if( handle <= 0 ) return;
	cmod = Mod_Handle( handle );

	if( cmod )
	{
		if( mins ) VectorCopy( cmod->mins, mins );
		if( maxs ) VectorCopy( cmod->maxs, maxs );
	}
	else
	{
		MsgDev( D_ERROR, "Mod_GetBounds: NULL model %i\n", handle );
		if( mins ) VectorClear( mins );
		if( maxs ) VectorClear( maxs );
	}
}

/*
===============
Mod_Calloc

===============
*/
void *Mod_Calloc( int number, size_t size )
{
	cache_user_t	*cu;

	if( number <= 0 || size <= 0 ) return NULL;
	cu = (cache_user_t *)Mem_Alloc( com_studiocache, sizeof( cache_user_t ) + number * size );
	cu->data = (void *)cu; // make sure what cu->data is not NULL

	return cu;
}

/*
===============
Mod_CacheCheck

===============
*/
void *Mod_CacheCheck( cache_user_t *c )
{
	return Cache_Check( com_studiocache, c );
}

/*
===============
Mod_LoadCacheFile

===============
*/
void Mod_LoadCacheFile( const char *filename, cache_user_t *cu )
{
	byte	*buf;
	string	name;
	size_t	i, j, size;

	ASSERT( cu != NULL );

	if( !filename || !filename[0] ) return;

	// eliminate '!' symbol (i'm doesn't know what this doing)
	for( i = j = 0; i < Q_strlen( filename ); i++ )
	{
		if( filename[i] == '!' ) continue;
		else if( filename[i] == '\\' ) name[j] = '/';
		else name[j] = Q_tolower( filename[i] );
		j++;
	}
	name[j] = '\0';

	buf = FS_LoadFile( name, &size, false );
	if( !buf || !size ) Host_Error( "LoadCacheFile: ^1can't load %s^7\n", filename );
	cu->data = Mem_Alloc( com_studiocache, size );
	memcpy( cu->data, buf, size );
	Mem_Free( buf );
}

/*
===================
Mod_RegisterModel

register model with shared index
===================
*/
qboolean Mod_RegisterModel( const char *name, int index )
{
	model_t	*mod;

	if( index < 0 || index > MAX_MODELS )
		return false;

	// this array used for acess to servermodels
	mod = Mod_ForName( name, false );
	com_models[index] = mod;

	return ( mod != NULL );
}

/*
===============
Mod_AliasExtradata

===============
*/
void *Mod_AliasExtradata( model_t *mod )
{
	if( mod && mod->type == mod_alias )
		return mod->cache.data;
	return NULL;
}

/*
===============
Mod_StudioExtradata

===============
*/
void *Mod_StudioExtradata( model_t *mod )
{
	if( mod && mod->type == mod_studio )
		return mod->cache.data;
	return NULL;
}

/*
==================
Mod_Handle

==================
*/
model_t *Mod_Handle( int handle )
{
	if( handle < 0 || handle >= MAX_MODELS )
	{
		MsgDev( D_NOTE, "Mod_Handle: bad handle #%i\n", handle );
		return NULL;
	}
	return com_models[handle];
}

/*
==================
Mod_CheckLump

check lump for existing
==================
*/
int Mod_CheckLump( const char *filename, const int lump, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", true );
	byte		buffer[sizeof( dheader31_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	dextrahdr_t	*extrahdr;
	dheader_t		*header;

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION && header->version != XTBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	// BSP31 and BSP30 have different offsets
	if( header->version == XTBSP_VERSION )
		extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader31_t ));	
	else extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	if( lumpsize )
		*lumpsize = extrahdr->lumps[lump].filelen;

	FS_Close( f );

	return LUMP_LOAD_OK;
}

/*
==================
Mod_ReadLump

reading random lump by user request
==================
*/
int Mod_ReadLump( const char *filename, const int lump, void **lumpdata, int *lumpsize )
{
	file_t		*f = FS_Open( filename, "rb", true );
	byte		buffer[sizeof( dheader31_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	dextrahdr_t	*extrahdr;
	dheader_t		*header;
	byte		*data;
	int		length;

	if( !f ) return LUMP_LOAD_COULDNT_OPEN;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION && header->version != XTBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_BAD_VERSION;
	}

	// BSP31 and BSP30 have different offsets
	if( header->version == XTBSP_VERSION )
		extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader31_t ));	
	else extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_LOAD_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_LOAD_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen <= 0 )
	{
		FS_Close( f );
		return LUMP_LOAD_NOT_EXIST;
	}

	data = malloc( extrahdr->lumps[lump].filelen + 1 );
	length = extrahdr->lumps[lump].filelen;

	if( !data )
	{
		FS_Close( f );
		return LUMP_LOAD_MEM_FAILED;
	}

	FS_Seek( f, extrahdr->lumps[lump].fileofs, SEEK_SET );

	if( FS_Read( f, data, length ) != length )
	{
		Mem_Free( data );
		FS_Close( f );
		return LUMP_LOAD_CORRUPTED;
	}

	data[length] = 0; // write term
	FS_Close( f );

	if( lumpsize )
		*lumpsize = length;
	*lumpdata = data;

	return LUMP_LOAD_OK;
}

/*
==================
Mod_SaveLump

writing lump by user request
only empty lumps is allows
==================
*/
int Mod_SaveLump( const char *filename, const int lump, void *lumpdata, int lumpsize )
{
	file_t		*f = FS_Open( filename, "e+b", true );
	byte		buffer[sizeof( dheader31_t ) + sizeof( dextrahdr_t )];
	size_t		prefetch_size = sizeof( buffer );
	dextrahdr_t	*extrahdr;
	dheader_t		*header;

	if( !f ) return LUMP_SAVE_COULDNT_OPEN;

	if( !lumpdata || lumpsize <= 0 )
		return LUMP_SAVE_NO_DATA;

	if( FS_Read( f, buffer, prefetch_size ) != prefetch_size )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_HEADER;
	}

	header = (dheader_t *)buffer;

	if( header->version != HLBSP_VERSION && header->version != XTBSP_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_BAD_VERSION;
	}

	// BSP31 and BSP30 have different offsets
	if( header->version == XTBSP_VERSION )
		extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader31_t ));	
	else extrahdr = (dextrahdr_t *)((byte *)buffer + sizeof( dheader_t ));

	if( extrahdr->id != IDEXTRAHEADER || extrahdr->version != EXTRA_VERSION )
	{
		FS_Close( f );
		return LUMP_SAVE_NO_EXTRADATA;
	}

	if( lump < 0 || lump >= EXTRA_LUMPS )
	{
		FS_Close( f );
		return LUMP_SAVE_INVALID_NUM;
	}

	if( extrahdr->lumps[lump].filelen != 0 )
	{
		FS_Close( f );
		return LUMP_SAVE_ALREADY_EXIST;
	}

	FS_Seek( f, 0, SEEK_END );

	// will be saved later
	extrahdr->lumps[lump].fileofs = FS_Tell( f );
	extrahdr->lumps[lump].filelen = lumpsize;

	if( FS_Write( f, lumpdata, lumpsize ) != lumpsize )
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	// update the header
	if( header->version == XTBSP_VERSION )
		FS_Seek( f, sizeof( dheader31_t ), SEEK_SET );
	else FS_Seek( f, sizeof( dheader_t ), SEEK_SET );

	if( FS_Write( f, extrahdr, sizeof( dextrahdr_t )) != sizeof( dextrahdr_t ))
	{
		FS_Close( f );
		return LUMP_SAVE_CORRUPTED;
	}

	FS_Close( f );
	return LUMP_SAVE_OK;
}