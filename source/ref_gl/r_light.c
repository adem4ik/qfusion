/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (C) 2002-2007 Victor Luchits

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// r_light.c

#include "r_local.h"

/*
=============================================================================

DYNAMIC LIGHTS BLEND RENDERING

=============================================================================
*/

static shader_t *r_coronaShader;
static drawSurfaceType_t r_coronaSurfs[MAX_DLIGHTS];

/*
* R_InitCoronas
*/
void R_InitCoronas( void )
{
	int i;

	r_coronaShader = R_LoadShader( "***r_coronaTexture***", SHADER_TYPE_CORONA, qtrue );

	for( i = 0; i < MAX_DLIGHTS; i++ ) {
		r_coronaSurfs[i] = ST_CORONA;
	}
}

/*
* R_BeginCoronaSurf
*/
qboolean R_BeginCoronaSurf( const entity_t *e, const shader_t *shader, 
	const mfog_t *fog, drawSurfaceType_t *drawSurf )
{
	RB_BindStreamVBO( qtrue, GL_TRIANGLES );
	return qtrue;
}

/*
* R_BatchCoronaSurf
*/
void R_BatchCoronaSurf( const entity_t *e, const shader_t *shader, 
	const mfog_t *fog, drawSurfaceType_t *drawSurf )
{
	int i;
	vec3_t origin, point;
	vec3_t v_left, v_up;
	dlight_t *light = rsc.dlights + (drawSurf - r_coronaSurfs);
	float radius = light->intensity, colorscale;
	float up = radius, down = -radius, left = -radius, right = radius;
	vec4_t xyz[4] = { {0,0,0,1}, {0,0,0,1}, {0,0,0,1}, {0,0,0,1} };
	vec4_t normals[4] = { {0,0,0,0}, {0,0,0,0}, {0,0,0,0}, {0,0,0,0} };
	byte_vec4_t colors[4];
	vec2_t texcoords[4] = { {0, 1}, {0, 0}, {1,0}, {1,1} };
	mesh_t mesh;

	VectorCopy( light->origin, origin );

	VectorCopy( &rn.viewAxis[AXIS_RIGHT], v_left );
	VectorCopy( &rn.viewAxis[AXIS_UP], v_up );

	if( rn.renderFlags & RF_MIRRORVIEW )
		VectorInverse( v_left );

	VectorMA( origin, down, v_up, point );
	VectorMA( point, left, v_left, xyz[0] );
	VectorMA( point, right, v_left, xyz[3] );

	VectorMA( origin, up, v_up, point );
	VectorMA( point, left, v_left, xyz[1] );
	VectorMA( point, right, v_left, xyz[2] );

	colorscale = 255.0 * bound( 0, r_coronascale->value, 1.0 );
	Vector4Set( colors[0],
		bound( 0, light->color[0] * colorscale, 255 ),
		bound( 0, light->color[1] * colorscale, 255 ),
		bound( 0, light->color[2] * colorscale, 255 ),
		255 );
	for( i = 1; i < 4; i++ )
		Vector4Copy( colors[0], colors[i] );

	// backend knows how to count elements for quads
	memset( &mesh, 0, sizeof( mesh ) );
	mesh.numVerts = 4;
	mesh.xyzArray = xyz;
	mesh.normalsArray = normals;
	mesh.stArray = texcoords;
	mesh.colorsArray[0] = colors;

	RB_BatchMesh( &mesh );
}

/*
* R_DrawCoronas
*/
void R_DrawCoronas( void )
{
	unsigned int i;
	float dist;
	dlight_t *light;
	rtrace_t tr;

	if( r_dynamiclight->integer != 2 )
		return;

	for( i = 0; i < rsc.numDlights; i++ )
	{
		light = rsc.dlights + i;
		dist =
			rn.viewAxis[AXIS_FORWARD+0] * ( light->origin[0] - rn.viewOrigin[0] ) +
			rn.viewAxis[AXIS_FORWARD+1] * ( light->origin[1] - rn.viewOrigin[1] ) +
			rn.viewAxis[AXIS_FORWARD+2] * ( light->origin[2] - rn.viewOrigin[2] );
		if( dist < 24.0f )
			continue;
		dist -= light->intensity;

		R_TraceLine( &tr, light->origin, rn.viewOrigin, SURF_NONSOLID );
		if( tr.fraction != 1.0f )
			continue;

		R_AddDSurfToDrawList( rsc.polyent, 
			R_FogForSphere( light->origin, 1 ), 
			r_coronaShader, 
			Distance( rn.viewOrigin, light->origin ), 0, NULL, &r_coronaSurfs[i] );
	}
}

