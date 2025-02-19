#include "wiImage.h"
#include "wiResourceManager.h"
#include "wiRenderer.h"
#include "wiHelper.h"
#include "SamplerMapping.h"
#include "ResourceMapping.h"
#include "wiSceneSystem.h"
#include "ShaderInterop_Image.h"
#include "wiBackLog.h"

#include <atomic>

using namespace std;
using namespace wiGraphics;

namespace wiImage
{

	enum IMAGE_SHADER
	{
		IMAGE_SHADER_STANDARD,
		IMAGE_SHADER_SEPARATENORMALMAP,
		IMAGE_SHADER_MASKED,
		IMAGE_SHADER_FULLSCREEN,
		IMAGE_SHADER_COUNT
	};
	enum IMAGE_HDR
	{
		IMAGE_HDR_DISABLED,
		IMAGE_HDR_ENABLED,
		IMAGE_HDR_COUNT
	};
	enum IMAGE_SAMPLING
	{
		IMAGE_SAMPLING_SIMPLE,
		IMAGE_SAMPLING_BICUBIC,
		IMAGE_SAMPLING_COUNT,
	};

	GPUBuffer				constantBuffer;
	const VertexShader*		vertexShader = nullptr;
	const VertexShader*		screenVS = nullptr;
	const PixelShader*		imagePS[IMAGE_SHADER_COUNT][IMAGE_SAMPLING_COUNT];
	BlendState				blendStates[BLENDMODE_COUNT];
	RasterizerState			rasterizerState;
	DepthStencilState		depthStencilStates[STENCILMODE_COUNT][STENCILREFMODE_COUNT];
	PipelineState			imagePSO[IMAGE_SHADER_COUNT][BLENDMODE_COUNT][STENCILMODE_COUNT][STENCILREFMODE_COUNT][IMAGE_HDR_COUNT][IMAGE_SAMPLING_COUNT];

	std::atomic_bool initialized{ false };


