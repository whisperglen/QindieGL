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
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_state.hpp"
#include "d3d_utils.hpp"
#include "d3d_matrix_stack.hpp"
#include "d3d_texture.hpp"
#include "d3d_arb_program.hpp"

#include <algorithm>
#include <sstream>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <direct.h>
#include <map>

//==================================================================================
// Global compiled program cache
//==================================================================================
static std::map<GLuint, ARBCompiledProgram*> gARBCompiledPrograms;
static bool gARBShadersActive = false;
static bool gARBVertexShaderActive = false;
static bool gARBPixelShaderActive = false;

static uint32_t HashARBProgram( const char* data, size_t length )
{
	uint32_t hash = 2166136261u;
	for ( size_t i = 0; i < length; ++i ) {
		hash ^= (unsigned char)data[i];
		hash *= 16777619u;
	}
	return hash;
}

static std::string DumpARBProgramText( GLuint programId, GLenum target, uint32_t sourceHash,
	const char* kind, const char* data, size_t length )
{
	_mkdir( "QindieGL-dump" );
	_mkdir( "QindieGL-dump\\shaders" );
	char path[256];
	sprintf( path, "QindieGL-dump\\shaders\\%s_%03u_%08X.%s",
		target == GL_VERTEX_PROGRAM_ARB ? "vp" : "fp", programId, sourceHash, kind );
	FILE* file = nullptr;
	if ( fopen_s( &file, path, "wb" ) == 0 && file ) {
		fwrite( data, 1, length, file );
		fclose( file );
		return path;
	}
	return "";
}

ARBCompiledProgram* ARB_GetCompiledProgram( GLuint programId )
{
	auto it = gARBCompiledPrograms.find( programId );
	return ( it != gARBCompiledPrograms.end() ) ? it->second : nullptr;
}

void ARB_Cleanup()
{
	for ( auto& pair : gARBCompiledPrograms ) {
		delete pair.second;
	}
	gARBCompiledPrograms.clear();
	gARBShadersActive = false;
	gARBVertexShaderActive = false;
	gARBPixelShaderActive = false;
}

void ARB_DeleteCompiledProgram( GLuint programId )
{
	auto it = gARBCompiledPrograms.find( programId );
	if ( it != gARBCompiledPrograms.end() ) {
		delete it->second;
		gARBCompiledPrograms.erase( it );
	}
}

//==================================================================================
// Tokenizer helpers
//==================================================================================
static std::string TrimString( const std::string& s )
{
	size_t start = s.find_first_not_of( " \t\r\n" );
	if ( start == std::string::npos ) return "";
	size_t end = s.find_last_not_of( " \t\r\n" );
	return s.substr( start, end - start + 1 );
}

static std::vector<std::string> SplitLines( const char* src, int len )
{
	std::vector<std::string> lines;
	std::string current;
	for ( int i = 0; i < len; ++i ) {
		if ( src[i] == '\n' || src[i] == ';' ) {
			lines.push_back( current );
			current.clear();
		} else {
			current += src[i];
		}
	}
	if ( !current.empty() ) lines.push_back( current );
	return lines;
}

static std::string StripComment( const std::string& line )
{
	size_t pos = line.find( '#' );
	if ( pos != std::string::npos ) return line.substr( 0, pos );
	return line;
}

// Split a token like "R0.xyz" into name="R0" and swizzle="xyz"
// Parse negate/abs: "-R0.xyz" or "|R0.xyz|"
static ARBOperand ParseOperand( const std::string& token )
{
	ARBOperand op;
	std::string t = TrimString( token );
	if ( t.empty() ) return op;

	// Check abs value: |...|
	if ( t.front() == '|' && t.back() == '|' ) {
		op.absValue = true;
		t = t.substr( 1, t.size() - 2 );
	}
	// Check negate
	if ( !t.empty() && t[0] == '-' ) {
		op.negate = true;
		t = t.substr( 1 );
		// negate + abs: -|...|
		if ( !t.empty() && t.front() == '|' && t.back() == '|' ) {
			op.absValue = true;
			t = t.substr( 1, t.size() - 2 );
		}
	}

	// Find swizzle: last '.' that follows a name/bracket and has only xyzwrgba chars after
	// But be careful with things like "state.matrix.mvp.row[0]"
	// Strategy: split off the swizzle only if the part after the last '.' is 1-4 chars from {x,y,z,w,r,g,b,a}
	size_t lastDot = t.rfind( '.' );
	if ( lastDot != std::string::npos && lastDot + 1 < t.size() ) {
		std::string suffix = t.substr( lastDot + 1 );
		bool isSwizzle = ( suffix.size() >= 1 && suffix.size() <= 4 );
		for ( size_t i = 0; i < suffix.size() && isSwizzle; ++i ) {
			char c = suffix[i];
			if ( c != 'x' && c != 'y' && c != 'z' && c != 'w' &&
				 c != 'r' && c != 'g' && c != 'b' && c != 'a' )
				isSwizzle = false;
		}
		// Don't treat known suffixes as swizzle
		if ( suffix == "secondary" || suffix == "primary" || suffix == "front" || suffix == "back" ||
			 suffix == "ambient" || suffix == "diffuse" || suffix == "specular" || suffix == "emission" ||
			 suffix == "shininess" || suffix == "position" || suffix == "normal" || suffix == "color" ||
			 suffix == "fogcoord" || suffix == "pointsize" || suffix == "fog" || suffix == "half" ||
			 suffix == "attenuation" || suffix == "range" || suffix == "spot" || suffix == "params" ||
			 suffix == "direction" || suffix == "inverse" || suffix == "transpose" || suffix == "invtrans" )
			isSwizzle = false;
		if ( isSwizzle ) {
			op.swizzle = suffix;
			// Normalize rgba → xyzw
			for ( auto& c : op.swizzle ) {
				if ( c == 'r' ) c = 'x';
				else if ( c == 'g' ) c = 'y';
				else if ( c == 'b' ) c = 'z';
				else if ( c == 'a' ) c = 'w';
			}
			t = t.substr( 0, lastDot );
		}
	}

	// Parse array index: name[n] or name[A0.x + n].  ARB vertex programs use
	// relative constant addressing for matrix-palette skinning.  atoi() used to
	// turn the latter into index zero, which silently replaced every bone row
	// with the first literal PARAM binding.
	size_t bracket = t.find( '[' );
	if ( bracket != std::string::npos ) {
		size_t close = t.find( ']', bracket );
		if ( close != std::string::npos ) {
			std::string idxStr = TrimString( t.substr( bracket + 1, close - bracket - 1 ) );
			size_t registerEnd = 0;
			if ( !idxStr.empty() && ( isalpha( (unsigned char)idxStr[0] ) || idxStr[0] == '_' ) ) {
				while ( registerEnd < idxStr.size() &&
					( isalnum( (unsigned char)idxStr[registerEnd] ) || idxStr[registerEnd] == '_' ) )
					++registerEnd;
				op.relativeRegister = idxStr.substr( 0, registerEnd );
				if ( registerEnd + 1 < idxStr.size() && idxStr[registerEnd] == '.' &&
					idxStr[registerEnd + 1] == 'x' )
					registerEnd += 2;
				std::string offset = TrimString( idxStr.substr( registerEnd ) );
				int sign = 1;
				if ( !offset.empty() && ( offset[0] == '+' || offset[0] == '-' ) ) {
					if ( offset[0] == '-' ) sign = -1;
					offset = TrimString( offset.substr( 1 ) );
				}
				op.relativeOffset = offset.empty() ? 0 : sign * atoi( offset.c_str() );
				op.relativeIndex = !op.relativeRegister.empty();
				op.arrayIndex = op.relativeOffset;
			} else {
				op.arrayIndex = atoi( idxStr.c_str() );
			}
			std::string rest = ( close + 1 < t.size() ) ? t.substr( close + 1 ) : "";
			t = t.substr( 0, bracket ) + rest;
			// Check for second array index (e.g. after ".row" or ".texture")
			size_t bracket2 = t.find( '[' );
			if ( bracket2 != std::string::npos ) {
				size_t close2 = t.find( ']', bracket2 );
				if ( close2 != std::string::npos ) {
					std::string idx2Str = t.substr( bracket2 + 1, close2 - bracket2 - 1 );
					op.arrayIndex2 = atoi( idx2Str.c_str() );
					t = t.substr( 0, bracket2 ) + ( ( close2 + 1 < t.size() ) ? t.substr( close2 + 1 ) : "" );
				}
			}
		}
	}

	op.name = t;
	return op;
}

// Split comma-separated operands, respecting { } and | | brackets
static std::vector<std::string> SplitOperands( const std::string& s )
{
	std::vector<std::string> result;
	int braceDepth = 0;
	int pipeCount = 0;
	std::string current;
	for ( size_t i = 0; i < s.size(); ++i ) {
		char c = s[i];
		if ( c == '{' ) braceDepth++;
		else if ( c == '}' ) braceDepth--;
		else if ( c == '|' ) pipeCount++;
		if ( c == ',' && braceDepth == 0 && ( pipeCount % 2 == 0 ) ) {
			result.push_back( TrimString( current ) );
			current.clear();
		} else {
			current += c;
		}
	}
	if ( !current.empty() ) result.push_back( TrimString( current ) );
	return result;
}

// Parse a constant literal: {x, y, z, w} → float[4]
static bool ParseConstant( const std::string& token, float out[4] )
{
	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f; // w defaults to 1.0 per ARB spec
	size_t open = token.find( '{' );
	size_t close = token.find( '}' );
	if ( open == std::string::npos || close == std::string::npos ) return false;
	std::string inner = token.substr( open + 1, close - open - 1 );
	std::vector<std::string> parts = SplitOperands( inner );
	for ( size_t i = 0; i < parts.size() && i < 4; ++i ) {
		out[i] = (float)atof( parts[i].c_str() );
	}
	// Replicate single value to all components
	if ( parts.size() == 1 ) { out[1] = out[2] = out[3] = out[0]; }
	return true;
}