/*
* R_ShutdownCoronas
*/
void R_ShutdownCoronas( void )
{
	r_coronaShader = NULL;
}

//===================================================================

/*
* R_LightForOrigin
*/
void R_LightForOrigin( const vec3_t origin, vec3_t dir, vec4_t ambient, vec4_t diffuse, float radius )
{
	int i, j;
	int k, s;
	int vi[3], elem[4];
	float dot, t[8], scale;
	vec3_t vf, vf2, tdir;
	vec3_t ambientLocal, diffuseLocal;
	vec_t *gridSize, *gridMins;
	int *gridBounds;
	static mgridlight_t lightarray[8];
	lightstyle_t *lightStyles = rsc.lightStyles;

	VectorSet( ambientLocal, 0, 0, 0 );
	VectorSet( diffuseLocal, 0, 0, 0 );

	if( !rsh.worldModel /* || (rn.refdef.rdflags & RDF_NOWORLDMODEL)*/ ||
		!rsh.worldBrushModel->lightgrid || !rsh.worldBrushModel->numlightgridelems )
	{
		VectorSet( dir, 0.1f, 0.2f, 0.7f );
		goto dynamic;
	}

	gridSize = rsh.worldBrushModel->gridSize;
	gridMins = rsh.worldBrushModel->gridMins;
	gridBounds = rsh.worldBrushModel->gridBounds;

	for( i = 0; i < 3; i++ )
	{
		vf[i] = ( origin[i] - gridMins[i] ) / gridSize[i];
		vi[i] = (int)vf[i];
		vf[i] = vf[i] - floor( vf[i] );
		vf2[i] = 1.0f - vf[i];
	}

	elem[0] = vi[2] * gridBounds[3] + vi[1] * gridBounds[0] + vi[0];
	elem[1] = elem[0] + gridBounds[0];
	elem[2] = elem[0] + gridBounds[3];
	elem[3] = elem[2] + gridBounds[0];

	for( i = 0; i < 4; i++ )
	{
		lightarray[i*2+0] = *rsh.worldBrushModel->lightarray[bound( 0, elem[i]+0, 
			(int)rsh.worldBrushModel->numlightarrayelems-1)];
		lightarray[i*2+1] = *rsh.worldBrushModel->lightarray[bound( 1, elem[i]+1, 
			(int)rsh.worldBrushModel->numlightarrayelems-1)];
	}

	t[0] = vf2[0] * vf2[1] * vf2[2];
	t[1] = vf[0] * vf2[1] * vf2[2];
	t[2] = vf2[0] * vf[1] * vf2[2];
	t[3] = vf[0] * vf[1] * vf2[2];
	t[4] = vf2[0] * vf2[1] * vf[2];
	t[5] = vf[0] * vf2[1] * vf[2];
	t[6] = vf2[0] * vf[1] * vf[2];
	t[7] = vf[0] * vf[1] * vf[2];

	VectorClear( dir );

	for( i = 0; i < 4; i++ )
	{
		R_LatLongToNorm( lightarray[i*2].direction, tdir );
		VectorScale( tdir, t[i*2], tdir );
		for( k = 0; k < MAX_LIGHTMAPS && ( s = lightarray[i*2].styles[k] ) != 255; k++ )
		{
			dir[0] += lightStyles[s].rgb[0] * tdir[0];
			dir[1] += lightStyles[s].rgb[1] * tdir[1];
			dir[2] += lightStyles[s].rgb[2] * tdir[2];
		}

		R_LatLongToNorm( lightarray[i*2+1].direction, tdir );
		VectorScale( tdir, t[i*2+1], tdir );
		for( k = 0; k < MAX_LIGHTMAPS && ( s = lightarray[i*2+1].styles[k] ) != 255; k++ )
		{
			dir[0] += lightStyles[s].rgb[0] * tdir[0];
			dir[1] += lightStyles[s].rgb[1] * tdir[1];
			dir[2] += lightStyles[s].rgb[2] * tdir[2];
		}
	}

	for( j = 0; j < 3; j++ )
	{
		if( ambient )
		{
			for( i = 0; i < 4; i++ )
			{
				for( k = 0; k < MAX_LIGHTMAPS; k++ )
				{
					if( ( s = lightarray[i*2].styles[k] ) != 255 )
						ambientLocal[j] += t[i*2] * lightarray[i*2].ambient[k][j] * lightStyles[s].rgb[j];
					if( ( s = lightarray[i*2+1].styles[k] ) != 255 )
						ambientLocal[j] += t[i*2+1] * lightarray[i*2+1].ambient[k][j] * lightStyles[s].rgb[j];
				}
			}
		}
		if( diffuse || radius )
		{
			for( i = 0; i < 4; i++ )
			{
				for( k = 0; k < MAX_LIGHTMAPS; k++ )
				{
					if( ( s = lightarray[i*2].styles[k] ) != 255 )
						diffuseLocal[j] += t[i*2] * lightarray[i*2].diffuse[k][j] * lightStyles[s].rgb[j];
					if( ( s = lightarray[i*2+1].styles[k] ) != 255 )
						diffuseLocal[j] += t[i*2+1] * lightarray[i*2+1].diffuse[k][j] * lightStyles[s].rgb[j];
				}
			}
		}
	}

	// convert to grayscale
	if( r_lighting_grayscale->integer ) {
		vec_t grey;

		if( ambient ) {
			grey = ColorGrayscale( ambientLocal );
			ambientLocal[0] = ambientLocal[1] = ambientLocal[2] = bound( 0, grey, 255 );
		}

		if( diffuse || radius ) {
			grey = ColorGrayscale( diffuseLocal );
			diffuseLocal[0] = diffuseLocal[1] = diffuseLocal[2] = bound( 0, grey, 255 );
		}
	}

dynamic:
	// add dynamic lights
	if( radius && r_dynamiclight->integer )
	{
		unsigned int lnum;
		dlight_t *dl;
		float dist, dist2, add;
		vec3_t direction;
		qboolean anyDlights = qfalse;

		for( lnum = 0; lnum < rsc.numDlights; lnum++ )
		{
			dl = rsc.dlights + lnum;
			if( Distance( dl->origin, origin ) > dl->intensity + radius )
				continue;

			VectorSubtract( dl->origin, origin, direction );
			dist = VectorLength( direction );

			if( !dist || dist > dl->intensity + radius )
				continue;

			if( !anyDlights )
			{
				VectorNormalizeFast( dir );
				anyDlights = qtrue;
			}

			add = 1.0 - (dist / (dl->intensity + radius));
			dist2 = add * 0.5 / dist;
			for( i = 0; i < 3; i++ )
			{
				dot = dl->color[i] * add;
				diffuseLocal[i] += dot;
				ambientLocal[i] += dot * 0.05;
				dir[i] += direction[i] * dist2;
			}
		}
	}

	VectorNormalizeFast( dir );

	scale = mapConfig.mapLightColorScale / 255.0f;

	if( ambient )
	{
		float scale2 = bound( 0.0f, r_lighting_ambientscale->value, 1.0f ) * scale;

		for( i = 0; i < 3; i++ )
			ambient[i] = ambientLocal[i] * scale2;

		ambient[3] = 1.0f;
	}

	if( diffuse )
	{
		float scale2 = bound( 0.0f, r_lighting_directedscale->value, 1.0f ) * scale;

		for( i = 0; i < 3; i++ )
			diffuse[i] = diffuseLocal[i] * scale2;

		diffuse[3] = 1.0f;
	}
}

