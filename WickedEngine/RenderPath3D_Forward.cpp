#include "RenderPath3D_Forward.h"
#include "wiRenderer.h"
#include "wiImage.h"
#include "wiHelper.h"
#include "wiProfiler.h"
#include "wiTextureHelper.h"

using namespace wiGraphics;

void RenderPath3D_Forward::ResizeBuffers()
{
	RenderPath3D::ResizeBuffers();

	GraphicsDevice* device = wiRenderer::GetDevice();

	FORMAT defaultTextureFormat = device->GetBackBufferFormat();


	{
		TextureDesc desc;
		desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
		if (getMSAASampleCount() == 1)
		{
			desc.BindFlags |= BIND_UNORDERED_ACCESS;
		}
		desc.Width = wiRenderer::GetInternalResolution().x;
		desc.Height = wiRenderer::GetInternalResolution().y;
		desc.SampleDesc.Count = getMSAASampleCount();

		desc.Format = wiRenderer::RTFormat_hdr;
		device->CreateTexture2D(&desc, nullptr, &rtMain[0]);
		device->SetName(&rtMain[0], "rtMain[0]");

		desc.Format = wiRenderer::RTFormat_gbuffer_1;
		device->CreateTexture2D(&desc, nullptr, &rtMain[1]);
		device->SetName(&rtMain[1], "rtMain[1]");

		if (getMSAASampleCount() > 1)
		{
			desc.SampleDesc.Count = 1;
			desc.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE | BIND_UNORDERED_ACCESS;

			desc.Format = wiRenderer::RTFormat_hdr;
			device->CreateTexture2D(&desc, nullptr, &rtMain_resolved[0]);
			device->SetName(&rtMain_resolved[0], "rtMain_resolved[0]");

			desc.Format = wiRenderer::RTFormat_gbuffer_1;
			device->CreateTexture2D(&desc, nullptr, &rtMain_resolved[1]);
			device->SetName(&rtMain_resolved[1], "rtMain_resolved[1]");
		}
	}
}

void RenderPath3D_Forward::Render() const
{
	GraphicsDevice* device = wiRenderer::GetDevice();
	wiJobSystem::context ctx;
	CommandList cmd;

	const Texture2D* scene_read[] = { &rtMain[0], &rtMain[1] };
	if (getMSAASampleCount() > 1)
	{
		scene_read[0] = &rtMain_resolved[0];
		scene_read[1] = &rtMain_resolved[1];
	}

	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, cmd] { RenderFrameSetUp(cmd); });
	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, cmd] { RenderShadows(cmd); });
	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, cmd] { RenderReflections(cmd); });

	// Main scene:
	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, device, cmd] {

		wiRenderer::UpdateCameraCB(wiRenderer::GetCamera(), cmd);

		const GPUResource* dsv[] = { &depthBuffer };
		device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_DEPTH_READ, RESOURCE_STATE_DEPTH_WRITE, cmd);

		// depth prepass
		{
			auto range = wiProfiler::BeginRangeGPU("Z-Prepass", cmd);

			device->BindRenderTargets(0, nullptr, &depthBuffer, cmd);
			device->ClearDepthStencil(&depthBuffer, CLEAR_DEPTH | CLEAR_STENCIL, 0, 0, cmd);

			ViewPort vp;
			vp.Width = (float)depthBuffer.GetDesc().Width;
			vp.Height = (float)depthBuffer.GetDesc().Height;
			device->BindViewports(1, &vp, cmd);

			wiRenderer::DrawScene(wiRenderer::GetCamera(), getTessellationEnabled(), cmd, RENDERPASS_DEPTHONLY, getHairParticlesEnabled(), true);

			device->BindRenderTargets(0, nullptr, nullptr, cmd);

			wiProfiler::EndRange(range);
		}

		if (getMSAASampleCount() > 1)
		{
			device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, cmd);
			wiRenderer::ResolveMSAADepthBuffer(depthBuffer_Copy, depthBuffer, cmd);
			device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, RESOURCE_STATE_DEPTH_READ, cmd);
		}
		else
		{
			device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_DEPTH_WRITE, RESOURCE_STATE_COPY_SOURCE, cmd);
			device->CopyTexture2D(&depthBuffer_Copy, &depthBuffer, cmd);
			device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_COPY_SOURCE, RESOURCE_STATE_DEPTH_READ, cmd);
		}

		RenderLinearDepth(cmd);

		RenderSSAO(cmd);
	});

	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, device, cmd, scene_read] {

		wiRenderer::UpdateCameraCB(wiRenderer::GetCamera(), cmd);

		const GPUResource* dsv[] = { &depthBuffer };
		device->TransitionBarrier(dsv, ARRAYSIZE(dsv), RESOURCE_STATE_DEPTH_READ, RESOURCE_STATE_DEPTH_WRITE, cmd);

		// Opaque Scene:
		{
			auto range = wiProfiler::BeginRangeGPU("Opaque Scene", cmd);

			const Texture2D* rts[] = {
				&rtMain[0],
				&rtMain[1],
			};
			device->BindRenderTargets(ARRAYSIZE(rts), rts, &depthBuffer, cmd);
			float clear[] = { 0,0,0,0 };
			device->ClearRenderTarget(rts[1], clear, cmd);

			ViewPort vp;
			vp.Width = (float)rts[0]->GetDesc().Width;
			vp.Height = (float)rts[0]->GetDesc().Height;
			device->BindViewports(1, &vp, cmd);

			device->BindResource(PS, getReflectionsEnabled() ? &rtReflection : wiTextureHelper::getTransparent(), TEXSLOT_RENDERPATH_REFLECTION, cmd);
			device->BindResource(PS, getSSAOEnabled() ? &rtSSAO[0] : wiTextureHelper::getWhite(), TEXSLOT_RENDERPATH_SSAO, cmd);
			device->BindResource(PS, getSSREnabled() ? &rtSSR : wiTextureHelper::getTransparent(), TEXSLOT_RENDERPATH_SSR, cmd);
			wiRenderer::DrawScene(wiRenderer::GetCamera(), getTessellationEnabled(), cmd, RENDERPASS_FORWARD, getHairParticlesEnabled(), true);
			wiRenderer::DrawSky(cmd);

			wiProfiler::EndRange(range); // Opaque Scene
		}
	});

	cmd = device->BeginCommandList();
	wiJobSystem::Execute(ctx, [this, device, cmd, scene_read] {

		wiRenderer::BindCommonResources(cmd);

		RenderSSR(*scene_read[0], rtMain[1], cmd);

		DownsampleDepthBuffer(cmd);

		wiRenderer::UpdateCameraCB(wiRenderer::GetCamera(), cmd);

		RenderOutline(rtMain[0], cmd);

		RenderLightShafts(cmd);

		RenderVolumetrics(cmd);

		RenderParticles(false, cmd);

		RenderRefractionSource(*scene_read[0], cmd);

		RenderTransparents(rtMain[0], RENDERPASS_FORWARD, cmd);

		if (getMSAASampleCount() > 1)
		{
			device->MSAAResolve(scene_read[0], &rtMain[0], cmd);
		}

		RenderParticles(true, cmd);

		TemporalAAResolve(*scene_read[0], *scene_read[1], cmd);

		RenderBloom(*scene_read[0], cmd);

		RenderPostprocessChain(*scene_read[0], *scene_read[1], cmd);

	});

	RenderPath2D::Render();

	wiJobSystem::Wait(ctx);
}
