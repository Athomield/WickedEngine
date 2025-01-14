#include "wiFFTGenerator.h"
#include "wiResourceManager.h"
#include "wiRenderer.h"
#include "ShaderInterop_FFTGenerator.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

using namespace wiGraphics;

static const ComputeShader* pRadix008A_CS = nullptr;
static const ComputeShader* pRadix008A_CS2 = nullptr;

void radix008A(
	const CSFFT512x512_Plan& fft_plan,
	const GPUResource& pUAV_Dst,
	const GPUResource& pSRV_Src,
	UINT thread_count,
	UINT istride, 
	CommandList cmd)
{
	// Setup execution configuration
	UINT grid = thread_count / COHERENCY_GRANULARITY;

	GraphicsDevice* device = wiRenderer::GetDevice();

	// Buffers
	const GPUResource* cs_srvs[1] = { &pSRV_Src };
	device->BindResources(CS, cs_srvs, TEXSLOT_ONDEMAND0, 1, cmd);

	const GPUResource* cs_uavs[1] = { &pUAV_Dst };
	device->BindUAVs(CS, cs_uavs, 0, ARRAYSIZE(cs_uavs), cmd);

	// Shader
	if (istride > 1)
	{
		device->BindComputeShader(pRadix008A_CS, cmd);
	}
	else
	{
		device->BindComputeShader(pRadix008A_CS2, cmd);
	}

	// Execute
	device->Dispatch(grid, 1, 1, cmd);

	device->UAVBarrier(cs_uavs, ARRAYSIZE(cs_uavs), cmd);

	// Unbind resource
	device->UnbindResources(TEXSLOT_ONDEMAND0, 1, cmd);
	device->UnbindUAVs(0, 1, cmd);
}

void fft_512x512_c2c(
	const CSFFT512x512_Plan& fft_plan,
	const GPUResource& pUAV_Dst,
	const GPUResource& pSRV_Dst,
	const GPUResource& pSRV_Src, 
	CommandList cmd)
{
	const UINT thread_count = fft_plan.slices * (512 * 512) / 8;
	GraphicsDevice* device = wiRenderer::GetDevice();
	const GPUBuffer* cs_cbs;

	UINT istride = 512 * 512 / 8;
	cs_cbs = &fft_plan.pRadix008A_CB[0];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, fft_plan.pBuffer_Tmp, pSRV_Src, thread_count, istride, cmd);

	istride /= 8;
	cs_cbs = &fft_plan.pRadix008A_CB[1];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, pUAV_Dst, fft_plan.pBuffer_Tmp, thread_count, istride, cmd);

	istride /= 8;
	cs_cbs = &fft_plan.pRadix008A_CB[2];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, fft_plan.pBuffer_Tmp, pSRV_Dst, thread_count, istride, cmd);

	istride /= 8;
	cs_cbs = &fft_plan.pRadix008A_CB[3];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, pUAV_Dst, fft_plan.pBuffer_Tmp, thread_count, istride, cmd);

	istride /= 8;
	cs_cbs = &fft_plan.pRadix008A_CB[4];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, fft_plan.pBuffer_Tmp, pSRV_Dst, thread_count, istride, cmd);

	istride /= 8;
	cs_cbs = &fft_plan.pRadix008A_CB[5];
	device->BindConstantBuffer(CS, cs_cbs, CB_GETBINDSLOT(FFTGeneratorCB), cmd);
	radix008A(fft_plan, pUAV_Dst, fft_plan.pBuffer_Tmp, thread_count, istride, cmd);
}

void create_cbuffers_512x512(CSFFT512x512_Plan& plan, GraphicsDevice* device, UINT slices)
{
	// Create 6 cbuffers for 512x512 transform.

	GPUBufferDesc cb_desc;
	cb_desc.Usage = USAGE_IMMUTABLE;
	cb_desc.BindFlags = BIND_CONSTANT_BUFFER;
	cb_desc.CPUAccessFlags = 0;
	cb_desc.MiscFlags = 0;
	cb_desc.ByteWidth = sizeof(FFTGeneratorCB);
	cb_desc.StructureByteStride = 0;

	SubresourceData cb_data;
	cb_data.SysMemPitch = 0;
	cb_data.SysMemSlicePitch = 0;

	// Buffer 0
	const UINT thread_count = slices * (512 * 512) / 8;
	UINT ostride = 512 * 512 / 8;
	UINT istride = ostride;
	double phase_base = -TWO_PI / (512.0 * 512.0);

	FFTGeneratorCB cb_data_buf0 = { thread_count, ostride, istride, 512, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf0;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[0]);

	// Buffer 1
	istride /= 8;
	phase_base *= 8.0;

	FFTGeneratorCB cb_data_buf1 = { thread_count, ostride, istride, 512, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf1;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[1]);

	// Buffer 2
	istride /= 8;
	phase_base *= 8.0;

	FFTGeneratorCB cb_data_buf2 = { thread_count, ostride, istride, 512, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf2;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[2]);

	// Buffer 3
	istride /= 8;
	phase_base *= 8.0;
	ostride /= 512;

	FFTGeneratorCB cb_data_buf3 = { thread_count, ostride, istride, 1, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf3;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[3]);

	// Buffer 4
	istride /= 8;
	phase_base *= 8.0;

	FFTGeneratorCB cb_data_buf4 = { thread_count, ostride, istride, 1, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf4;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[4]);

	// Buffer 5
	istride /= 8;
	phase_base *= 8.0;

	FFTGeneratorCB cb_data_buf5 = { thread_count, ostride, istride, 1, (float)phase_base };
	cb_data.pSysMem = &cb_data_buf5;

	device->CreateBuffer(&cb_desc, &cb_data, &plan.pRadix008A_CB[5]);
}

void fft512x512_create_plan(CSFFT512x512_Plan& plan, UINT slices)
{
	GraphicsDevice* device = wiRenderer::GetDevice();

	plan.slices = slices;

	// Constants
	// Create 6 cbuffers for 512x512 transform
	create_cbuffers_512x512(plan, device, slices);

	// Temp buffer
	GPUBufferDesc buf_desc;
	buf_desc.ByteWidth = sizeof(float) * 2 * (512 * slices) * 512;
	buf_desc.Usage = USAGE_DEFAULT;
	buf_desc.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;
	buf_desc.CPUAccessFlags = 0;
	buf_desc.MiscFlags = RESOURCE_MISC_BUFFER_STRUCTURED;
	buf_desc.StructureByteStride = sizeof(float) * 2;

	device->CreateBuffer(&buf_desc, nullptr, &plan.pBuffer_Tmp);
}



void CSFFT_512x512_Data_t::LoadShaders()
{
	std::string path = wiRenderer::GetShaderPath();

	pRadix008A_CS = static_cast<const ComputeShader*>(wiResourceManager::GetShaderManager().add(path+ "fft_512x512_c2c_CS.cso", wiResourceManager::COMPUTESHADER));
	pRadix008A_CS2 = static_cast<const ComputeShader*>(wiResourceManager::GetShaderManager().add(path + "fft_512x512_c2c_v2_CS.cso", wiResourceManager::COMPUTESHADER));
}
