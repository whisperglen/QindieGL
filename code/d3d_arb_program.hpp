/***************************************************************************
* Copyright (C) 2011-2016, Crystice Softworks.
* 
* This file is part of QindieGL source code.
* Please note that QindieGL is not driver, it's emulator.
* 
* QindieGL source code is free software; you can redistribute it and/or 
* modify it under the terms of the GNU General Public License as 
* published by the Free Software Foundation; either version 2 of 
* the License, or (at your option) any later version.
* 
* QindieGL source code is distributed in the hope that it will be 
* useful, but WITHOUT ANY WARRANTY; without even the implied 
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
* See the GNU General Public License for more details.
* 
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software 
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
***************************************************************************/
#ifndef QINDIEGL_D3D_ARB_PROGRAM_H
#define QINDIEGL_D3D_ARB_PROGRAM_H

#include <string>
#include <vector>
#include <map>
#include <set>

//==================================================================================
// ARB_vertex_program / ARB_fragment_program → D3D9 shader compilation
//==================================================================================
// Parses ARB assembly, generates HLSL, compiles to D3D9 VS/PS.
// At draw time, activates compiled shaders when GL_VERTEX_PROGRAM_ARB /
// GL_FRAGMENT_PROGRAM_ARB are enabled.
//==================================================================================

//----------------------------------------------------------
// ARB operand: parsed source or destination register
//----------------------------------------------------------
struct ARBOperand {
	std::string		name;			// e.g. "vertex.position", "R0", "program.env[5]"
	std::string		swizzle;		// e.g. "xyzw", "xxxx", "xy" (empty = default)
	bool			negate;			// leading '-'
	bool			absValue;		// |...|
	int				arrayIndex;		// for env/local/texcoord/attrib/matrix row, -1 if none
	int				arrayIndex2;	// secondary index (e.g. state.matrix.texture[n].row[m])
	bool			relativeIndex;	// array index uses ADDRESS, e.g. c[A0.x + 11]
	std::string		relativeRegister;
	int				relativeOffset;

	ARBOperand() : negate( false ), absValue( false ), arrayIndex( -1 ), arrayIndex2( -1 ),
		relativeIndex( false ), relativeOffset( 0 ) {}
};

//----------------------------------------------------------
// ARB instruction
//----------------------------------------------------------
struct ARBInstruction {
	std::string		opcode;			// MOV, ADD, MUL, MAD, DP3, DP4, TEX, etc.
	ARBOperand		dst;			// destination
	ARBOperand		src[3];			// up to 3 sources
	int				srcCount;		// actual number of sources
	std::string		texTarget;		// "2D", "3D", "CUBE", "RECT" (for TEX/TXP/TXB)
	int				texUnit;		// texture unit index (from src for TEX)
	bool			saturate;		// _SAT modifier

	ARBInstruction() : srcCount( 0 ), texUnit( 0 ), saturate( false ) {}
};

//----------------------------------------------------------
// PARAM binding: resolved constant/state reference
//----------------------------------------------------------
enum ARBParamType {
	PARAM_CONST,				// literal {x, y, z, w}
	PARAM_ENV,					// program.env[n]
	PARAM_LOCAL,				// program.local[n]
	PARAM_STATE_MATRIX_ROW,		// state.matrix.*.row[n]
	PARAM_STATE_MATERIAL,		// state.material.front/back.*
	PARAM_STATE_LIGHT,			// state.light[n].*
	PARAM_STATE_LIGHTMODEL,		// state.lightmodel.ambient
	PARAM_STATE_FOG,			// state.fog.*
};

struct ARBParamBinding {
	ARBParamType	type;
	float			constValue[4];	// for PARAM_CONST
	int				index;			// env/local index, light index, matrix row
	std::string		matrixName;		// "mvp", "modelview", "projection", "texture"
	int				matrixIndex;	// for texture[n], modelview[n]
	bool			inverse;		// .inverse
	bool			transpose;		// .transpose
	bool			invtrans;		// .invtrans
	std::string		stateField;		// "ambient", "diffuse", "specular", etc.

	ARBParamBinding() : type( PARAM_CONST ), index( 0 ), matrixIndex( 0 ),
		inverse( false ), transpose( false ), invtrans( false )
	{
		constValue[0] = constValue[1] = constValue[2] = constValue[3] = 0.0f;
	}
};

//----------------------------------------------------------
// Parsed ARB program
//----------------------------------------------------------
struct ARBParsedProgram {
	GLenum			target;			// GL_VERTEX_PROGRAM_ARB or GL_FRAGMENT_PROGRAM_ARB
	std::vector<std::string>					tempNames;		// TEMP t0, t1, ...
	std::map<std::string, std::string>			attribMap;		// alias → ARB attrib name
	std::map<std::string, std::string>			outputMap;		// alias → ARB output name
	std::map<std::string, std::vector<ARBParamBinding>>	paramMap;		// name → bindings (array = multiple)
	std::string									addressReg;		// ADDRESS A0 (VP only)
	std::vector<ARBInstruction>					instructions;

