#include "globals.hlsli"
#include "grassHF_GS.hlsli"
#include "cullingShaderHF.hlsli"

static const uint vertexBuffer_stride = 16 * 3; // pos, normal, tangent 
RAWBUFFER(vertexBuffer, 0);

RWRAWBUFFER(argumentBuffer, 0); // indirect draw args
RWRAWBUFFER(indexBuffer, 1);


[numthreads(GRASS_CULLING_THREADCOUNT_COARSE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	const uint fetchAddress = DTid.x * vertexBuffer_stride;
	float4 pos = float4(asfloat(vertexBuffer.Load3(fetchAddress)), 1);
	pos = mul(pos, xWorld);

	if (distance(g_xFrame_MainCamera_CamPos, pos.xyz) > LOD2)
	{
		return;
	}

	float len = asfloat(vertexBuffer.Load(fetchAddress + 16 + 12));

	const Plane planes[6] = {
		{ g_xFrame_FrustumPlanesWS[0].xyz, -g_xFrame_FrustumPlanesWS[0].w }, // left plane
		{ g_xFrame_FrustumPlanesWS[1].xyz, -g_xFrame_FrustumPlanesWS[1].w }, // right plane
		{ g_xFrame_FrustumPlanesWS[2].xyz, -g_xFrame_FrustumPlanesWS[2].w }, // top plane
		{ g_xFrame_FrustumPlanesWS[3].xyz, -g_xFrame_FrustumPlanesWS[3].w }, // bottom plane
		{ g_xFrame_FrustumPlanesWS[4].xyz, -g_xFrame_FrustumPlanesWS[4].w }, // near plane
		{ g_xFrame_FrustumPlanesWS[5].xyz, -g_xFrame_FrustumPlanesWS[5].w }, // far plane
	};

	const Sphere sphere = { pos.xyz, len };

	if (SphereInsideFrustum(sphere, planes))
	{
		uint prevValue;
		argumentBuffer.InterlockedAdd(0, 1, prevValue); // index count

		indexBuffer.Store(prevValue * 4, DTid.x);
	}
}