/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

#define MAX_LIGHTMAP_IMAGES		1024

static qbyte *r_lightmapBuffer;
static int r_lightmapBufferSize;
static image_t *r_lightmapTextures[MAX_LIGHTMAP_IMAGES];
static int r_numUploadedLightmaps;
static int r_maxLightmapBlockSize;

/*
* R_BuildLightmap
*/
static void R_BuildLightmap( int w, int h, qboolean deluxe, const qbyte *data, qbyte *dest, int blockWidth )
{
	int x, y;
	qbyte *rgba;
	int bits = mapConfig.pow2MapOvrbr;

	if( !data || (r_fullbright->integer && !deluxe) )
	{
		int val = deluxe ? 127 : 255;
		for( y = 0; y < h; y++, dest )
			memset( dest + y * blockWidth, val, w * LIGHTMAP_BYTES * sizeof( *dest ) );
		return;
	}

	if( deluxe || ( !bits && !r_lighting_grayscale->integer ) )
	{
		int wB = w * LIGHTMAP_BYTES;
		for( y = 0, rgba = dest; y < h; y++, data += wB, rgba += blockWidth )
		{
			memcpy( rgba, data, wB );
		}
	}
	else
	{
		float scaled[3];
		float intensity = (1 << bits) / 255.0f;

		for( y = 0; y < h; y++ )
		{
			for( x = 0, rgba = dest + y * blockWidth; x < w; x++, data += LIGHTMAP_BYTES, rgba += LIGHTMAP_BYTES )
			{
				vec3_t normalized;

				scaled[0] = (float)( (int)data[0] ) * intensity;
				scaled[1] = (float)( (int)data[1] ) * intensity;
				scaled[2] = (float)( (int)data[2] ) * intensity;

				ColorNormalize( scaled, normalized );

				// monochrome lighting: convert to grayscale
				if( r_lighting_grayscale->integer ) {
					vec_t grey = ColorGrayscale( normalized );
					normalized[0] = normalized[1] = normalized[2] = bound( 0, grey, 1 );
				}

				rgba[0] = ( qbyte )( normalized[0] * 255 );
				rgba[1] = ( qbyte )( normalized[1] * 255 );
				rgba[2] = ( qbyte )( normalized[2] * 255 );
			}
		}
	}
}