	// Feature tracking
	bool			positionInvariant;	// OPTION ARB_position_invariant
	bool			fogOption;			// OPTION ARB_fog_exp / exp2 / linear

	// Resource usage (discovered during parse)
	std::set<int>	usedEnvParams;		// env[n] indices accessed
	std::set<int>	usedLocalParams;	// local[n] indices accessed
	std::set<int>	usedTexUnits;		// texture units used (FP only)
	std::set<int>	usedTexCoords;		// texcoord[n] inputs used
	bool			usesColor;			// uses vertex.color / fragment.color
	bool			usesColor2;			// uses secondary color
	bool			usesNormal;			// uses vertex.normal (VP)
	bool			usesFogCoord;		// uses vertex/fragment.fogcoord
	bool			usesPosition;		// uses vertex.position (VP)
	bool			outputsColor;		// writes result.color
	bool			outputsColor2;		// writes result.color.secondary
	bool			outputsFog;			// writes result.fogcoord
	bool			outputsPointSize;	// writes result.pointsize
	bool			outputsDepth;		// writes result.depth (FP)
	bool			usesFragmentPosition;	// reads fragment.position (FP)

	// State matrix usage
	struct MatrixRef {
		std::string name;		// "mvp", "modelview", "projection", "texture"
		int			index;		// for indexed variants (texture[n], modelview[n])
		bool		inverse;
		bool		transpose;
		bool		invtrans;
	};
	std::vector<MatrixRef>	usedMatrices;

	// State param usage
	bool			usesMaterial;
	bool			usesLights;
	std::set<int>	usedLightIndices;
	bool			usesLightModelAmbient;
	bool			usesFogParams;

	// Per-unit texture target: 0=2D, 1=3D, 2=CUBE, 3=RECT
	std::map<int, std::string>	texTargetPerUnit;

	// YAE compatibility (yae_eye_distance_fog): rescale the VP's
	// result.texcoord[5] to the true eye-space distance. Set before HLSL
	// generation; see ARB_CompileProgram.
	bool			eyeDistanceTexCoord5;

	ARBParsedProgram() : target( 0 ), positionInvariant( false ), fogOption( false ),
		usesColor( false ), usesColor2( false ), usesNormal( false ),
		usesFogCoord( false ), usesPosition( false ),
		outputsColor( false ), outputsColor2( false ), outputsFog( false ), outputsPointSize( false ),
		outputsDepth( false ), usesFragmentPosition( false ),
		usesMaterial( false ), usesLights( false ), usesLightModelAmbient( false ), usesFogParams( false ),
		eyeDistanceTexCoord5( false )
	{}
};

//----------------------------------------------------------
// Compiled ARB program (D3D9 shader + constant table)
//----------------------------------------------------------
struct ARBCompiledProgram {
	GLenum					target;
	LPDIRECT3DVERTEXSHADER9	vs;				// non-NULL for vertex programs
	LPDIRECT3DPIXELSHADER9	ps;				// non-NULL for fragment programs
	LPD3DXCONSTANTTABLE		constants;		// constant table for setting uniforms
	ARBParsedProgram		parsed;			// keep parsed state for constant setup
	std::string				hlslSource;		// for debugging

	ARBCompiledProgram() : target( 0 ), vs( nullptr ), ps( nullptr ), constants( nullptr ) {}
	~ARBCompiledProgram() {
		if ( vs ) vs->Release();
		if ( ps ) ps->Release();
		if ( constants ) constants->Release();
	}
};

//----------------------------------------------------------
// Public API
//----------------------------------------------------------

// Parse ARB program source into structured representation
bool ARB_ParseProgram( const char* source, int length, GLenum target, ARBParsedProgram& out );

// Generate HLSL source from parsed ARB program
std::string ARB_GenerateHLSL( const ARBParsedProgram& parsed );

// Compile an ARB program: parse → HLSL → D3D9 shader
// Returns true on success, fills errorString on failure
bool ARB_CompileProgram( GLuint programId, GLenum target, const char* source, int length, std::string& errorString );

// Activate currently bound VP/FP shaders before a draw call
// Returns true if shaders were activated (caller should deactivate after draw)
bool ARB_ActivateShaders();

// Deactivate shaders after a draw call
void ARB_DeactivateShaders();

// Clean up all compiled shaders
void ARB_Cleanup();

// Delete a specific compiled program
void ARB_DeleteCompiledProgram( GLuint programId );

// Get the compiled program for a given ID (or NULL)
ARBCompiledProgram* ARB_GetCompiledProgram( GLuint programId );

// Number of consecutive D3D TEXCOORD semantics required to preserve the
// OpenGL texcoord[n] indices used by the active vertex and fragment programs.
// A fragment program can run with the fixed-function vertex pipeline, so its
// inputs must participate even when the corresponding texture unit is disabled.
int ARB_GetRequiredVertexTexCoordCount();

#endif //QINDIEGL_D3D_ARB_PROGRAM_H
