#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform Tex
{
	uint ID;
}tex;

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec3 inWorldTangent;
layout(location = 3) in vec3 inWorldBitangent;
layout(location = 4) in vec2 inTexCoord;


layout(location = 0) out vec4 outTangentFrame;
layout(location = 1) out vec4 outUVandDepthGradient;
layout(location = 2) out vec4 outUVGradient;
layout(location = 3) out uint outMaterialID;


vec4 QuatFrom3x3(mat3 m)
{
	mat3 a = transpose(m);
	vec4 q;
	float trace = a[0][0] + a[1][1] + a[2][2];
	if(trace > 0)
	{
		float s = 0.5f / sqrt(trace + 1.0f);
		q.w = 0.25f / s;
		q.x = (a[2][1] - a[1][2]) * s;
		q.y = (a[0][2] - a[2][0]) * s;
		q.z = (a[1][0] - a[0][1]) * s;
	}
	else
	{
		if(a[0][0] > a[1][1] && a[0][0] > a[2][2])
		{
			float s = 2.0f * sqrt(1.0f + a[0][0] - a[1][1] - a[2][2]);
			q.w = (a[2][1] - a[1][2]) / s;
			q.x = 0.25f * s;
			q.y = (a[0][1] + a[1][0]) / s;
			q.z = (a[0][2] + a[2][0]) / s;
		}
		else if(a[1][1] > a[2][2])
		{
			float s = 2.0f * sqrt(1.0f + a[1][1] - a[0][0] - a[2][2]);
			q.w = (a[0][2] - a[2][0]) / s;
			q.x = (a[0][1] + a[1][0]) / s;
			q.y = 0.25f * s;
			q.z = (a[1][2] + a[2][1]) / s;
		}
		else
		{
			float s = 2.0f * sqrt(1.0f + a[2][2] - a[0][0] - a[1][1]);
			q.w = (a[1][0] - a[0][1]) / s;
			q.x = (a[0][2] + a[2][0]) / s;
			q.y = (a[1][2] + a[2][1]) / s;
			q.z = 0.25f * s;
		}
	}
	return q;
}

vec4 PackQuaternion(vec4 q)
{
	vec4 absQ = abs(q);
	float absMaxComponent = max(max(absQ.x, absQ.y), max(absQ.z, absQ.w));

	uint maxCompIdx = 0;
	float maxComponent = q.x;

	for(uint i = 0; i < 4; ++i)
	{
		if(absQ[i] == absMaxComponent)
		{
			maxCompIdx = i;
			maxComponent = q[i];
		}
	}

	if(maxComponent < 0.0f)
		q *= -1.0f;

	vec3 components;
	if(maxCompIdx == 0)
		components = q.yzw;
	else if(maxCompIdx == 1)
		components = q.xzw;
	else if(maxCompIdx == 2)
		components = q.xyw;
	else
		components = q.xyz;

	const float maxRange = 1.0f / sqrt(2.0f);
	components /= maxRange;
	components = components * 0.5f + 0.5f;

	return vec4(components, maxCompIdx / 3.0f);
}

void main() 
{
	vec3 normalWS = normalize(inWorldNormal);
	vec3 tangentWS = normalize(inWorldTangent);
	vec3 bitangentWS = normalize(inWorldBitangent);

	float handedness = dot(bitangentWS, cross(normalWS, tangentWS)) > 0.0f ? 1.0f : -1.0f;
	bitangentWS *= handedness;

	//vec4 tangentFrame = QuatFrom3x3(mat3x3(tangentWS, bitangentWS, normalWS));
	//outTangentFrame = PackQuaternion(tangentFrame);
	outTangentFrame = vec4(normalWS,1.0f);

	outUVandDepthGradient.xy = fract(inTexCoord / 2.0000f);
	outUVandDepthGradient.zw = vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z));
	outUVandDepthGradient.zw = sign(outUVandDepthGradient.zw) * pow(abs(outUVandDepthGradient.zw), vec2(1/2.0f, 1/2.0f));
	outMaterialID = tex.ID & 0x7F;
	if(handedness == -1.0f)
		outMaterialID |= 0x80;

	outUVGradient = vec4(dFdx(inTexCoord), dFdy(inTexCoord));
}