/*
* R_UploadLightmap
*/
static int R_UploadLightmap( const char *name, qbyte *data, int w, int h )
{
	image_t *image;
	char uploadName[128];

	if( !name || !data )
		return r_numUploadedLightmaps;
	if( r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES ) {
		// not sure what I'm supposed to do here.. an unrealistic scenario
		Com_Printf( S_COLOR_YELLOW "Warning: r_numUploadedLightmaps == MAX_LIGHTMAP_IMAGES\n" ); 
		return 0;
	}

	Q_snprintfz( uploadName, sizeof( uploadName ), "%s%i", name, r_numUploadedLightmaps );

	image = R_LoadImage( uploadName, (qbyte **)( &data ), w, h, 
		IT_CLAMP|IT_NOPICMIP|IT_NOMIPMAP|IT_NOCOMPRESS, LIGHTMAP_BYTES );
	r_lightmapTextures[r_numUploadedLightmaps] = image;

	return r_numUploadedLightmaps++;
}

/*
* R_PackLightmaps
*/
static int R_PackLightmaps( int num, int w, int h, int size, int stride, qboolean deluxe, 
	const char *name, const qbyte *data, mlightmapRect_t *rects )
{
	int i, x, y, root;
	qbyte *block;
	int lightmapNum;
	int rectX, rectY, rectSize;
	int maxX, maxY, max, xStride;
	double tw, th, tx, ty;
	mlightmapRect_t *rect;

	maxX = r_maxLightmapBlockSize / w;
	maxY = r_maxLightmapBlockSize / h;
	max = min( maxX, maxY );

	ri.Com_DPrintf( "Packing %i lightmap(s) -> ", num );

	if( !max || num == 1 || !mapConfig.lightmapsPacking /* || !r_lighting_packlightmaps->integer*/ )
	{
		// process as it is
		R_BuildLightmap( w, h, deluxe, data, r_lightmapBuffer, w * LIGHTMAP_BYTES );

		lightmapNum = R_UploadLightmap( name, r_lightmapBuffer, w, h );
		if( rects )
		{
			rects[0].texNum = lightmapNum;

			// this is not a real texture matrix, but who cares?
			rects[0].texMatrix[0][0] = 1; rects[0].texMatrix[0][1] = 0;
			rects[0].texMatrix[1][0] = 1; rects[0].texMatrix[1][1] = 0;
		}

		ri.Com_DPrintf( "\n" );

		return 1;
	}

	// find the nearest square block size
	root = ( int )sqrt( (float)num );
	if( root > max )
		root = max;

	// keep row size a power of two
	for( i = 1; i < root; i <<= 1 ) ;
	if( i > root )
		i >>= 1;
	root = i;

	num -= root * root;
	rectX = rectY = root;

	if( maxY > maxX )
	{
		for(; ( num >= root ) && ( rectY < maxY ); rectY++, num -= root ) ;

		//if( !glConfig.ext.texture_non_power_of_two )
		{
			// sample down if not a power of two
			for( y = 1; y < rectY; y <<= 1 ) ;
			if( y > rectY )
				y >>= 1;
			rectY = y;
		}
	}
	else
	{
		for(; ( num >= root ) && ( rectX < maxX ); rectX++, num -= root ) ;

		//if( !glConfig.ext.texture_non_power_of_two )
		{
			// sample down if not a power of two
			for( x = 1; x < rectX; x <<= 1 ) ;
			if( x > rectX )
				x >>= 1;
			rectX = x;
		}
	}

	tw = 1.0 / (double)rectX;
	th = 1.0 / (double)rectY;

	xStride = w * LIGHTMAP_BYTES;
	rectSize = ( rectX * w ) * ( rectY * h ) * LIGHTMAP_BYTES * sizeof( *r_lightmapBuffer );
	if( rectSize > r_lightmapBufferSize )
	{
		if( r_lightmapBuffer )
			R_Free( r_lightmapBuffer );
		r_lightmapBuffer = R_MallocExt( r_mempool, rectSize, 0, 0 );
		memset( r_lightmapBuffer, 255, rectSize );
		r_lightmapBufferSize = rectSize;
	}

	ri.Com_DPrintf( "%ix%i : %ix%i\n", rectX, rectY, rectX * w, rectY * h );

	block = r_lightmapBuffer;
	for( y = 0, ty = 0.0, num = 0, rect = rects; y < rectY; y++, ty += th, block += rectX * xStride * h )
	{
		for( x = 0, tx = 0.0; x < rectX; x++, tx += tw, num++, data += size * stride )
		{
			R_BuildLightmap( w, h, 
				mapConfig.deluxeMappingEnabled && ( num & 1 ) ? qtrue : qfalse, 
				data, block + x * xStride, rectX * xStride );

			// this is not a real texture matrix, but who cares?
			if( rects )
			{
				rect->texMatrix[0][0] = tw; rect->texMatrix[0][1] = tx;
				rect->texMatrix[1][0] = th; rect->texMatrix[1][1] = ty;
				rect += stride;
			}
		}
	}

	lightmapNum = R_UploadLightmap( name, r_lightmapBuffer, rectX * w, rectY * h );
	if( rects )
	{
		for( i = 0, rect = rects; i < num; i++, rect += stride )
			rect->texNum = lightmapNum;
	}

	return num;
}

