#include "objectHF.hlsli"
#include "voxelHF.hlsli"

RWRAWBUFFER(voxelstats, 0);
RWSTRUCTUREDBUFFER(output, VoxelType, 1);

void main(float4 pos : SV_POSITION, float3 N : NORMAL, float2 tex : TEXCOORD, float3 P : POSITION3D, nointerpolation float3 instanceColor : COLOR)
{
	N = normalize(N);

	float3 diff = (P - g_xWorld_VoxelRadianceDataCenter) / g_xWorld_VoxelRadianceDataRes / g_xWorld_VoxelRadianceDataSize;
	float3 uvw = diff * float3(0.5f, -0.5f, 0.5f) + 0.5f;
	uint3 writecoord = floor(uvw * g_xWorld_VoxelRadianceDataRes);

	[branch]
	if (writecoord.x > 0 && writecoord.x < g_xWorld_VoxelRadianceDataRes
		&& writecoord.y > 0 && writecoord.y < g_xWorld_VoxelRadianceDataRes
		&& writecoord.z > 0 && writecoord.z < g_xWorld_VoxelRadianceDataRes)
	{
		float4 baseColor = DEGAMMA(g_xMat_baseColor * float4(instanceColor, 1) * xBaseColorMap.Sample(sampler_linear_wrap, tex));
		float4 color = baseColor;
		float emissive = g_xMat_emissive;

		float3 diffuse = 0;

		uint lightIndexStart = (uint)g_xColor.x;
		uint lightCount = (uint)g_xColor.y;
		for (uint i = lightIndexStart; i < lightCount; ++i)
		{
			ShaderEntityType light = EntityArray[i];

			LightingResult result = (LightingResult)0;

			switch (light.type)
			{
			case ENTITY_TYPE_DIRECTIONALLIGHT:
			{
				float3 L = light.directionWS;

				float3 lightColor = light.GetColor().rgb * light.energy * max(dot(N, L), 0);

				[branch]
				if (light.additionalData_index >= 0)
				{
					float4 ShPos = mul(float4(P, 1), MatrixArray[light.additionalData_index + 0]);
					ShPos.xyz /= ShPos.w;
					float3 ShTex = ShPos.xyz*float3(1, -1, 1) / 2.0f + 0.5f;

					[branch]if ((saturate(ShTex.x) == ShTex.x) && (saturate(ShTex.y) == ShTex.y) && (saturate(ShTex.z) == ShTex.z))
					{
						lightColor *= shadowCascade(ShPos, ShTex.xy, light.shadowKernel, light.shadowBias, light.additionalData_index + 0);
					}
				}

				result.diffuse = lightColor;
			}
			break;
			case ENTITY_TYPE_POINTLIGHT:
			{
				float3 L = light.positionWS - P;
				float dist = length(L);

				[branch]
				if (dist < light.range)
				{
					L /= dist;

					float att = (light.energy * (light.range / (light.range + 1 + dist)));
					float attenuation = (att * (light.range - dist) / light.range);

					float3 lightColor = light.GetColor().rgb * light.energy * max(dot(N, L), 0) * attenuation;

					[branch]
					if (light.additionalData_index >= 0) {
						lightColor *= texture_shadowarray_cube.SampleCmpLevelZero(sampler_cmp_depth, float4(-L, light.additionalData_index), 1 - dist / light.range * (1 - light.shadowBias)).r;
					}

					result.diffuse = lightColor;
				}
			}
			break;
			case ENTITY_TYPE_SPOTLIGHT:
			{
				float3 L = light.positionWS - P;
				float dist = length(L);

				[branch]
				if (dist < light.range)
				{
					L /= dist;

					float SpotFactor = dot(L, light.directionWS);
					float spotCutOff = light.coneAngleCos;

					[branch]
					if (SpotFactor > spotCutOff)
					{
						float att = (light.energy * (light.range / (light.range + 1 + dist)));
						float attenuation = (att * (light.range - dist) / light.range);
						attenuation *= saturate((1.0 - (1.0 - SpotFactor) * 1.0 / (1.0 - spotCutOff)));

						float3 lightColor = light.GetColor().rgb * light.energy * max(dot(N, L), 0) * attenuation;

						[branch]
						if (light.additionalData_index >= 0)
						{
							float4 ShPos = mul(float4(P, 1), MatrixArray[light.additionalData_index + 0]);
							ShPos.xyz /= ShPos.w;
							float2 ShTex = ShPos.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
							[branch]
							if ((saturate(ShTex.x) == ShTex.x) && (saturate(ShTex.y) == ShTex.y))
							{
								lightColor *= shadowCascade(ShPos, ShTex.xy, light.shadowKernel, light.shadowBias, light.additionalData_index);
							}
						}

						result.diffuse = lightColor;
					}
				}
			}
			break;
			}

			diffuse += result.diffuse;
		}

		color.rgb *= diffuse + GetAmbientColor(); // should ambient light bounce?
		
		OBJECT_PS_EMISSIVE

		uint color_encoded = EncodeColor(color);
		uint normal_encoded = EncodeNormal(N);

		// output:
		uint id = flatten3D(writecoord, g_xWorld_VoxelRadianceDataRes);
		//InterlockedMax(output[id].colorMask, color_encoded);
		//InterlockedMax(output[id].normalMask, normal_encoded);

		uint voxelcount;
		voxelstats.InterlockedAdd(4 * 3, 1, voxelcount);
		output[voxelcount].coordFlattened = id;
		output[voxelcount].colorMask = color_encoded;
		output[voxelcount].normalMask = normal_encoded;
	}
}