#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(set = 1, binding = 0) uniform MVP 
{
	mat4 model;
	mat4 view;
	mat4 proj;
} mvp;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec3 worldTangent;
layout(location = 3) out vec3 worldBitangent;
layout(location = 4) out vec2 texCoord;

void main() 
{
	// ����ռ�λ��
	worldPosition = (mvp.model * vec4(inPosition, 1.0)).xyz;

	// �ü��ռ�λ��
	gl_Position = mvp.proj * mvp.view * mvp.model * vec4(inPosition, 1.0);

	// ����ռ䷨��
	worldNormal = normalize(mvp.model * vec4(inNormal, 0.0f)).xyz;

	// ����ռ�����
	worldBitangent = normalize(mvp.model * vec4(inTangent, 0.0f)).xyz;
	
	// ����ռ丱����
	worldBitangent = normalize(mvp.model * vec4(inBitangent, 0.0f)).xyz;

	// ��������
	texCoord = inTexCoord;
}