/*
* R_BuildLightmaps
*/
void R_BuildLightmaps( model_t *mod, int numLightmaps, int w, int h, const qbyte *data, mlightmapRect_t *rects )
{
	int i, j, p, m;
	int numBlocks, size, stride;
	const qbyte *lmData;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = (( mbrushmodel_t * )mod->extradata);

	if( !mapConfig.lightmapsPacking )
		size = max( w, h );
	else
		for( size = 1; ( size < r_lighting_maxlmblocksize->integer ) 
			&& ( size < glConfig.maxTextureSize ); size <<= 1 ) ;

	if( mapConfig.deluxeMappingEnabled && ( ( size == w ) || ( size == h ) ) )
	{
		Com_Printf( S_COLOR_YELLOW "Lightmap blocks larger than %ix%i aren't supported" 
			", deluxemaps will be disabled\n", size, size );
		mapConfig.deluxeMappingEnabled = qfalse;
	}

	r_maxLightmapBlockSize = size;
	size = w * h * LIGHTMAP_BYTES;
	r_lightmapBufferSize = size;
	r_lightmapBuffer = R_MallocExt( r_mempool, r_lightmapBufferSize, 0, 0 );
	r_numUploadedLightmaps = 0;

	m = 1;
	lmData = data;
	stride = 1;
	numBlocks = numLightmaps;

	if( mapConfig.deluxeMaps && !mapConfig.deluxeMappingEnabled )
	{
		m = 2;
		stride = 2;
		numLightmaps /= 2;
	}

	for( i = 0, j = 0; i < numBlocks; i += p * m, j += p )
		p = R_PackLightmaps( numLightmaps - j, w, h, size, stride, 
		qfalse, "*lm", lmData + j * size * stride, &rects[i] );

	if( r_lightmapBuffer )
		R_Free( r_lightmapBuffer );

	loadbmodel->lightmapImages = Mod_Malloc( mod, sizeof( *loadbmodel->lightmapImages ) * r_numUploadedLightmaps );
	memcpy( loadbmodel->lightmapImages, r_lightmapTextures, 
		sizeof( *loadbmodel->lightmapImages ) * r_numUploadedLightmaps );
	loadbmodel->numLightmapImages = r_numUploadedLightmaps;

	ri.Com_DPrintf( "Packed %i lightmap blocks into %i texture(s)\n", numBlocks, r_numUploadedLightmaps );
}

/*
* R_TouchLightmapImages
*/
void R_TouchLightmapImages( model_t *mod )
{
	unsigned int i;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = (( mbrushmodel_t * )mod->extradata);

	for( i = 0; i < loadbmodel->numLightmapImages; i++ ) {
		R_TouchImage( loadbmodel->lightmapImages[i] );
	}
}

