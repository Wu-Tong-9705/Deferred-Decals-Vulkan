#version 450

struct Decal
{
	vec4 position;
	vec4 normal;
	vec4 size;
	float rotation;
	float angle_fade;
	float intensity;
	float albedo;
	uint albedoTexIdx;
	uint normalTexIdx;
};

layout( constant_id = 0 ) const int MAX_CLUSTER_NUM = 64;
layout( constant_id = 1 ) const int MODE = 0;

layout(set = 0, binding = 0) uniform MVP 
{
	mat4 model;
	mat4 view;
	mat4 proj;
} mvp;

layout(set = 1, binding = 0) uniform DecalUniform
{
	Decal decals[MAX_CLUSTER_NUM];
}decalUniform;

layout(set = 2, binding = 0) buffer IndexUniform
{
	uint decalIndices[MAX_CLUSTER_NUM];
	uint numIntersectingDecals;
}indexUniform;

layout(location = 0) in vec3 vertexPostion;

layout(location = 0) out uint outDecalIndex;


void main()
{
	if(MODE == 0)
	{
		outDecalIndex = indexUniform.decalIndices[gl_InstanceIndex];
	}
	else
	{
		outDecalIndex = indexUniform.decalIndices[gl_InstanceIndex + indexUniform.numIntersectingDecals];
	}
	
	outDecalIndex = indexUniform.decalIndices[gl_InstanceIndex];
	Decal decal = decalUniform.decals[outDecalIndex];


	vec3 forward = - decal.normal.xyz;
	vec3 up = abs(dot(forward, vec3(0.0f, 1.0f, 0.0f))) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f);
	vec3 right = normalize(cross(up, forward));
	up = cross(forward, right);
	mat3 orientation = mat3(right, up, forward);

	vec3 vtxPos = vertexPostion * decal.size.xyz;
	vtxPos = mat3(cos(decal.rotation), -sin(decal.rotation), 0,
				  sin(decal.rotation), cos(decal.rotation), 0,
				  0, 0, 1) * vtxPos;
	vtxPos = orientation * vtxPos;
	vtxPos += decal.position.xyz;
	gl_Position = mvp.proj * mvp.view * vec4(vtxPos, 1.0f);
}