//==================================================================================
// Parameter binding parser
//==================================================================================
static ARBParamBinding ParseParamBinding( const std::string& binding )
{
	ARBParamBinding pb;
	std::string b = TrimString( binding );

	// program.env[n]
	if ( b.find( "program.env" ) == 0 ) {
		pb.type = PARAM_ENV;
		size_t br = b.find( '[' );
		if ( br != std::string::npos ) {
			pb.index = atoi( b.substr( br + 1 ).c_str() );
		}
		return pb;
	}
	// program.local[n]
	if ( b.find( "program.local" ) == 0 ) {
		pb.type = PARAM_LOCAL;
		size_t br = b.find( '[' );
		if ( br != std::string::npos ) {
			pb.index = atoi( b.substr( br + 1 ).c_str() );
		}
		return pb;
	}
	// state.matrix.*
	if ( b.find( "state.matrix." ) == 0 ) {
		pb.type = PARAM_STATE_MATRIX_ROW;
		std::string rest = b.substr( 13 ); // after "state.matrix."
		// Parse matrix name and modifiers
		if ( rest.find( "mvp" ) == 0 ) {
			pb.matrixName = "mvp";
			rest = rest.substr( 3 );
		} else if ( rest.find( "modelview" ) == 0 ) {
			pb.matrixName = "modelview";
			rest = rest.substr( 9 );
			if ( !rest.empty() && rest[0] == '[' ) {
				pb.matrixIndex = atoi( rest.substr( 1 ).c_str() );
				size_t cl = rest.find( ']' );
				if ( cl != std::string::npos ) rest = rest.substr( cl + 1 );
			}
		} else if ( rest.find( "projection" ) == 0 ) {
			pb.matrixName = "projection";
			rest = rest.substr( 10 );
		} else if ( rest.find( "texture" ) == 0 ) {
			pb.matrixName = "texture";
			rest = rest.substr( 7 );
			if ( !rest.empty() && rest[0] == '[' ) {
				pb.matrixIndex = atoi( rest.substr( 1 ).c_str() );
				size_t cl = rest.find( ']' );
				if ( cl != std::string::npos ) rest = rest.substr( cl + 1 );
			}
		} else if ( rest.find( "program" ) == 0 ) {
			pb.matrixName = "program";
			rest = rest.substr( 7 );
			if ( !rest.empty() && rest[0] == '[' ) {
				pb.matrixIndex = atoi( rest.substr( 1 ).c_str() );
				size_t cl = rest.find( ']' );
				if ( cl != std::string::npos ) rest = rest.substr( cl + 1 );
			}
		}
		// Modifiers: .inverse, .transpose, .invtrans
		if ( rest.find( ".invtrans" ) != std::string::npos || rest.find( ".inversetranspose" ) != std::string::npos )
			pb.invtrans = true;
		else if ( rest.find( ".inverse" ) != std::string::npos )
			pb.inverse = true;
		else if ( rest.find( ".transpose" ) != std::string::npos )
			pb.transpose = true;
		// Row index from .row[n]
		size_t rowpos = rest.find( ".row[" );
		if ( rowpos != std::string::npos ) {
			pb.index = atoi( rest.substr( rowpos + 5 ).c_str() );
		} else {
			pb.index = -1; // whole matrix (will expand to 4 rows)
		}
		return pb;
	}
	// state.material.*
	if ( b.find( "state.material." ) == 0 || b.find( "state.material[" ) == 0 ) {
		pb.type = PARAM_STATE_MATERIAL;
		pb.stateField = b;
		return pb;
	}
	// state.light[n].*
	if ( b.find( "state.light[" ) == 0 || b.find( "state.light." ) == 0 ) {
		pb.type = PARAM_STATE_LIGHT;
		size_t br = b.find( '[' );
		if ( br != std::string::npos ) pb.index = atoi( b.substr( br + 1 ).c_str() );
		pb.stateField = b;
		return pb;
	}
	// state.lightmodel.ambient
	if ( b.find( "state.lightmodel" ) == 0 ) {
		pb.type = PARAM_STATE_LIGHTMODEL;
		pb.stateField = b;
		return pb;
	}
	// state.fog.*
	if ( b.find( "state.fog" ) == 0 ) {
		pb.type = PARAM_STATE_FOG;
		pb.stateField = b;
		return pb;
	}
	// Literal constant {x, y, z, w}
	if ( b.find( '{' ) != std::string::npos ) {
		pb.type = PARAM_CONST;
		ParseConstant( b, pb.constValue );
		return pb;
	}
	// Numeric literal (single value)
	if ( !b.empty() && ( isdigit( b[0] ) || b[0] == '-' || b[0] == '+' || b[0] == '.' ) ) {
		pb.type = PARAM_CONST;
		pb.constValue[0] = pb.constValue[1] = pb.constValue[2] = pb.constValue[3] = (float)atof( b.c_str() );
		return pb;
	}

	// Unknown — treat as constant 0
	pb.type = PARAM_CONST;
	return pb;
}

// Expand consecutive ARB bindings used by PARAM arrays.
static void AppendExpandedParamBinding( const std::string& binding, std::vector<ARBParamBinding>& bindings )
{
	std::string b = TrimString( binding );
	if ( b.empty() ) return;

	size_t range = b.find( ".." );
	if ( range != std::string::npos ) {
		size_t open = b.rfind( '[', range );
		size_t close = b.find( ']', range );
		if ( open != std::string::npos && close != std::string::npos ) {
			int first = atoi( b.substr( open + 1, range - open - 1 ).c_str() );
			int last = atoi( b.substr( range + 2, close - range - 2 ).c_str() );
			if ( last >= first ) {
				std::string prefix = b.substr( 0, open );
				std::string suffix = b.substr( close + 1 );
				for ( int index = first; index <= last; ++index ) {
					char expanded[256];
					sprintf( expanded, "%s[%d]%s", prefix.c_str(), index, suffix.c_str() );
					bindings.push_back( ParseParamBinding( expanded ) );
				}
				return;
			}
		}
	}

	ARBParamBinding parsed = ParseParamBinding( b );
	if ( parsed.type == PARAM_STATE_MATRIX_ROW && parsed.index < 0 ) {
		for ( int row = 0; row < 4; ++row ) {
			ARBParamBinding rowBinding = parsed;
			rowBinding.index = row;
			bindings.push_back( rowBinding );
		}
		return;
	}
	bindings.push_back( parsed );
}