/*
=============================================================================

LIGHT STYLE MANAGEMENT

=============================================================================
*/

/*
* R_InitLightStyles
*/
void R_InitLightStyles( model_t *mod )
{
	int i;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = (( mbrushmodel_t * )mod->extradata);
	loadbmodel->superLightStyles = Mod_Malloc( mod, sizeof( *loadbmodel->superLightStyles ) * MAX_LIGHTSTYLES );
	loadbmodel->numSuperLightStyles = 0;

	for( i = 0; i < MAX_LIGHTSTYLES; i++ )
	{
		rsc.lightStyles[i].rgb[0] = 1;
		rsc.lightStyles[i].rgb[1] = 1;
		rsc.lightStyles[i].rgb[2] = 1;
	}
}

/*
* R_AddSuperLightStyle
*/
superLightStyle_t *R_AddSuperLightStyle( model_t *mod, const int *lightmaps,
	const qbyte *lightmapStyles, const qbyte *vertexStyles, mlightmapRect_t **lmRects )
{
	unsigned int i, j;
	superLightStyle_t *sls;
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = (( mbrushmodel_t * )mod->extradata);

	for( i = 0, sls = loadbmodel->superLightStyles; i < loadbmodel->numSuperLightStyles; i++, sls++ )
	{
		for( j = 0; j < MAX_LIGHTMAPS; j++ )
			if( sls->lightmapNum[j] != lightmaps[j] ||
				sls->lightmapStyles[j] != lightmapStyles[j] ||
				sls->vertexStyles[j] != vertexStyles[j] )
				break;
		if( j == MAX_LIGHTMAPS )
			return sls;
	}

	if( loadbmodel->numSuperLightStyles == MAX_SUPER_STYLES )
		ri.Com_Error( ERR_DROP, "R_AddSuperLightStyle: r_numSuperLightStyles == MAX_SUPER_STYLES" );
	loadbmodel->numSuperLightStyles++;

	sls->vattribs = 0;
	for( j = 0; j < MAX_LIGHTMAPS; j++ )
	{
		sls->lightmapNum[j] = lightmaps[j];
		sls->lightmapStyles[j] = lightmapStyles[j];
		sls->vertexStyles[j] = vertexStyles[j];

		if( lmRects && lmRects[j] && ( lightmaps[j] != -1 ) )
		{
			sls->stOffset[j][0] = lmRects[j]->texMatrix[0][0];
			sls->stOffset[j][1] = lmRects[j]->texMatrix[1][0];
		}
		else
		{
			sls->stOffset[j][0] = 0;
			sls->stOffset[j][0] = 0;
		}

		if( j )
		{
			// bit0 is set in the shader
			if( lightmapStyles[j] != 255 )
				sls->vattribs |= ( VATTRIB_LMCOORDS1_BIT << (j-1) );
		}
	}

	return sls;
}

/*
* R_SuperLightStylesCmp
* 
* Compare function for qsort
*/
static int R_SuperLightStylesCmp( superLightStyle_t *sls1, superLightStyle_t *sls2 )
{
	int i;

	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{	// compare lightmaps
		if( sls2->lightmapNum[i] > sls1->lightmapNum[i] )
			return 1;
		else if( sls1->lightmapNum[i] > sls2->lightmapNum[i] )
			return -1;
	}

	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{	// compare lightmap styles
		if( sls2->lightmapStyles[i] > sls1->lightmapStyles[i] )
			return 1;
		else if( sls1->lightmapStyles[i] > sls2->lightmapStyles[i] )
			return -1;
	}

	for( i = 0; i < MAX_LIGHTMAPS; i++ )
	{	// compare vertex styles
		if( sls2->vertexStyles[i] > sls1->vertexStyles[i] )
			return 1;
		else if( sls1->vertexStyles[i] > sls2->vertexStyles[i] )
			return -1;
	}

	return 0; // equal
}

/*
* R_SortSuperLightStyles
*/
void R_SortSuperLightStyles( model_t *mod )
{
	mbrushmodel_t *loadbmodel;

	assert( mod );

	loadbmodel = (( mbrushmodel_t * )mod->extradata);
	qsort( loadbmodel->superLightStyles, loadbmodel->numSuperLightStyles, 
		sizeof( superLightStyle_t ), ( int ( * )( const void *, const void * ) )R_SuperLightStylesCmp );
}
