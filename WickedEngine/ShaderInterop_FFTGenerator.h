#ifndef WI_SHADERINTEROP_FFTGENERATOR_H
#define WI_SHADERINTEROP_FFTGENERATOR_H
#include "ShaderInterop.h"

CBUFFER(FFTGeneratorCB, CBSLOT_OTHER_FFTGENERATOR)
{
	uint thread_count;
	uint ostride;
	uint istride;
	uint pstride;

	float phase_base;
};

#endif // WI_SHADERINTEROP_FFTGENERATOR_H