//==================================================================================
// PARSER: ARB assembly → ARBParsedProgram
//==================================================================================
bool ARB_ParseProgram( const char* source, int length, GLenum target, ARBParsedProgram& out )
{
	out = ARBParsedProgram();
	out.target = target;

	std::vector<std::string> lines = SplitLines( source, length );

	bool foundHeader = false;
	for ( size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx ) {
		std::string line = TrimString( StripComment( lines[lineIdx] ) );
		if ( line.empty() ) continue;

		// Program header: "!!ARBvp1.0" or "!!ARBfp1.0"
		if ( line.find( "!!ARB" ) == 0 ) {
			foundHeader = true;
			continue;
		}

		// END
		if ( line == "END" || line == "END;" ) break;

		// OPTION
		if ( line.find( "OPTION" ) == 0 ) {
			std::string rest = TrimString( line.substr( 6 ) );
			// Remove trailing semicolon
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			rest = TrimString( rest );
			if ( rest.find( "ARB_position_invariant" ) != std::string::npos )
				out.positionInvariant = true;
			if ( rest.find( "ARB_fog" ) != std::string::npos )
				out.fogOption = true;
			continue;
		}

		// TEMP declaration
		if ( line.find( "TEMP" ) == 0 && ( line.size() > 4 && ( line[4] == ' ' || line[4] == '\t' ) ) ) {
			std::string rest = TrimString( line.substr( 4 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			std::vector<std::string> temps = SplitOperands( rest );
			for ( auto& t : temps ) {
				std::string name = TrimString( t );
				if ( !name.empty() ) out.tempNames.push_back( name );
			}
			continue;
		}

		// ADDRESS declaration (VP only)
		if ( line.find( "ADDRESS" ) == 0 && ( line.size() > 7 && ( line[7] == ' ' || line[7] == '\t' ) ) ) {
			std::string rest = TrimString( line.substr( 7 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			out.addressReg = TrimString( rest );
			continue;
		}

		// ATTRIB declaration: ATTRIB name = attrib_binding
		if ( line.find( "ATTRIB" ) == 0 && ( line.size() > 6 && ( line[6] == ' ' || line[6] == '\t' ) ) ) {
			std::string rest = TrimString( line.substr( 6 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			size_t eq = rest.find( '=' );
			if ( eq != std::string::npos ) {
				std::string alias = TrimString( rest.substr( 0, eq ) );
				std::string binding = TrimString( rest.substr( eq + 1 ) );
				out.attribMap[alias] = binding;
			}
			continue;
		}

		// OUTPUT declaration: OUTPUT name = output_binding
		if ( line.find( "OUTPUT" ) == 0 && ( line.size() > 6 && ( line[6] == ' ' || line[6] == '\t' ) ) ) {
			std::string rest = TrimString( line.substr( 6 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			size_t eq = rest.find( '=' );
			if ( eq != std::string::npos ) {
				std::string alias = TrimString( rest.substr( 0, eq ) );
				std::string binding = TrimString( rest.substr( eq + 1 ) );
				out.outputMap[alias] = binding;
			}
			continue;
		}

		// ALIAS declaration: ALIAS name = name
		if ( line.find( "ALIAS" ) == 0 && ( line.size() > 5 && ( line[5] == ' ' || line[5] == '\t' ) ) ) {
			// Treat like ATTRIB for simplicity
			std::string rest = TrimString( line.substr( 5 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			size_t eq = rest.find( '=' );
			if ( eq != std::string::npos ) {
				std::string alias = TrimString( rest.substr( 0, eq ) );
				std::string binding = TrimString( rest.substr( eq + 1 ) );
				out.attribMap[alias] = binding;
			}
			continue;
		}

		// PARAM declaration: PARAM name = binding or PARAM name[n] = { binding, ... }
		if ( line.find( "PARAM" ) == 0 && ( line.size() > 5 && ( line[5] == ' ' || line[5] == '\t' ) ) ) {
			// PARAM initializers can span physical lines. SplitLines treats ';' as
			// an instruction delimiter, so join continuations until braces balance.
			int braceDepth = 0;
			for ( char c : line ) {
				if ( c == '{' ) ++braceDepth;
				else if ( c == '}' ) --braceDepth;
			}
			while ( braceDepth > 0 && lineIdx + 1 < lines.size() ) {
				std::string continuation = TrimString( StripComment( lines[++lineIdx] ) );
				if ( !continuation.empty() ) line += " " + continuation;
				for ( char c : continuation ) {
					if ( c == '{' ) ++braceDepth;
					else if ( c == '}' ) --braceDepth;
				}
			}

			std::string rest = TrimString( line.substr( 5 ) );
			if ( !rest.empty() && rest.back() == ';' ) rest.pop_back();
			size_t eq = rest.find( '=' );
			if ( eq != std::string::npos ) {
				std::string namepart = TrimString( rest.substr( 0, eq ) );
				std::string bindingPart = TrimString( rest.substr( eq + 1 ) );
				
				// Check for array syntax: name[n]
				std::string paramName = namepart;
				int arraySize = -1;
				size_t br = namepart.find( '[' );
				if ( br != std::string::npos ) {
					paramName = TrimString( namepart.substr( 0, br ) );
					size_t cl = namepart.find( ']', br );
					if ( cl != std::string::npos ) {
						std::string szStr = namepart.substr( br + 1, cl - br - 1 );
						if ( !szStr.empty() ) arraySize = atoi( szStr.c_str() );
					}
				}

				std::vector<ARBParamBinding> bindings;
				if ( arraySize > 0 && bindingPart.size() >= 2 &&
					 bindingPart.front() == '{' && bindingPart.back() == '}' ) {
					std::string initializer = bindingPart.substr( 1, bindingPart.size() - 2 );
					std::vector<std::string> entries = SplitOperands( initializer );
					for ( const std::string& entry : entries )
						AppendExpandedParamBinding( entry, bindings );
				} else {
					AppendExpandedParamBinding( bindingPart, bindings );
				}
				out.paramMap[paramName] = bindings;
				if ( arraySize > 0 && arraySize != (int)bindings.size() ) {
					logPrintf( "WARNING: ARB PARAM %s declares %d bindings but parsed %u\n",
						paramName.c_str(), arraySize, (unsigned int)bindings.size() );
				}

#if 0 // Replaced by the generic array/range expansion above.
				// Check if it's a matrix range: state.matrix.*.row[0..3]
				if ( bindingPart.find( ".row[" ) != std::string::npos && bindingPart.find( ".." ) != std::string::npos ) {
					// e.g. state.matrix.mvp.row[0..3] → 4 row bindings
					std::string base = bindingPart.substr( 0, bindingPart.find( ".row[" ) );
					size_t dotdot = bindingPart.find( ".." );
					size_t rowBr = bindingPart.find( ".row[" );
					int startRow = atoi( bindingPart.substr( rowBr + 5 ).c_str() );
					int endRow = atoi( bindingPart.substr( dotdot + 2 ).c_str() );
					std::vector<ARBParamBinding> bindings;
					for ( int r = startRow; r <= endRow; ++r ) {
						char rowBuf[64];
						sprintf( rowBuf, "%s.row[%d]", base.c_str(), r );
						bindings.push_back( ParseParamBinding( rowBuf ) );
					}
					out.paramMap[paramName] = bindings;
				} else if ( bindingPart.find( '{' ) != std::string::npos && arraySize > 0 ) {
					// Array of constant values: { {1,0,0,0}, {0,1,0,0}, ... }
					// Or program.env[0..n]
					std::vector<ARBParamBinding> bindings;
					bindings.push_back( ParseParamBinding( bindingPart ) );
					out.paramMap[paramName] = bindings;
				} else if ( bindingPart.find( ".." ) != std::string::npos ) {
					// Range: program.env[0..7]
					size_t oBr = bindingPart.find( '[' );
					size_t dd = bindingPart.find( ".." );
					size_t cBr = bindingPart.find( ']' );
					if ( oBr != std::string::npos && dd != std::string::npos && cBr != std::string::npos ) {
						int startIdx = atoi( bindingPart.substr( oBr + 1 ).c_str() );
						int endIdx = atoi( bindingPart.substr( dd + 2 ).c_str() );
						std::string prefix = bindingPart.substr( 0, oBr );
						std::vector<ARBParamBinding> bindings;
						for ( int idx = startIdx; idx <= endIdx; ++idx ) {
							char buf[128];
							sprintf( buf, "%s[%d]", prefix.c_str(), idx );
							bindings.push_back( ParseParamBinding( buf ) );
						}
						out.paramMap[paramName] = bindings;
					}
				} else {
					// Single binding
					std::vector<ARBParamBinding> bindings;
					bindings.push_back( ParseParamBinding( bindingPart ) );
					out.paramMap[paramName] = bindings;
				}
#endif
			}
			continue;
		}

		// Must be an instruction
		// Remove trailing semicolon
		if ( !line.empty() && line.back() == ';' ) line.pop_back();
		line = TrimString( line );
		if ( line.empty() ) continue;

		// Parse: OPCODE[_SAT] dst, src1[, src2[, src3]][, texTarget]
		ARBInstruction inst;
		
		// Split into opcode and operands
		size_t spacePos = line.find_first_of( " \t" );
		if ( spacePos == std::string::npos ) {
			// Opcode only (e.g. END, which we already handle)
			inst.opcode = line;
			if ( inst.opcode != "END" ) {
				out.instructions.push_back( inst );
			}
			continue;
		}

		inst.opcode = line.substr( 0, spacePos );
		std::string operandStr = TrimString( line.substr( spacePos ) );

		// Check _SAT suffix
		if ( inst.opcode.size() > 4 && inst.opcode.substr( inst.opcode.size() - 4 ) == "_SAT" ) {
			inst.saturate = true;
			inst.opcode = inst.opcode.substr( 0, inst.opcode.size() - 4 );
		}

		// Split operands
		std::vector<std::string> ops = SplitOperands( operandStr );
		if ( ops.empty() ) continue;

		// First operand is destination
		inst.dst = ParseOperand( ops[0] );

		// Source operands
		int srcStart = 1;
		inst.srcCount = 0;
		for ( size_t i = srcStart; i < ops.size() && inst.srcCount < 3; ++i ) {
			std::string& opStr = ops[i];
			// Check if this is a texture target (for TEX/TXP/TXB/TXL)
			if ( opStr == "1D" || opStr == "2D" || opStr == "3D" || opStr == "CUBE" || opStr == "RECT" ||
				 opStr == "SHADOW1D" || opStr == "SHADOW2D" || opStr == "SHADOWRECT" ) {
				inst.texTarget = opStr;
				continue;
			}
			// Check if previous src was "texture[n]" for TEX instructions
			inst.src[inst.srcCount] = ParseOperand( opStr );
			inst.srcCount++;
		}

		// For TEX/TXP/TXB: extract texture unit from src (usually texture[n])
		if ( inst.opcode == "TEX" || inst.opcode == "TXP" || inst.opcode == "TXB" || inst.opcode == "TXL" ) {
			// The texture unit is typically the second source: texture[n]
			if ( inst.srcCount >= 2 ) {
				ARBOperand& texOp = inst.src[1];
				if ( texOp.name.find( "texture" ) != std::string::npos ) {
					inst.texUnit = texOp.arrayIndex >= 0 ? texOp.arrayIndex : 0;
				}
			}
		}

		out.instructions.push_back( inst );
	}

	// Analyze resource usage
	auto trackOperand = [&]( const ARBOperand& op, bool isDst ) {
		std::string name = op.name;

		// Resolve aliases
		auto ait = out.attribMap.find( name );
		if ( ait != out.attribMap.end() ) name = ait->second;
		auto oit = out.outputMap.find( name );
		if ( oit != out.outputMap.end() ) name = oit->second;

		// Track inputs
		if ( name.find( "vertex.position" ) != std::string::npos ||
			 (name.find( "vertex.attrib" ) == 0 && (op.arrayIndex == 0 || name.find("[0]") != std::string::npos)) )
			out.usesPosition = true;
		if ( name.find( "vertex.color" ) != std::string::npos || name.find( "fragment.color" ) != std::string::npos ) {
			if ( name.find( "secondary" ) != std::string::npos )
				out.usesColor2 = true;
			else
				out.usesColor = true;
		}
		if ( name.find( "vertex.normal" ) != std::string::npos )
			out.usesNormal = true;
		if ( name.find( "vertex.fogcoord" ) != std::string::npos || name.find( "fragment.fogcoord" ) != std::string::npos )
			out.usesFogCoord = true;
		if ( name.find( "vertex.texcoord" ) != std::string::npos || name.find( "fragment.texcoord" ) != std::string::npos ) {
			int tc = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
			out.usedTexCoords.insert( tc );
		}
		if ( name.find( "fragment.position" ) != std::string::npos )
			out.usesFragmentPosition = true;

		// Track generic vertex.attrib[n] → standard attrib inputs
		if ( name.find( "vertex.attrib" ) == 0 ) {
			int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
			if ( idx == 2 ) out.usesNormal = true;
			if ( idx == 3 ) out.usesColor = true;
			if ( idx == 4 ) out.usesColor2 = true;
			if ( idx == 5 ) out.usesFogCoord = true;
			if ( idx >= 8 && idx <= 15 ) out.usedTexCoords.insert( idx - 8 );
		}

		// Track outputs
		if ( isDst ) {
			if ( name.find( "result.color" ) != std::string::npos ) {
				if ( name.find( "secondary" ) != std::string::npos )
					out.outputsColor2 = true;
				else
					out.outputsColor = true;
			}
			if ( name.find( "result.fogcoord" ) != std::string::npos )
				out.outputsFog = true;
			if ( name.find( "result.pointsize" ) != std::string::npos )
				out.outputsPointSize = true;
			if ( name.find( "result.depth" ) != std::string::npos )
				out.outputsDepth = true;
		}

		// Track params
		if ( name.find( "program.env" ) != std::string::npos && op.arrayIndex >= 0 )
			out.usedEnvParams.insert( op.arrayIndex );
		if ( name.find( "program.local" ) != std::string::npos && op.arrayIndex >= 0 )
			out.usedLocalParams.insert( op.arrayIndex );

		// Track state
		if ( name.find( "state.material" ) != std::string::npos )
			out.usesMaterial = true;
		if ( name.find( "state.light" ) != std::string::npos && name.find( "lightmodel" ) == std::string::npos ) {
			out.usesLights = true;
			if ( op.arrayIndex >= 0 ) out.usedLightIndices.insert( op.arrayIndex );
		}
		if ( name.find( "state.lightmodel" ) != std::string::npos )
			out.usesLightModelAmbient = true;
		if ( name.find( "state.fog" ) != std::string::npos )
			out.usesFogParams = true;
	};

	// Also scan param declarations for their bindings
	for ( auto& paramPair : out.paramMap ) {
		for ( auto& pb : paramPair.second ) {
			if ( pb.type == PARAM_ENV ) out.usedEnvParams.insert( pb.index );
			if ( pb.type == PARAM_LOCAL ) out.usedLocalParams.insert( pb.index );
			if ( pb.type == PARAM_STATE_MATRIX_ROW ) {
				// Register matrix usage
				bool found = false;
				for ( auto& mr : out.usedMatrices ) {
					if ( mr.name == pb.matrixName && mr.index == pb.matrixIndex &&
						 mr.inverse == pb.inverse && mr.transpose == pb.transpose && mr.invtrans == pb.invtrans ) {
						found = true;
						break;
					}
				}
				if ( !found ) {
					ARBParsedProgram::MatrixRef mr;
					mr.name = pb.matrixName;
					mr.index = pb.matrixIndex;
					mr.inverse = pb.inverse;
					mr.transpose = pb.transpose;
					mr.invtrans = pb.invtrans;
					out.usedMatrices.push_back( mr );
				}
			}
			if ( pb.type == PARAM_STATE_MATERIAL ) out.usesMaterial = true;
			if ( pb.type == PARAM_STATE_LIGHT ) {
				out.usesLights = true;
				out.usedLightIndices.insert( pb.index );
			}
			if ( pb.type == PARAM_STATE_LIGHTMODEL ) out.usesLightModelAmbient = true;
			if ( pb.type == PARAM_STATE_FOG ) out.usesFogParams = true;
		}
	}

	for ( auto& inst : out.instructions ) {
		trackOperand( inst.dst, true );
		for ( int i = 0; i < inst.srcCount; ++i ) {
			trackOperand( inst.src[i], false );
		}
		if ( inst.opcode == "TEX" || inst.opcode == "TXP" || inst.opcode == "TXB" || inst.opcode == "TXL" ) {
			out.usedTexUnits.insert( inst.texUnit );
			// Track texture target per unit
			if ( !inst.texTarget.empty() ) {
				out.texTargetPerUnit[inst.texUnit] = inst.texTarget;
			}
		}
	}

	// Position invariant implies position transform using MVP
	if ( out.positionInvariant && target == GL_VERTEX_PROGRAM_ARB ) {
		out.usesPosition = true;
		ARBParsedProgram::MatrixRef mr;
		mr.name = "mvp";
		mr.index = 0;
		mr.inverse = false;
		mr.transpose = false;
		mr.invtrans = false;
		bool found = false;
		for ( auto& m : out.usedMatrices ) {
			if ( m.name == "mvp" ) { found = true; break; }
		}
		if ( !found ) out.usedMatrices.push_back( mr );
	}

	return true;
}

//==================================================================================
// HLSL GENERATOR
//==================================================================================

// Generate a unique HLSL variable name for a matrix constant
static std::string MatrixConstName( const ARBParsedProgram::MatrixRef& mr )
{
	std::string n = "_mtx_" + mr.name;
	if ( mr.name == "modelview" || mr.name == "texture" || mr.name == "program" )
		n += "_" + std::to_string( mr.index );
	if ( mr.inverse ) n += "_inv";
	if ( mr.transpose ) n += "_trans";
	if ( mr.invtrans ) n += "_invtrans";
	return n;
}

// Resolve an operand name to its HLSL representation
static std::string ResolveOperandHLSL( const ARBOperand& op, const ARBParsedProgram& p, bool isDst )
{
	std::string name = op.name;

	// Resolve ATTRIB and OUTPUT aliases
	auto ait = p.attribMap.find( name );
	if ( ait != p.attribMap.end() ) name = ait->second;
	auto oit = p.outputMap.find( name );
	if ( oit != p.outputMap.end() ) name = oit->second;

	// Check PARAM declarations
	auto pit = p.paramMap.find( op.name );
	if ( pit != p.paramMap.end() ) {
		const auto& bindings = pit->second;
		if ( op.relativeIndex ) {
			int offset = op.relativeOffset;
			if ( offset >= 0 && offset < (int)bindings.size() ) {
				const ARBParamBinding& pb = bindings[offset];
				const char* arrayName = nullptr;
				if ( pb.type == PARAM_ENV ) arrayName = "_env";
				else if ( pb.type == PARAM_LOCAL ) arrayName = "_local";
				if ( arrayName ) {
					char buf[96];
					sprintf( buf, "%s[%s + %d]", arrayName,
						op.relativeRegister.c_str(), pb.index );
					return buf;
				}
			}
		}
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		if ( idx < (int)bindings.size() ) {
			const ARBParamBinding& pb = bindings[idx];
			switch ( pb.type ) {
			case PARAM_CONST: {
				char buf[128];
				sprintf( buf, "float4(%.8g, %.8g, %.8g, %.8g)", pb.constValue[0], pb.constValue[1], pb.constValue[2], pb.constValue[3] );
				return buf;
			}
			case PARAM_ENV: {
				char buf[32];
				sprintf( buf, "_env[%d]", pb.index );
				return buf;
			}
			case PARAM_LOCAL: {
				char buf[32];
				sprintf( buf, "_local[%d]", pb.index );
				return buf;
			}
			case PARAM_STATE_MATRIX_ROW: {
				// Find the matrix constant
				ARBParsedProgram::MatrixRef mr;
				mr.name = pb.matrixName;
				mr.index = pb.matrixIndex;
				mr.inverse = pb.inverse;
				mr.transpose = pb.transpose;
				mr.invtrans = pb.invtrans;
				std::string mtxName = MatrixConstName( mr );
				if ( pb.index >= 0 && pb.index < 4 ) {
					char buf[64];
					sprintf( buf, "%s[%d]", mtxName.c_str(), pb.index );
					return buf;
				}
				return mtxName + "[0]";
			}
			case PARAM_STATE_MATERIAL:
			case PARAM_STATE_LIGHT:
			case PARAM_STATE_LIGHTMODEL:
			case PARAM_STATE_FOG: {
				// For state params, use a named constant that we'll set at draw time
				// Naming must match the declaration and SetProgramConstants:
				// single-binding -> "_sp_<paramName>", multi-binding -> "_sp_<paramName>_<index>"
				std::string cname = "_sp_" + op.name;
				if ( bindings.size() > 1 ) cname += "_" + std::to_string( idx );
				// Replace dots and brackets with underscores
				for ( auto& c : cname ) {
					if ( c == '.' || c == '[' || c == ']' ) c = '_';
				}
				return cname;
			}
			}
		} else if ( bindings.size() > 0 && op.arrayIndex >= 0 ) {
			// Array param accessed with index - use first binding's pattern
			const ARBParamBinding& pb = bindings[0];
			if ( pb.type == PARAM_ENV ) {
				char buf[32];
				sprintf( buf, "_env[%d]", pb.index + op.arrayIndex );
				return buf;
			} else if ( pb.type == PARAM_LOCAL ) {
				char buf[32];
				sprintf( buf, "_local[%d]", pb.index + op.arrayIndex );
				return buf;
			} else if ( pb.type == PARAM_STATE_MATRIX_ROW ) {
				ARBParsedProgram::MatrixRef mr;
				mr.name = pb.matrixName;
				mr.index = pb.matrixIndex;
				mr.inverse = pb.inverse;
				mr.transpose = pb.transpose;
				mr.invtrans = pb.invtrans;
				std::string mtxName = MatrixConstName( mr );
				int rowIdx = op.arrayIndex;
				if ( rowIdx >= 0 && rowIdx < 4 ) {
					char buf[64];
					sprintf( buf, "%s[%d]", mtxName.c_str(), rowIdx );
					return buf;
				}
			}
		}
	}

	bool isVP = ( p.target == GL_VERTEX_PROGRAM_ARB );

	// Direct program.env[n] / program.local[n] reference
	if ( name.find( "program.env" ) == 0 ) {
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		char buf[32]; sprintf( buf, "_env[%d]", idx );
		return buf;
	}
	if ( name.find( "program.local" ) == 0 ) {
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		char buf[32]; sprintf( buf, "_local[%d]", idx );
		return buf;
	}

	// Vertex program inputs
	if ( name == "vertex.position" ) return "v.position";
	if ( name == "vertex.normal" ) return "float4(v.normal, 0.0)";
	if ( name == "vertex.color" ) return "v.color";
	if ( name == "vertex.color.primary" ) return "v.color";
	if ( name == "vertex.color.secondary" ) return "v.color2";
	if ( name == "vertex.fogcoord" ) return "v.fogcoord";
	if ( name == "vertex.texcoord" ) {
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		char buf[32]; sprintf( buf, "v.texcoord%d", idx );
		return buf;
	}
	if ( name == "vertex.attrib" ) {
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		// Map standard attrib indices to named inputs
		switch ( idx ) {
		case 0: return "v.position";
		case 2: return "float4(v.normal, 0.0)";
		case 3: return "v.color";
		case 4: return "v.color2";
		case 5: return "v.fogcoord";
		default:
			if ( idx >= 8 && idx <= 15 ) {
				char buf[32]; sprintf( buf, "v.texcoord%d", idx - 8 );
				return buf;
			}
			return "float4(0,0,0,1)";
		}
	}

	// Fragment program inputs
	if ( name == "fragment.color" ) return "f.color";
	if ( name == "fragment.color.primary" ) return "f.color";
	if ( name == "fragment.color.secondary" ) return "f.color2";
	if ( name == "fragment.texcoord" ) {
		int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
		char buf[32]; sprintf( buf, "f.texcoord%d", idx );
		return buf;
	}
	if ( name == "fragment.position" ) return "f.position";
	if ( name == "fragment.fogcoord" ) return "f.fogcoord";

	// VP outputs
	if ( isDst ) {
		if ( name == "result.position" ) return "o.position";
		if ( name == "result.color" ) {
			if ( !isVP ) return "o.color"; // FP output
			return "o.color"; // VP output
		}
		if ( name == "result.color.primary" ) return "o.color";
		if ( name == "result.color.secondary" ) return "o.color2";
		if ( name == "result.texcoord" ) {
			int idx = ( op.arrayIndex >= 0 ) ? op.arrayIndex : 0;
			char buf[32]; sprintf( buf, "o.texcoord%d", idx );
			return buf;
		}
		if ( name == "result.fogcoord" ) return "o.fogcoord";
		if ( name == "result.pointsize" ) return "o.pointsize";
		if ( name == "result.depth" ) return "o.depth";
	}

	// State matrix direct reference (not through PARAM)
	if ( name.find( "state.matrix." ) == 0 ) {
		// Parse it as a param binding and generate the same constant name
		ARBParamBinding pb = ParseParamBinding( name );
		if ( pb.type == PARAM_STATE_MATRIX_ROW ) {
			ARBParsedProgram::MatrixRef mr;
			mr.name = pb.matrixName;
			mr.index = pb.matrixIndex;
			mr.inverse = pb.inverse;
			mr.transpose = pb.transpose;
			mr.invtrans = pb.invtrans;
			std::string mtxName = MatrixConstName( mr );
			int row = ( op.arrayIndex >= 0 ) ? op.arrayIndex : ( pb.index >= 0 ? pb.index : 0 );
			char buf[64];
			sprintf( buf, "%s[%d]", mtxName.c_str(), row );
			return buf;
		}
	}

	// TEMP register or address register — use as-is (it's a local variable name)
	return name;
}

// Format a full operand expression with swizzle, negate, abs
static std::string FormatOperandHLSL( const ARBOperand& op, const ARBParsedProgram& p, bool isDst )
{
	std::string base = ResolveOperandHLSL( op, p, isDst );
	
	// Apply swizzle
	if ( !op.swizzle.empty() ) {
		base += "." + op.swizzle;
	}

	// Apply abs
	if ( op.absValue ) {
		base = "abs(" + base + ")";
	}

	// Apply negate
	if ( op.negate ) {
		base = "-(" + base + ")";
	}

	return base;
}

// HLSL truncates vectors from x, while an ARB destination mask selects the
// corresponding source components. Preserve that rule for component-wise ops.
static std::string FormatOperandForMaskHLSL( const ARBOperand& op, const ARBParsedProgram& p,
	const std::string& writeMask )
{
	ARBOperand masked = op;
	if ( !writeMask.empty() && writeMask != "xyzw" ) {
		if ( masked.swizzle.empty() ) {
			masked.swizzle = writeMask;
		} else if ( masked.swizzle.size() > 1 ) {
			std::string selected;
			for ( char component : writeMask ) {
				int index = component == 'x' ? 0 : component == 'y' ? 1 : component == 'z' ? 2 : 3;
				selected += masked.swizzle[index < (int)masked.swizzle.size() ? index : (int)masked.swizzle.size() - 1];
			}
			masked.swizzle = selected;
		}
	}
	return FormatOperandHLSL( masked, p, false );
}

static std::string ApplyResultMaskHLSL( const std::string& expression, const std::string& writeMask )
{
	if ( writeMask.empty() || writeMask == "xyzw" ) return expression;
	return "(" + expression + ")." + writeMask;
}

// Format a write-masked destination assignment
static std::string FormatDestHLSL( const ARBOperand& op, const ARBParsedProgram& p )
{
	std::string base = ResolveOperandHLSL( op, p, true );
	if ( !op.swizzle.empty() && op.swizzle != "xyzw" ) {
		base += "." + op.swizzle;
	}
	return base;
}

// Wrap expression with saturate if needed
static std::string MaybeSaturate( const std::string& expr, bool sat )
{
	return sat ? ( "saturate(" + expr + ")" ) : expr;
}

// Texture coordinate sets written by a vertex program (result.texcoord[n]).
static std::set<int> CollectOutputTexCoords( const ARBParsedProgram& p )
{
	std::set<int> outTexCoords;
	for ( auto& inst : p.instructions ) {
		std::string dstName = inst.dst.name;
		auto oit = p.outputMap.find( dstName );
		if ( oit != p.outputMap.end() ) dstName = oit->second;
		if ( dstName.find( "result.texcoord" ) != std::string::npos ) {
			int idx = inst.dst.arrayIndex >= 0 ? inst.dst.arrayIndex : 0;
			outTexCoords.insert( idx );
		}
	}
	return outTexCoords;
}

std::string ARB_GenerateHLSL( const ARBParsedProgram& p )
{
	std::ostringstream hlsl;
	bool isVP = ( p.target == GL_VERTEX_PROGRAM_ARB );

	//--------------------------------------------------------------
	// Constant declarations
	//--------------------------------------------------------------
	// env/local params
	if ( !p.usedEnvParams.empty() ) {
		int maxEnv = *p.usedEnvParams.rbegin() + 1;
		hlsl << "float4 _env[" << maxEnv << "];\n";
	}
	if ( !p.usedLocalParams.empty() ) {
		int maxLocal = *p.usedLocalParams.rbegin() + 1;
		hlsl << "float4 _local[" << maxLocal << "];\n";
	}

	// Matrix constants
	for ( auto& mr : p.usedMatrices ) {
		// Store explicit ARB rows. This avoids HLSL/D3DX matrix packing and
		// transpose conventions leaking into state.matrix.*.row[n].
		hlsl << "float4 " << MatrixConstName( mr ) << "[4];\n";
	}
	if ( isVP && p.eyeDistanceTexCoord5 )
		hlsl << "float4 _yaeInvProjection[4];\n";

	// State param constants (material, lights, fog, lightmodel)
	// Generate named float4 constants for each unique state param
	std::set<std::string> stateConsts;
	for ( auto& paramPair : p.paramMap ) {
		for ( size_t bi = 0; bi < paramPair.second.size(); ++bi ) {
			const auto& pb = paramPair.second[bi];
			if ( pb.type == PARAM_STATE_MATERIAL || pb.type == PARAM_STATE_LIGHT ||
				 pb.type == PARAM_STATE_LIGHTMODEL || pb.type == PARAM_STATE_FOG ) {
				std::string cname = "_sp_" + paramPair.first;
				if ( paramPair.second.size() > 1 ) cname += "_" + std::to_string( bi );
				for ( auto& c : cname ) {
					if ( c == '.' || c == '[' || c == ']' ) c = '_';
				}
				if ( stateConsts.find( cname ) == stateConsts.end() ) {
					hlsl << "float4 " << cname << ";\n";
					stateConsts.insert( cname );
				}
			}
		}
	}

	// Fragment program: sampler declarations with correct type per target
	if ( !isVP ) {
		for ( int unit : p.usedTexUnits ) {
			std::string samplerType = "sampler2D";
			auto tit = p.texTargetPerUnit.find( unit );
			if ( tit != p.texTargetPerUnit.end() ) {
				if ( tit->second == "3D" ) samplerType = "sampler3D";
				else if ( tit->second == "CUBE" ) samplerType = "samplerCUBE";
				else if ( tit->second == "RECT" ) samplerType = "sampler2D"; // RECT → 2D on D3D9
			}
			hlsl << samplerType << " _tex" << unit << " : register(s" << unit << ");\n";
			if ( tit != p.texTargetPerUnit.end() && tit->second == "RECT" )
				hlsl << "float2 _rectScale" << unit << ";\n";
		}
	}
	hlsl << "\n";

	//--------------------------------------------------------------
	// Input/Output structs
	//--------------------------------------------------------------
	if ( isVP ) {
		hlsl << "struct VS_INPUT {\n";
		hlsl << "\tfloat4 position : POSITION;\n";
		if ( p.usesNormal ) hlsl << "\tfloat3 normal : NORMAL;\n";
		hlsl << "\tfloat4 color : COLOR0;\n";
		if ( p.usesColor2 ) hlsl << "\tfloat4 color2 : COLOR1;\n";
		for ( int tc : p.usedTexCoords ) {
			hlsl << "\tfloat4 texcoord" << tc << " : TEXCOORD" << tc << ";\n";
		}
		if ( p.usesFogCoord ) hlsl << "\tfloat4 fogcoord : TEXCOORD7;\n";
		hlsl << "};\n\n";

		hlsl << "struct VS_OUTPUT {\n";
		hlsl << "\tfloat4 position : POSITION;\n";
		hlsl << "\tfloat4 color : COLOR0;\n";
		if ( p.outputsColor2 ) hlsl << "\tfloat4 color2 : COLOR1;\n";
		// Output texcoords
		for ( int tc : CollectOutputTexCoords( p ) ) {
			hlsl << "\tfloat4 texcoord" << tc << " : TEXCOORD" << tc << ";\n";
		}
		if ( p.outputsFog ) hlsl << "\tfloat fogcoord : FOG;\n";
		if ( p.outputsPointSize ) hlsl << "\tfloat pointsize : PSIZE;\n";
		hlsl << "};\n\n";

		hlsl << "VS_OUTPUT main( VS_INPUT v ) {\n";
		hlsl << "\tVS_OUTPUT o = (VS_OUTPUT)0;\n";
	} else {
		// Fragment program
		hlsl << "struct PS_INPUT {\n";
		if ( p.usesFragmentPosition ) hlsl << "\tfloat4 position : VPOS;\n";
		hlsl << "\tfloat4 color : COLOR0;\n";
		if ( p.usesColor2 ) hlsl << "\tfloat4 color2 : COLOR1;\n";
		for ( int tc : p.usedTexCoords ) {
			hlsl << "\tfloat4 texcoord" << tc << " : TEXCOORD" << tc << ";\n";
		}
		if ( p.usesFogCoord ) hlsl << "\tfloat4 fogcoord : TEXCOORD7;\n";
		hlsl << "};\n\n";

		hlsl << "struct PS_OUTPUT {\n";
		hlsl << "\tfloat4 color : COLOR0;\n";
		if ( p.outputsDepth ) hlsl << "\tfloat depth : DEPTH;\n";
		hlsl << "};\n\n";

		hlsl << "PS_OUTPUT main( PS_INPUT f ) {\n";
		hlsl << "\tPS_OUTPUT o = (PS_OUTPUT)0;\n";
	}

	//--------------------------------------------------------------
	// TEMP declarations
	//--------------------------------------------------------------
	for ( auto& temp : p.tempNames ) {
		hlsl << "\tfloat4 " << temp << " = float4(0,0,0,0);\n";
	}

	// Address register (VP)
	if ( !p.addressReg.empty() ) {
		hlsl << "\tint " << p.addressReg << " = 0;\n";
	}

	hlsl << "\n";

	// Position-invariant: auto-transform
	if ( p.positionInvariant && isVP ) {
		// Find the MVP matrix explicitly (it may not be at index 0)
		std::string mvpName;
		for ( auto& mr : p.usedMatrices ) {
			if ( mr.name == "mvp" && !mr.inverse && !mr.transpose && !mr.invtrans ) {
				mvpName = MatrixConstName( mr );
				break;
			}
		}
		if ( !mvpName.empty() ) {
			hlsl << "\t// OPTION ARB_position_invariant\n";
			hlsl << "\to.position = float4(dot(v.position, " << mvpName << "[0]), "
				 << "dot(v.position, " << mvpName << "[1]), "
				 << "dot(v.position, " << mvpName << "[2]), "
				 << "dot(v.position, " << mvpName << "[3]));\n\n";
		}
	}

	//--------------------------------------------------------------
	// Instructions
	//--------------------------------------------------------------
	for ( auto& inst : p.instructions ) {
		std::string dst = FormatDestHLSL( inst.dst, p );
		std::string s0 = ( inst.srcCount > 0 ) ? FormatOperandHLSL( inst.src[0], p, false ) : "";
		std::string s1 = ( inst.srcCount > 1 ) ? FormatOperandHLSL( inst.src[1], p, false ) : "";
		std::string s2 = ( inst.srcCount > 2 ) ? FormatOperandHLSL( inst.src[2], p, false ) : "";
		std::string m0 = ( inst.srcCount > 0 ) ? FormatOperandForMaskHLSL( inst.src[0], p, inst.dst.swizzle ) : "";
		std::string m1 = ( inst.srcCount > 1 ) ? FormatOperandForMaskHLSL( inst.src[1], p, inst.dst.swizzle ) : "";
		std::string m2 = ( inst.srcCount > 2 ) ? FormatOperandForMaskHLSL( inst.src[2], p, inst.dst.swizzle ) : "";

		const std::string& op = inst.opcode;

		if ( op == "MOV" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( m0, inst.saturate ) << ";\n";
		}
		else if ( op == "ADD" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(" + m0 + " + " + m1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "SUB" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(" + m0 + " - " + m1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "MUL" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(" + m0 + " * " + m1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "MAD" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(" + m0 + " * " + m1 + " + " + m2 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "DP3" ) {
			std::string d0 = FormatOperandForMaskHLSL( inst.src[0], p, "xyz" );
			std::string d1 = FormatOperandForMaskHLSL( inst.src[1], p, "xyz" );
			hlsl << "\t" << dst << " = " << MaybeSaturate( "dot(" + d0 + ", " + d1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "DP4" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "dot(" + s0 + ", " + s1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "DPH" ) {
			// DPH: dot(src0.xyz, src1.xyz) + src1.w
			std::string d0 = FormatOperandForMaskHLSL( inst.src[0], p, "xyz" );
			std::string d1 = FormatOperandForMaskHLSL( inst.src[1], p, "xyz" );
			std::string d1w = FormatOperandForMaskHLSL( inst.src[1], p, "w" );
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(dot(" + d0 + ", " + d1 + ") + " + d1w + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "DST" ) {
			std::string expr = "float4(1.0, (" + s0 + ").y * (" + s1 + ").y, (" + s0 + ").z, (" + s1 + ").w)";
			if ( !inst.dst.swizzle.empty() && inst.dst.swizzle != "xyzw" )
				expr += "." + inst.dst.swizzle;
			hlsl << "\t" << dst << " = " << MaybeSaturate( expr, inst.saturate ) << ";\n";
		}
		else if ( op == "RCP" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(1.0 / (" + s0 + "))", inst.saturate ) << ";\n";
		}
		else if ( op == "RSQ" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "rsqrt(abs(" + s0 + "))", inst.saturate ) << ";\n";
		}
		else if ( op == "MAX" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "max(" + m0 + ", " + m1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "MIN" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "min(" + m0 + ", " + m1 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "SGE" ) {
			// SGE: per-component (src0 >= src1) ? 1.0 : 0.0
			hlsl << "\t" << dst << " = " << MaybeSaturate( "step(" + m1 + ", " + m0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "SLT" ) {
			// SLT: per-component (src0 < src1) ? 1.0 : 0.0
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(1.0 - step(" + m1 + ", " + m0 + "))", inst.saturate ) << ";\n";
		}
		else if ( op == "ABS" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "abs(" + m0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "FLR" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "floor(" + m0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "FRC" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "frac(" + m0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "EX2" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "exp2((" + s0 + ").x)", inst.saturate ) << ";\n";
		}
		else if ( op == "EXP" ) {
			// EXP: approximate exponential — returns float4(2^floor(x), frac(x), 2^x, 1.0)
			hlsl << "\t{\n";
			hlsl << "\t\tfloat _t = (" + s0 + ").x;\n";
			hlsl << "\t\t" << dst << " = " << MaybeSaturate( "float4(exp2(floor(_t)), frac(_t), exp2(_t), 1.0)", inst.saturate ) << ";\n";
			hlsl << "\t}\n";
		}
		else if ( op == "LG2" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "log2(abs((" + s0 + ").x))", inst.saturate ) << ";\n";
		}
		else if ( op == "LOG" ) {
			// LOG: approximate logarithm — returns float4(floor(log2(|x|)), |x|/2^floor(log2(|x|)), log2(|x|), 1.0)
			hlsl << "\t{\n";
			hlsl << "\t\tfloat _t = abs((" + s0 + ").x);\n";
			hlsl << "\t\tfloat _l = log2(max(_t, 1.175494e-38));\n";
			hlsl << "\t\t" << dst << " = " << MaybeSaturate( "float4(floor(_l), _t / exp2(floor(_l)), _l, 1.0)", inst.saturate ) << ";\n";
			hlsl << "\t}\n";
		}
		else if ( op == "POW" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "pow(abs((" + s0 + ").x), (" + s1 + ").x)", inst.saturate ) << ";\n";
		}
		else if ( op == "LIT" ) {
			hlsl << "\t" << dst << " = " << MaybeSaturate( "lit((" + s0 + ").x, (" + s0 + ").y, (" + s0 + ").w)", inst.saturate ) << ";\n";
		}
		else if ( op == "SIN" || op == "COS" ) {
			std::string fn = ( op == "SIN" ) ? "sin" : "cos";
			hlsl << "\t" << dst << " = " << MaybeSaturate( fn + "((" + s0 + ").x)", inst.saturate ) << ";\n";
		}
		else if ( op == "SCS" ) {
			// SCS: dst.x = cos, dst.y = sin — apply write mask to RHS
			std::string expr = "float4(cos((" + s0 + ").x), sin((" + s0 + ").x), 0.0, 0.0)";
			if ( !inst.dst.swizzle.empty() && inst.dst.swizzle != "xyzw" )
				expr += "." + inst.dst.swizzle;
			hlsl << "\t" << dst << " = " << expr << ";\n";
		}
		else if ( op == "XPD" ) {
			std::string expr = "float4(cross(float3(" + s0 + "), float3(" + s1 + ")), 0.0)";
			if ( !inst.dst.swizzle.empty() && inst.dst.swizzle != "xyzw" )
				expr += "." + inst.dst.swizzle;
			hlsl << "\t" << dst << " = " << expr << ";\n";
		}
		else if ( op == "CMP" ) {
			// CMP: per-component, if src0 < 0 then src1 else src2 (src0 >= 0 → src2)
			hlsl << "\t" << dst << " = " << MaybeSaturate( "(" + m0 + " < 0 ? " + m1 + " : " + m2 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "LRP" ) {
			// LRP: dst = lerp(src2, src1, src0) = src0 * src1 + (1-src0) * src2
			hlsl << "\t" << dst << " = " << MaybeSaturate( "lerp(" + m2 + ", " + m1 + ", " + m0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "TEX" ) {
			// TEX dst, coord, texture[n], target — use correct fetch function per target
			char buf[64]; sprintf( buf, "_tex%d", inst.texUnit );
			std::string texFn = "tex2D";
			std::string coordSwiz = ".xy";
			if ( inst.texTarget == "3D" ) { texFn = "tex3D"; coordSwiz = ".xyz"; }
			else if ( inst.texTarget == "CUBE" ) { texFn = "texCUBE"; coordSwiz = ".xyz"; }
			std::string coord = "(" + s0 + ")" + coordSwiz;
			if ( inst.texTarget == "RECT" ) {
				char scale[64]; sprintf( scale, "_rectScale%d", inst.texUnit );
				// OpenGL rectangle coordinates use the framebuffer's lower-left
				// origin.  CopyTextureSubLevel preserves the captured image in the
				// D3D texture, whose sampling origin is upper-left, so normalize X
				// directly and reflect Y around the texture height.  Without this,
				// YAE's full-screen pickup effect displays the captured scene upside
				// down and reads as an opaque replacement rather than a screen blur.
				coord = "float2((" + coord + ").x * " + scale + ".x, "
					"1.0 - (" + coord + ").y * " + scale + ".y)";
			}
			std::string fetch = texFn + "(" + buf + ", " + coord + ")";
			hlsl << "\t" << dst << " = " << MaybeSaturate( ApplyResultMaskHLSL( fetch, inst.dst.swizzle ), inst.saturate ) << ";\n";
		}
		else if ( op == "TXP" ) {
			// TXP: projective texture fetch
			char buf[64]; sprintf( buf, "_tex%d", inst.texUnit );
			std::string texFn = "tex2Dproj";
			if ( inst.texTarget == "3D" ) texFn = "tex3Dproj";
			else if ( inst.texTarget == "CUBE" ) texFn = "texCUBEproj";
			hlsl << "\t" << dst << " = " << MaybeSaturate( texFn + "(" + buf + ", " + s0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "TXB" ) {
			// TXB: texture fetch with bias
			char buf[64]; sprintf( buf, "_tex%d", inst.texUnit );
			std::string texFn = "tex2Dbias";
			if ( inst.texTarget == "3D" ) texFn = "tex3Dbias";
			else if ( inst.texTarget == "CUBE" ) texFn = "texCUBEbias";
			hlsl << "\t" << dst << " = " << MaybeSaturate( texFn + "(" + buf + ", " + s0 + ")", inst.saturate ) << ";\n";
		}
		else if ( op == "KIL" ) {
			// KIL: discard fragment if any component < 0 (operand is in dst slot)
			std::string kilSrc = FormatOperandHLSL( inst.dst, p, false );
			hlsl << "\tif (any(" << kilSrc << " < 0.0)) discard;\n";
		}
		else if ( op == "ARL" ) {
			// ARL: address register load
			if ( !p.addressReg.empty() ) {
				hlsl << "\t" << p.addressReg << " = int(floor((" << s0 << ").x));\n";
			}
		}
		else if ( op == "SWZ" ) {
			// SWZ: extended swizzle - complex, just do a MOV for now
			hlsl << "\t" << dst << " = " << s0 << "; // SWZ\n";
		}
		else if ( op == "SEQ" ) {
			// SEQ: per-component (src0 == src1) ? 1.0 : 0.0
			hlsl << "\t" << dst << " = (1.0 - abs(sign(" + m0 + " - " + m1 + ")));\n";
		}
		else if ( op == "SNE" ) {
			// SNE: per-component (src0 != src1) ? 1.0 : 0.0
			hlsl << "\t" << dst << " = abs(sign(" + m0 + " - " + m1 + "));\n";
		}
		else {
			hlsl << "\t// UNSUPPORTED: " << op << "\n";
		}
	}

	if ( isVP && p.eyeDistanceTexCoord5 ) {
		// See ARB_CompileProgram. Recover the eye-space position from the final
		// clip position and give TEXCOORD5 that length, keeping its direction.
		hlsl << "\n\t// YAE_COMPAT yae_eye_distance_fog\n";
		hlsl << "\tfloat4 _yaeEye = float4(dot(o.position, _yaeInvProjection[0]), "
			 << "dot(o.position, _yaeInvProjection[1]), "
			 << "dot(o.position, _yaeInvProjection[2]), "
			 << "dot(o.position, _yaeInvProjection[3]));\n";
		hlsl << "\tfloat _yaeToEyeLength = length(o.texcoord5.xyz);\n";
		hlsl << "\tif (_yaeToEyeLength > 0.0)\n";
		hlsl << "\t\to.texcoord5.xyz *= length(_yaeEye.xyz / _yaeEye.w) / _yaeToEyeLength;\n";
	}

	//--------------------------------------------------------------
	// Return
	//--------------------------------------------------------------
	hlsl << "\n\treturn o;\n";
	hlsl << "}\n";

	return hlsl.str();
}

//==================================================================================
// COMPILER: HLSL → D3D9 shader
//==================================================================================
bool ARB_CompileProgram( GLuint programId, GLenum target, const char* source, int length, std::string& errorString )
{
	uint32_t sourceHash = HashARBProgram( source, length );
	std::string arbDumpPath = DumpARBProgramText( programId, target, sourceHash, "arb", source, length );
	logPrintf( "ARB_CompileProgram: program %u (%s), %d source bytes, hash %08X, dump %s\n",
		programId, target == GL_VERTEX_PROGRAM_ARB ? "VP" : "FP", length, sourceHash,
		arbDumpPath.empty() ? "FAILED" : arbDumpPath.c_str() );

	// Delete previous compilation for this program
	auto it = gARBCompiledPrograms.find( programId );
	if ( it != gARBCompiledPrograms.end() ) {
		delete it->second;
		gARBCompiledPrograms.erase( it );
	}

	// Parse
	ARBParsedProgram parsed;
	if ( !ARB_ParseProgram( source, length, target, parsed ) ) {
		errorString = "Failed to parse ARB program";
		return false;
	}

	// YAE_COMPAT (yae_eye_distance_fog, opt-in, deliberately deviates from native).
	// DS2's rigid and skinned material VPs write camera_pos_ws - inst_matrix * pos
	// to TEXCOORD5, and every DS2 fragment program reading TEXCOORD5 uses only its
	// length, as the linear fog distance. For weapons attached to a bone, DS2
	// supplies the owner's inst_matrix without the (scaled) bone transform that
	// it multiplies into the modelview, so the length is thousands of units and
	// the fog saturates to its blue-green colour. The native NVIDIA driver renders
	// the same blue weapon. For every consistent draw the length already equals
	// the eye-space distance, so rescaling to that distance leaves them unchanged.
	if ( target == GL_VERTEX_PROGRAM_ARB && D3DGlobal.settings.game.yaeEyeDistanceFog &&
		CollectOutputTexCoords( parsed ).count( 5 ) ) {
		parsed.eyeDistanceTexCoord5 = true;
		logPrintf( "YAE_COMPAT: program %u TEXCOORD5 uses the eye-space fog distance.\n", programId );
	}

	// Generate HLSL
	std::string hlslSource = ARB_GenerateHLSL( parsed );
	std::string hlslDumpPath = DumpARBProgramText( programId, target, sourceHash, "hlsl",
		hlslSource.c_str(), hlslSource.size() );
	logPrintf( "ARB_CompileProgram: generated %u HLSL bytes, dump %s\n",
		(unsigned int)hlslSource.size(), hlslDumpPath.empty() ? "FAILED" : hlslDumpPath.c_str() );

#if 0 // Full HLSL is stored in the dump file above.
	logPrintf( "ARB_CompileProgram: program %u (%s) → HLSL:\n%s\n",
		programId, target == GL_VERTEX_PROGRAM_ARB ? "VP" : "FP", hlslSource.c_str() );
#endif

	// Compile
	ID3DXBuffer* shaderBuffer = nullptr;
	ID3DXBuffer* errorBuffer = nullptr;
	LPD3DXCONSTANTTABLE constTable = nullptr;

	const char* profile = ( target == GL_VERTEX_PROGRAM_ARB ) ? "vs_2_0" : "ps_2_0";
	// Try vs_3_0/ps_3_0 if available for more instruction slots
	if ( target == GL_VERTEX_PROGRAM_ARB && D3DGlobal.hD3DCaps.VertexShaderVersion >= D3DVS_VERSION(3,0) )
		profile = "vs_3_0";
	if ( target == GL_FRAGMENT_PROGRAM_ARB && D3DGlobal.hD3DCaps.PixelShaderVersion >= D3DPS_VERSION(3,0) )
		profile = "ps_3_0";

	HRESULT hr = D3DXCompileShader(
		hlslSource.c_str(),
		(UINT)hlslSource.size(),
		NULL,		// no macros
		NULL,		// no includes
		"main",
		profile,
		0,			// flags
		&shaderBuffer,
		&errorBuffer,
		&constTable
	);

	if ( FAILED( hr ) ) {
		if ( errorBuffer ) {
			errorString = std::string( (const char*)errorBuffer->GetBufferPointer(), errorBuffer->GetBufferSize() );
			errorBuffer->Release();
		} else {
			errorString = "D3DXCompileShader failed";
		}
		logPrintf( "ARB_CompileProgram: FAILED for program %u: %s\n", programId, errorString.c_str() );
		if ( shaderBuffer ) shaderBuffer->Release();
		return false;
	}

	if ( errorBuffer ) {
		logPrintf( "ARB_CompileProgram: warnings for program %u: %s\n", programId,
			std::string( (const char*)errorBuffer->GetBufferPointer(), errorBuffer->GetBufferSize() ).c_str() );
		errorBuffer->Release();
	}

	// Create the D3D shader object
	ARBCompiledProgram* prog = new ARBCompiledProgram;
	prog->target = target;
	prog->constants = constTable;
	prog->parsed = parsed;
	prog->hlslSource = hlslSource;

	if ( target == GL_VERTEX_PROGRAM_ARB ) {
		hr = D3DGlobal.pDevice->CreateVertexShader( (DWORD*)shaderBuffer->GetBufferPointer(), &prog->vs );
	} else {
		hr = D3DGlobal.pDevice->CreatePixelShader( (DWORD*)shaderBuffer->GetBufferPointer(), &prog->ps );
	}

	shaderBuffer->Release();

	if ( FAILED( hr ) ) {
		errorString = "CreateShader failed";
		delete prog;
		logPrintf( "ARB_CompileProgram: CreateShader FAILED for program %u\n", programId );
		return false;
	}

	gARBCompiledPrograms[programId] = prog;
	logPrintf( "ARB_CompileProgram: SUCCESS for program %u (%s)\n", programId, profile );
	return true;
}

//==================================================================================
// SHADER ACTIVATION: set D3D shaders and constants before drawing
//==================================================================================

// Helper: get a matrix from the GL state and optionally transform it
static void GetGLMatrix( const ARBParsedProgram::MatrixRef& mr, D3DXMATRIX& out )
{
	D3DXMATRIX mat;
	D3DXMatrixIdentity( &mat );

	if ( mr.name == "mvp" ) {
		// MVP = projection * modelview
		D3DXMATRIX mv = *D3DGlobal.modelviewMatrixStack->top();
		D3DXMATRIX proj = *D3DGlobal.projectionMatrixStack->top();
		D3DXMatrixMultiply( &mat, &mv, &proj );
	} else if ( mr.name == "modelview" ) {
		mat = *D3DGlobal.modelviewMatrixStack->top();
	} else if ( mr.name == "projection" ) {
		mat = *D3DGlobal.projectionMatrixStack->top();
	} else if ( mr.name == "texture" ) {
		int idx = mr.index;
		if ( idx >= 0 && idx < MAX_D3D_TMU )
			mat = *D3DGlobal.textureMatrixStack[idx]->top();
	}

	// Apply modifiers
	if ( mr.invtrans ) {
		D3DXMATRIX inv;
		if ( D3DXMatrixInverse( &inv, nullptr, &mat ) ) {
			D3DXMatrixTranspose( &out, &inv );
		} else {
			D3DXMatrixIdentity( &out );
		}
	} else if ( mr.inverse ) {
		if ( !D3DXMatrixInverse( &out, nullptr, &mat ) ) {
			D3DXMatrixIdentity( &out );
		}
	} else if ( mr.transpose ) {
		D3DXMatrixTranspose( &out, &mat );
	} else {
		out = mat;
	}
}

// Set constants for a compiled program
static void SetProgramConstants( ARBCompiledProgram* prog, bool isVS )
{
	if ( !prog || !prog->constants ) return;

	LPDIRECT3DDEVICE9 dev = D3DGlobal.pDevice;
	LPD3DXCONSTANTTABLE ct = prog->constants;
	const ARBParsedProgram& p = prog->parsed;

	// Env params
	extern GLfloat (*ARB_EnvParams( GLenum target ))[4];
	extern GLfloat (*ARB_LocalParams( GLenum target ))[4];

	if ( !p.usedEnvParams.empty() ) {
		D3DXHANDLE h = ct->GetConstantByName( nullptr, "_env" );
		if ( h ) {
			GLfloat (*envp)[4] = ARB_EnvParams( prog->target );
			int maxEnv = *p.usedEnvParams.rbegin() + 1;
			ct->SetFloatArray( dev, h, &envp[0][0], maxEnv * 4 );
		}
	}

	// Local params
	if ( !p.usedLocalParams.empty() ) {
		D3DXHANDLE h = ct->GetConstantByName( nullptr, "_local" );
		if ( h ) {
			GLfloat (*localp)[4] = ARB_LocalParams( prog->target );
			int maxLocal = *p.usedLocalParams.rbegin() + 1;
			ct->SetFloatArray( dev, h, &localp[0][0], maxLocal * 4 );
		}
	}

	// Matrices
	for ( auto& mr : p.usedMatrices ) {
		std::string name = MatrixConstName( mr );
		D3DXHANDLE h = ct->GetConstantByName( nullptr, name.c_str() );
		if ( h ) {
			D3DXMATRIX mat;
			GetGLMatrix( mr, mat );
			// Matrix stacks contain the transpose of the conceptual OpenGL matrix
			// (so D3D's row-vector fixed pipeline produces the same transform).
			// ARB row[n] therefore corresponds to column n of this D3D matrix.
			float rows[16];
			for ( int row = 0; row < 4; ++row )
				for ( int column = 0; column < 4; ++column )
					rows[row * 4 + column] = mat.m[column][row];
			ct->SetFloatArray( dev, h, rows, 16 );
		}
	}

	if ( isVS && p.eyeDistanceTexCoord5 ) {
		// The inverse of the projection used for state.matrix.mvp maps the
		// program's clip position back to eye space. Same row layout as above.
		D3DXHANDLE h = ct->GetConstantByName( nullptr, "_yaeInvProjection" );
		if ( h ) {
			ARBParsedProgram::MatrixRef inverseProjection = { "projection", 0, true, false, false };
			D3DXMATRIX mat;
			GetGLMatrix( inverseProjection, mat );
			float rows[16];
			for ( int row = 0; row < 4; ++row )
				for ( int column = 0; column < 4; ++column )
					rows[row * 4 + column] = mat.m[column][row];
			ct->SetFloatArray( dev, h, rows, 16 );
		}
	}

	// State params (material, lights, fog, lightmodel)
	for ( auto& paramPair : p.paramMap ) {
		for ( size_t bi = 0; bi < paramPair.second.size(); ++bi ) {
			const ARBParamBinding& pb = paramPair.second[bi];
			if ( pb.type != PARAM_STATE_MATERIAL && pb.type != PARAM_STATE_LIGHT &&
				 pb.type != PARAM_STATE_LIGHTMODEL && pb.type != PARAM_STATE_FOG )
				continue;

			std::string cname = "_sp_" + paramPair.first;
			if ( paramPair.second.size() > 1 ) cname += "_" + std::to_string( bi );
			for ( auto& c : cname ) {
				if ( c == '.' || c == '[' || c == ']' ) c = '_';
			}

			D3DXHANDLE h = ct->GetConstantByName( nullptr, cname.c_str() );
			if ( !h ) continue;

			float val[4] = { 0, 0, 0, 1 };

			if ( pb.type == PARAM_STATE_LIGHTMODEL ) {
				// lightmodel.ambient → D3DState.LightingState.lightModelAmbient (DWORD ARGB)
				DWORD amb = D3DState.LightingState.lightModelAmbient;
				val[0] = ((amb >> 16) & 0xFF) / 255.0f;
				val[1] = ((amb >> 8) & 0xFF) / 255.0f;
				val[2] = (amb & 0xFF) / 255.0f;
				val[3] = ((amb >> 24) & 0xFF) / 255.0f;
			}
			else if ( pb.type == PARAM_STATE_FOG ) {
				if ( pb.stateField.find( "params" ) != std::string::npos ) {
					val[0] = D3DState.FogState.fogDensity;
					val[1] = D3DState.FogState.fogStart;
					val[2] = D3DState.FogState.fogEnd;
					val[3] = ( D3DState.FogState.fogEnd != D3DState.FogState.fogStart ) ?
						1.0f / ( D3DState.FogState.fogEnd - D3DState.FogState.fogStart ) : 0.0f;
				} else if ( pb.stateField.find( "color" ) != std::string::npos ) {
					DWORD c = D3DState.FogState.fogColor;
					val[0] = ((c >> 16) & 0xFF) / 255.0f;
					val[1] = ((c >> 8) & 0xFF) / 255.0f;
					val[2] = (c & 0xFF) / 255.0f;
					val[3] = ((c >> 24) & 0xFF) / 255.0f;
				}
			}
			// Additional state.material / state.light support can be added as needed

			ct->SetFloatArray( dev, h, val, 4 );
		}
	}
}

// Externally accessible bound program IDs (defined in d3d_extension.cpp)
extern GLuint ARB_GetBoundVertexProgram();
extern GLuint ARB_GetBoundFragmentProgram();

int ARB_GetRequiredVertexTexCoordCount()
{
	// A programmable vertex shader consumes only its own declared inputs.  A
	// fragment shader's texcoord usage describes vertex-shader outputs, not extra
	// entries in the source vertex declaration.  Folding both sets together made
	// YAE's skinned viewmodels use TEXCOORD0..5 even though their vertex program
	// reads only TEXCOORD0..2.  Apart from wasting bandwidth, that produces a
	// source declaration containing inputs which do not belong to the translated
	// vertex program.
	if ( D3DState.EnableState.vertexProgramEnabled ) {
		ARBCompiledProgram* vp = ARB_GetCompiledProgram( ARB_GetBoundVertexProgram() );
		if ( vp && vp->target == GL_VERTEX_PROGRAM_ARB && vp->vs ) {
			if ( vp->parsed.usedTexCoords.empty() ) return 0;
			return *vp->parsed.usedTexCoords.rbegin() + 1;
		}
	}

	// With the fixed-function vertex pipeline, make enough source coordinates
	// available for the programmable fragment stage.
	int required = 0;
	if ( D3DState.EnableState.fragmentProgramEnabled ) {
		ARBCompiledProgram* fp = ARB_GetCompiledProgram( ARB_GetBoundFragmentProgram() );
		if ( fp && fp->target == GL_FRAGMENT_PROGRAM_ARB && !fp->parsed.usedTexCoords.empty() )
			required = *fp->parsed.usedTexCoords.rbegin() + 1;
	}
	return required;
}

static void SetProgramTextures( ARBCompiledProgram* fp )
{
	if ( !fp || !D3DGlobal.pDevice ) return;
	for ( int unit : fp->parsed.usedTexUnits ) {
		if ( unit < 0 || unit >= MAX_D3D_TMU ) continue;
		int target = D3D_TEXTARGET_2D;
		auto targetIt = fp->parsed.texTargetPerUnit.find( unit );
		if ( targetIt != fp->parsed.texTargetPerUnit.end() ) {
			if ( targetIt->second == "1D" ) target = D3D_TEXTARGET_1D;
			else if ( targetIt->second == "3D" ) target = D3D_TEXTARGET_3D;
			else if ( targetIt->second == "CUBE" ) target = D3D_TEXTARGET_CUBE;
		}
		D3DTextureObject* texture = D3DState.TextureState.currentTexture[unit][target];
		D3DGlobal.pDevice->SetTexture( unit, texture ? texture->GetD3DTexture() : nullptr );
		if ( !texture ) continue;
		if ( targetIt != fp->parsed.texTargetPerUnit.end() && targetIt->second == "RECT" &&
			fp->constants ) {
			char constantName[64];
			sprintf_s( constantName, "_rectScale%d", unit );
			D3DXHANDLE scaleHandle = fp->constants->GetConstantByName( nullptr, constantName );
			if ( scaleHandle ) {
				const float rectangleScale[2] = {
					texture->GetWidth() ? 1.0f / texture->GetWidth() : 1.0f,
					texture->GetHeight() ? 1.0f / texture->GetHeight() : 1.0f
				};
				fp->constants->SetFloatArray( D3DGlobal.pDevice, scaleHandle, rectangleScale, 2 );
				PRINT_ONCE( "YAE_COMPAT: ARB RECT coordinates normalized for D3D9 texture %ux%u.\n",
					texture->GetWidth(), texture->GetHeight() );
			}
		}
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_BORDERCOLOR, texture->GetD3DBorderColor() );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_ADDRESSU, texture->GetD3DAddressMode( 0 ) );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_ADDRESSV, texture->GetD3DAddressMode( 1 ) );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_ADDRESSW, texture->GetD3DAddressMode( 2 ) );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_MAXANISOTROPY, texture->GetAnisotropy() );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_MAGFILTER, texture->GetD3DFilter( 0 ) );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_MINFILTER, texture->GetD3DFilter( 1 ) );
		D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_MIPFILTER, texture->GetD3DFilter( 2 ) );
		if ( D3DGlobal.hD3DCaps.RasterCaps & D3DPRASTERCAPS_MIPMAPLODBIAS )
			D3DGlobal.pDevice->SetSamplerState( unit, D3DSAMP_MIPMAPLODBIAS, UTIL_FloatToDword( texture->GetLodBias() ) );
	}
}

bool ARB_ActivateShaders()
{
	LPDIRECT3DDEVICE9 dev = D3DGlobal.pDevice;
	if ( !dev ) return false;

	bool vpEnabled = D3DState.EnableState.vertexProgramEnabled != 0;
	bool fpEnabled = D3DState.EnableState.fragmentProgramEnabled != 0;
	if ( !vpEnabled && !fpEnabled ) return false;

	bool activated = false;

	if ( vpEnabled ) {
		GLuint vpId = ARB_GetBoundVertexProgram();
		ARBCompiledProgram* vp = ARB_GetCompiledProgram( vpId );
		if ( vp && vp->vs ) {
			dev->SetVertexShader( vp->vs );
			SetProgramConstants( vp, true );
			activated = true;
			gARBVertexShaderActive = true;
		} else {
			dev->SetVertexShader( nullptr );
			gARBVertexShaderActive = false;
		}
	} else {
		dev->SetVertexShader( nullptr );
		gARBVertexShaderActive = false;
	}

	if ( fpEnabled ) {
		GLuint fpId = ARB_GetBoundFragmentProgram();
		ARBCompiledProgram* fp = ARB_GetCompiledProgram( fpId );
		if ( fp && fp->ps ) {
			SetProgramTextures( fp );
			dev->SetPixelShader( fp->ps );
			SetProgramConstants( fp, false );
			activated = true;
			gARBPixelShaderActive = true;
		} else {
			dev->SetPixelShader( nullptr );
			gARBPixelShaderActive = false;
		}
	} else {
		if ( gARBPixelShaderActive ) {
			D3DState.TextureState.textureSamplerStateChanged = TRUE;
			D3DState.TextureState.textureEnableChanged = TRUE;
		}
		dev->SetPixelShader( nullptr );
		gARBPixelShaderActive = false;
	}

	gARBShadersActive = activated;
	return activated;
}

void ARB_DeactivateShaders()
{
	if ( !gARBShadersActive ) return;

	LPDIRECT3DDEVICE9 dev = D3DGlobal.pDevice;
	if ( !dev ) return;

	dev->SetPixelShader( nullptr );
	if ( D3DGlobal.settings.game.orthovertexshader && D3DGlobal_IsOrthoProjection() ) {
		dev->SetVertexShader( D3DGlobal.orthoShaders.vs );
		D3DGlobal.orthoShaders.constants->SetMatrix( dev, "projectionMatrix", D3DGlobal.projectionMatrixStack->top() );
		D3DGlobal.orthoShaders.constants->SetMatrix( dev, "worldMatrix", D3DGlobal.modelMatrixStack->top() );
		D3DGlobal.orthoShaders.constants->SetMatrix( dev, "viewMatrix", D3DGlobal.viewMatrixStack->top() );
		float texelOffset[4] = { -1.0f / D3DState.viewport.Width, 1.0f / D3DState.viewport.Height, 0.0f, 0.0f };
		dev->SetVertexShaderConstantF( 0, texelOffset, 1 );
	} else {
		dev->SetVertexShader( nullptr );
	}
	if ( gARBPixelShaderActive ) {
		D3DState.TextureState.textureSamplerStateChanged = TRUE;
		D3DState.TextureState.textureEnableChanged = TRUE;
	}

	gARBShadersActive = false;
	gARBVertexShaderActive = false;
	gARBPixelShaderActive = false;
}