	void Draw(const Texture2D* texture, const wiImageParams& params, CommandList cmd)
	{
		if (!initialized.load())
		{
			return;
		}

		GraphicsDevice* device = wiRenderer::GetDevice();
		device->EventBegin("Image", cmd);

		bool fullScreenEffect = false;

		device->BindResource(PS, texture, TEXSLOT_ONDEMAND0, cmd);

		UINT stencilRef = params.stencilRef;
		if (params.stencilRefMode == STENCILREFMODE_USER)
		{
			stencilRef = wiRenderer::CombineStencilrefs(STENCILREF_EMPTY, (uint8_t)stencilRef);
		}
		device->BindStencilRef(stencilRef, cmd);

		IMAGE_SAMPLING sampling_type = params.quality == QUALITY_BICUBIC ? IMAGE_SAMPLING_BICUBIC : IMAGE_SAMPLING_SIMPLE;

		if (params.quality == QUALITY_NEAREST)
		{
			if (params.sampleFlag == SAMPLEMODE_MIRROR)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_POINT_MIRROR), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_WRAP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_POINT_WRAP), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_CLAMP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_POINT_CLAMP), SSLOT_ONDEMAND0, cmd);
		}
		else if (params.quality == QUALITY_LINEAR)
		{
			if (params.sampleFlag == SAMPLEMODE_MIRROR)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_LINEAR_MIRROR), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_WRAP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_LINEAR_WRAP), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_CLAMP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_LINEAR_CLAMP), SSLOT_ONDEMAND0, cmd);
		}
		else if (params.quality == QUALITY_ANISOTROPIC)
		{
			if (params.sampleFlag == SAMPLEMODE_MIRROR)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_ANISO_MIRROR), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_WRAP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_ANISO_WRAP), SSLOT_ONDEMAND0, cmd);
			else if (params.sampleFlag == SAMPLEMODE_CLAMP)
				device->BindSampler(PS, wiRenderer::GetSampler(SSLOT_ANISO_CLAMP), SSLOT_ONDEMAND0, cmd);
		}

		ImageCB cb;
		cb.xColor = params.col;
		const float darken = 1 - params.fade;
		cb.xColor.x *= darken;
		cb.xColor.y *= darken;
		cb.xColor.z *= darken;
		cb.xColor.w *= params.opacity;
		cb.xMirror = params.isMirrorEnabled() ? 1 : 0;
		cb.xMipLevel = params.mipLevel;

		if (params.isFullScreenEnabled())
		{
			device->BindPipelineState(&imagePSO[IMAGE_SHADER_FULLSCREEN][params.blendFlag][params.stencilComp][params.stencilRefMode][params.isHDREnabled()][sampling_type], cmd);
			device->UpdateBuffer(&constantBuffer, &cb, cmd);
			device->BindConstantBuffer(PS, &constantBuffer, CB_GETBINDSLOT(ImageCB), cmd);
			device->Draw(3, 0, cmd);
			device->EventEnd(cmd);
			return;
		}

		XMMATRIX M;
		if (params.typeFlag == SCREEN)
		{
			M =
				XMMatrixScaling(params.scale.x*params.siz.x, params.scale.y*params.siz.y, 1)
				* XMMatrixRotationZ(params.rotation)
				* XMMatrixTranslation(params.pos.x, params.pos.y, 0)
				* device->GetScreenProjection()
				;
		}
		else if (params.typeFlag == WORLD)
		{
			XMMATRIX faceRot = XMMatrixIdentity();
			if (params.lookAt.w)
			{
				XMVECTOR vvv = (params.lookAt.x == 1 && !params.lookAt.y && !params.lookAt.z) ? XMVectorSet(0, 1, 0, 0) : XMVectorSet(1, 0, 0, 0);
				faceRot =
					XMMatrixLookAtLH(XMVectorSet(0, 0, 0, 0)
						, XMLoadFloat4(&params.lookAt)
						, XMVector3Cross(
							vvv, XMLoadFloat4(&params.lookAt)
						)
					);
			}
			else
			{
				faceRot = XMLoadFloat3x3(&wiRenderer::GetCamera().rotationMatrix);
			}

			XMMATRIX view = wiRenderer::GetCamera().GetView();
			XMMATRIX projection = wiRenderer::GetCamera().GetProjection();
			// Remove possible jittering from temporal camera:
			projection.r[2] = XMVectorSetX(projection.r[2], 0);
			projection.r[2] = XMVectorSetY(projection.r[2], 0);

			M =
				XMMatrixScaling(params.scale.x*params.siz.x, -1 * params.scale.y*params.siz.y, 1)
				*XMMatrixRotationZ(params.rotation)
				*faceRot
				*XMMatrixTranslation(params.pos.x, params.pos.y, params.pos.z)
				*view * projection
				;
		}

		for (int i = 0; i < 4; ++i)
		{
			XMVECTOR V = XMVectorSet(params.corners[i].x - params.pivot.x, params.corners[i].y - params.pivot.y, 0, 1);
			V = XMVector2Transform(V, M);
			XMStoreFloat4(&cb.xCorners[i], V);
		}

		const TextureDesc& desc = texture->GetDesc();
		const float inv_width = 1.0f / float(desc.Width);
		const float inv_height = 1.0f / float(desc.Height);

		if (params.isDrawRectEnabled())
		{
			cb.xTexMulAdd.x = params.drawRect.z * inv_width;	// drawRec.width: mul
			cb.xTexMulAdd.y = params.drawRect.w * inv_height;	// drawRec.heigh: mul
			cb.xTexMulAdd.z = params.drawRect.x * inv_width;	// drawRec.x: add
			cb.xTexMulAdd.w = params.drawRect.y * inv_height;	// drawRec.y: add
		}
		else
		{
			cb.xTexMulAdd = XMFLOAT4(1, 1, 0, 0);	// disabled draw rect
		}
		cb.xTexMulAdd.z += params.texOffset.x * inv_width;	// texOffset.x: add
		cb.xTexMulAdd.w += params.texOffset.y * inv_height;	// texOffset.y: add

		if (params.isDrawRect2Enabled())
		{
			cb.xTexMulAdd2.x = params.drawRect2.z * inv_width;	// drawRec.width: mul
			cb.xTexMulAdd2.y = params.drawRect2.w * inv_height;	// drawRec.heigh: mul
			cb.xTexMulAdd2.z = params.drawRect2.x * inv_width;	// drawRec.x: add
			cb.xTexMulAdd2.w = params.drawRect2.y * inv_height;	// drawRec.y: add
		}
		else
		{
			cb.xTexMulAdd2 = XMFLOAT4(1, 1, 0, 0);	// disabled draw rect
		}
		cb.xTexMulAdd2.z += params.texOffset2.x * inv_width;	// texOffset.x: add
		cb.xTexMulAdd2.w += params.texOffset2.y * inv_height;	// texOffset.y: add

		device->UpdateBuffer(&constantBuffer, &cb, cmd);

		// Determine relevant image rendering pixel shader:
		IMAGE_SHADER targetShader;
		bool NormalmapSeparate = params.isExtractNormalMapEnabled();
		bool Mask = params.maskMap != nullptr;
		if (NormalmapSeparate)
		{
			targetShader = IMAGE_SHADER_SEPARATENORMALMAP;
		}
		else
		{
			if (Mask)
			{
				targetShader = IMAGE_SHADER_MASKED;
			}
			else
			{
				targetShader = IMAGE_SHADER_STANDARD;
			}
		}

		device->BindPipelineState(&imagePSO[targetShader][params.blendFlag][params.stencilComp][params.stencilRefMode][params.isHDREnabled()][sampling_type], cmd);

		device->BindConstantBuffer(VS, &constantBuffer, CB_GETBINDSLOT(ImageCB), cmd);
		device->BindConstantBuffer(PS, &constantBuffer, CB_GETBINDSLOT(ImageCB), cmd);

		device->BindResource(PS, params.maskMap, TEXSLOT_ONDEMAND1, cmd);

		device->Draw(4, 0, cmd);

		device->EventEnd(cmd);
	}


	void LoadShaders()
	{
		std::string path = wiRenderer::GetShaderPath();

		vertexShader = static_cast<const VertexShader*>(wiResourceManager::GetShaderManager().add(path + "imageVS.cso", wiResourceManager::VERTEXSHADER));
		screenVS = static_cast<const VertexShader*>(wiResourceManager::GetShaderManager().add(path + "screenVS.cso", wiResourceManager::VERTEXSHADER));

		imagePS[IMAGE_SHADER_STANDARD][IMAGE_SAMPLING_SIMPLE] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_SEPARATENORMALMAP][IMAGE_SAMPLING_SIMPLE] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS_separatenormalmap.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_MASKED][IMAGE_SAMPLING_SIMPLE] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS_masked.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_FULLSCREEN][IMAGE_SAMPLING_SIMPLE] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "screenPS.cso", wiResourceManager::PIXELSHADER));

		imagePS[IMAGE_SHADER_STANDARD][IMAGE_SAMPLING_BICUBIC] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS_bicubic.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_SEPARATENORMALMAP][IMAGE_SAMPLING_BICUBIC] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS_separatenormalmap_bicubic.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_MASKED][IMAGE_SAMPLING_BICUBIC] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "imagePS_masked_bicubic.cso", wiResourceManager::PIXELSHADER));
		imagePS[IMAGE_SHADER_FULLSCREEN][IMAGE_SAMPLING_BICUBIC] = static_cast<const PixelShader*>(wiResourceManager::GetShaderManager().add(path + "screenPS_bicubic.cso", wiResourceManager::PIXELSHADER));


		GraphicsDevice* device = wiRenderer::GetDevice();

		for (int i = 0; i < IMAGE_SHADER_COUNT; ++i)
		{
			PipelineStateDesc desc;
			desc.vs = vertexShader;
			if (i == IMAGE_SHADER_FULLSCREEN)
			{
				desc.vs = screenVS;
			}
			desc.rs = &rasterizerState;
			desc.pt = TRIANGLESTRIP;

			for (int l = 0; l < IMAGE_SAMPLING_COUNT; ++l)
			{
				desc.ps = imagePS[i][l];

				for (int j = 0; j < BLENDMODE_COUNT; ++j)
				{
					desc.bs = &blendStates[j];
					for (int k = 0; k < STENCILMODE_COUNT; ++k)
					{
						for (int m = 0; m < STENCILREFMODE_COUNT; ++m)
						{
							desc.dss = &depthStencilStates[k][m];

							if (k == STENCILMODE_DISABLED)
							{
								desc.DSFormat = FORMAT_UNKNOWN;
							}
							else
							{
								desc.DSFormat = wiRenderer::DSFormat_full;
							}

							desc.numRTs = 1;

							desc.RTFormats[0] = device->GetBackBufferFormat();
							device->CreatePipelineState(&desc, &imagePSO[i][j][k][m][0][l]);

							desc.RTFormats[0] = wiRenderer::RTFormat_hdr;
							device->CreatePipelineState(&desc, &imagePSO[i][j][k][m][1][l]);

						}
					}
				}
			}
		}


	}

	void Initialize()
	{
		GraphicsDevice* device = wiRenderer::GetDevice();

		{
			GPUBufferDesc bd;
			bd.Usage = USAGE_DYNAMIC;
			bd.ByteWidth = sizeof(ImageCB);
			bd.BindFlags = BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = CPU_ACCESS_WRITE;
			HRESULT hr = device->CreateBuffer(&bd, nullptr, &constantBuffer);
			assert(SUCCEEDED(hr));
		}

		RasterizerStateDesc rs;
		rs.FillMode = FILL_SOLID;
		rs.CullMode = CULL_NONE;
		rs.FrontCounterClockwise = false;
		rs.DepthBias = 0;
		rs.DepthBiasClamp = 0;
		rs.SlopeScaledDepthBias = 0;
		rs.DepthClipEnable = false;
		rs.MultisampleEnable = false;
		rs.AntialiasedLineEnable = false;
		device->CreateRasterizerState(&rs, &rasterizerState);




		for (int i = 0; i < STENCILREFMODE_COUNT; ++i)
		{
			DepthStencilStateDesc dsd;
			dsd.DepthEnable = false;
			dsd.StencilEnable = false;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_DISABLED][i]);

			dsd.StencilEnable = true;
			switch (i)
			{
			case STENCILREFMODE_ENGINE:
				dsd.StencilReadMask = STENCILREF_MASK_ENGINE;
				break;
			case STENCILREFMODE_USER:
				dsd.StencilReadMask = STENCILREF_MASK_USER;
				break;
			default:
				dsd.StencilReadMask = STENCILREF_MASK_ALL;
				break;
			}
			dsd.StencilWriteMask = 0;
			dsd.FrontFace.StencilPassOp = STENCIL_OP_KEEP;
			dsd.FrontFace.StencilFailOp = STENCIL_OP_KEEP;
			dsd.FrontFace.StencilDepthFailOp = STENCIL_OP_KEEP;
			dsd.BackFace.StencilPassOp = STENCIL_OP_KEEP;
			dsd.BackFace.StencilFailOp = STENCIL_OP_KEEP;
			dsd.BackFace.StencilDepthFailOp = STENCIL_OP_KEEP;

			dsd.FrontFace.StencilFunc = COMPARISON_EQUAL;
			dsd.BackFace.StencilFunc = COMPARISON_EQUAL;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_EQUAL][i]);

			dsd.FrontFace.StencilFunc = COMPARISON_LESS;
			dsd.BackFace.StencilFunc = COMPARISON_LESS;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_LESS][i]);

			dsd.FrontFace.StencilFunc = COMPARISON_LESS_EQUAL;
			dsd.BackFace.StencilFunc = COMPARISON_LESS_EQUAL;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_LESSEQUAL][i]);

			dsd.FrontFace.StencilFunc = COMPARISON_GREATER;
			dsd.BackFace.StencilFunc = COMPARISON_GREATER;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_GREATER][i]);

			dsd.FrontFace.StencilFunc = COMPARISON_GREATER_EQUAL;
			dsd.BackFace.StencilFunc = COMPARISON_GREATER_EQUAL;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_GREATEREQUAL][i]);

			dsd.FrontFace.StencilFunc = COMPARISON_NOT_EQUAL;
			dsd.BackFace.StencilFunc = COMPARISON_NOT_EQUAL;
			device->CreateDepthStencilState(&dsd, &depthStencilStates[STENCILMODE_NOT][i]);
		}


		BlendStateDesc bd;
		bd.RenderTarget[0].BlendEnable = true;
		bd.RenderTarget[0].SrcBlend = BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = BLEND_ONE;
		bd.RenderTarget[0].BlendOpAlpha = BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = COLOR_WRITE_ENABLE_ALL;
		bd.IndependentBlendEnable = false;
		device->CreateBlendState(&bd, &blendStates[BLENDMODE_ALPHA]);

		bd.RenderTarget[0].BlendEnable = true;
		bd.RenderTarget[0].SrcBlend = BLEND_ONE;
		bd.RenderTarget[0].DestBlend = BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = BLEND_ONE;
		bd.RenderTarget[0].BlendOpAlpha = BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = COLOR_WRITE_ENABLE_ALL;
		bd.IndependentBlendEnable = false;
		device->CreateBlendState(&bd, &blendStates[BLENDMODE_PREMULTIPLIED]);

		bd.RenderTarget[0].BlendEnable = false;
		bd.RenderTarget[0].RenderTargetWriteMask = COLOR_WRITE_ENABLE_ALL;
		bd.IndependentBlendEnable = false;
		device->CreateBlendState(&bd, &blendStates[BLENDMODE_OPAQUE]);

		bd.RenderTarget[0].BlendEnable = true;
		bd.RenderTarget[0].SrcBlend = BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = BLEND_ONE;
		bd.RenderTarget[0].BlendOp = BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = BLEND_ZERO;
		bd.RenderTarget[0].DestBlendAlpha = BLEND_ONE;
		bd.RenderTarget[0].BlendOpAlpha = BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = COLOR_WRITE_ENABLE_ALL;
		bd.IndependentBlendEnable = false;
		device->CreateBlendState(&bd, &blendStates[BLENDMODE_ADDITIVE]);

		LoadShaders();

		wiBackLog::post("wiImage Initialized");
		initialized.store(true);
	}

}
