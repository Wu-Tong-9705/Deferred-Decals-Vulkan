#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 0) uniform sampler2D texSampler[];
layout(push_constant) uniform Tex
{
    uint ID;
}tex;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec3 worldTangent;
layout(location = 3) in vec3 worldBitangent;
layout(location = 4) in vec2 texCoord;

layout(location = 0) out vec4 outColor;

void main() 
{
    outColor = texture(texSampler[tex.ID], texCoord);
}