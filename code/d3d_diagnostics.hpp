/***************************************************************************
* QindieGL diagnostic instrumentation.
***************************************************************************/
#ifndef QINDIEGL_D3D_DIAGNOSTICS_H
#define QINDIEGL_D3D_DIAGNOSTICS_H

#include <stdint.h>

enum QGLLogLevel
{
	QGL_LOG_ERROR = 0,
	QGL_LOG_WARN = 1,
	QGL_LOG_INFO = 2,
	QGL_LOG_DEBUG = 3,
	QGL_LOG_TRACE = 4
};

void logSetLevel( int level );
int logGetLevel();
bool logIsEnabled( QGLLogLevel level );
void logPrintfLevel( QGLLogLevel level, const char *category, const char *fmt, ... );

void QGL_DiagnosticsInitialize();
void QGL_DiagnosticsShutdown();
void QGL_DiagnosticsConfigure( int crashDiagnostics, int debugMaxDrawCall,
	int debugDumpFrame, int debugDumpDraw );

uint64_t QGL_DiagnosticsGetFrameId();
uint64_t QGL_DiagnosticsGetDrawId();

// Returns false when the selected debug draw limiter requests that this draw
// be observed but not submitted to D3D9.
bool QGL_DiagnosticsBeginDraw( const char *api, unsigned int mode, int count,
	int first, unsigned int indexType, const void *indices );
void QGL_DiagnosticsAfterDraw();
void QGL_DiagnosticsEndFrame( long presentResult );

void QGL_DiagnosticsRecordEvent( bool d3dEvent, const char *category, const char *fmt, ... );
void QGL_DiagnosticsRecordD3DFailure( const char *call, long result );
void QGL_DiagnosticsRecordDeviceReset( long result );
void QGL_DiagnosticsRecordPBufferCreated();
void QGL_DiagnosticsRecordARBProgramUpload( bool compiled, bool failed );
void QGL_DiagnosticsRecordVBOCreated();
void QGL_DiagnosticsRecordVBOBytes( int64_t delta );
void QGL_DiagnosticsSetRenderTarget( const char *name );

// Projection classes and world-to-HUD boundaries (d3d_view_diagnostics.cpp).
void QGL_ViewDiagnosticsOnDraw( uint64_t frame, uint64_t draw );
void QGL_ViewDiagnosticsOnFrameEnd( uint64_t frame );
void QGL_ViewDiagnosticsDumpSummary();

// YAE Phase F program/modelview history, dumped by the program-fog probe.
// op: 'B' bind program, 'L' local parameter write, 'E'/'e' enable/disable
// program target, 'M' modelview operation (index = operation), 'D' draw.
void QGL_DiagnosticsRecordProgramOp( char op, unsigned int target, unsigned int program,
	int index, const float *values );

void QGL_DiagnosticsDumpCapabilityReport();
void QGL_DiagnosticsDumpSessionSummary();

// All GL error writes go through this helper so DEBUG/TRACE logs retain the
// originating wrapper function. Successful writes clear the saved origin.
void QGL_SetErrorImpl( long error, const char *source );
#define QGL_SET_ERROR(error) QGL_SetErrorImpl( (long)(error), __FUNCTION__ )

#endif // QINDIEGL_D3D_DIAGNOSTICS_H
