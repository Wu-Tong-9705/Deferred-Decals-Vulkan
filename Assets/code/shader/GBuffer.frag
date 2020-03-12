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
	//探测四元数中最大的项 
	float fourWSquaredMinusl = m[0][0]+m[1][1]+m[2][2];
	float fourXSquaredMinusl = m[0][0]-m[1][1]-m[2][2];
	float fourYSquaredMinusl = m[1][1]-m[0][0]-m[2][2];
	float fourZSquaredMinusl = m[2][2]-m[0][0]-m[1][1];

	int biggestIndex = 0;
	float fourBiggestSqureMinus1 = fourWSquaredMinusl;
	if(fourXSquaredMinusl>fourBiggestSqureMinus1){
		fourBiggestSqureMinus1 = fourXSquaredMinusl;
		biggestIndex =1;
	} 
	if(fourYSquaredMinusl>fourBiggestSqureMinus1){
		fourBiggestSqureMinus1 = fourYSquaredMinusl;
		biggestIndex =2;
	} 
	if(fourZSquaredMinusl>fourBiggestSqureMinus1){
		fourBiggestSqureMinus1 = fourZSquaredMinusl;
		biggestIndex =3;
	} 

	//计算平方根和除法 
	float biggestVal = sqrt(fourBiggestSqureMinus1+1.0f)*0.5f;
	float mult = 0.25f/biggestVal;

	//计算四元数的值
	vec4 q;
	switch(biggestIndex){
		case 0:
			q.w=biggestVal;
			q.x=(m[1][2]-m[2][1])*mult;
			q.y=(m[2][0]-m[0][2])*mult;
			q.z=(m[0][1]-m[1][0])*mult;
			break;
		case 1:
			q.x = biggestVal;
			q.w =(m[1][2]-m[2][1])*mult;
			q.y =(m[0][1]+m[1][0])*mult;
			q.z =(m[2][0]+m[0][2])*mult;
			break;
		case 2:
			q.y =biggestVal;
			q.w =(m[2][0]-m[0][2])*mult;
			q.x =(m[0][1]+m[1][0])*mult;
			q.z =(m[1][2]+m[2][1])*mult;
			break;
		case 3:
			q.z =biggestVal;
			q.w =(m[0][1]-m[1][0])*mult;
			q.x =(m[2][0]+m[0][2])*mult;
			q.y =(m[1][2]+m[2][1])*mult;
			break;
	}
	return q;
}

vec4 PackQuaternion(vec4 q)
{
	vec3 components = q.xyz;
	components = components * 0.5f + 0.5f;

	return vec4(components, 0.0f);
}

void main() 
{
	vec3 normalWS = normalize(inWorldNormal);
	vec3 bitangentWS = normalize(inWorldBitangent);
	vec3 tangentWS = normalize(inWorldTangent);

	float handedness = dot(bitangentWS, cross(normalWS, tangentWS)) > 0.0f ? 1.0f : -1.0f;
	bitangentWS *= handedness;
	
	vec4 tangentFrame = QuatFrom3x3(mat3(tangentWS, bitangentWS, normalWS));
	outTangentFrame = PackQuaternion(tangentFrame);

	outUVandDepthGradient.xy = fract(inTexCoord / 2.0000f);
	outUVandDepthGradient.zw = vec2(dFdx(gl_FragCoord.z), dFdy(gl_FragCoord.z));
	outUVandDepthGradient.zw = sign(outUVandDepthGradient.zw) * pow(abs(outUVandDepthGradient.zw), vec2(1/2.0f, 1/2.0f));
	outMaterialID = tex.ID & 0x3F;
	if(handedness == -1.0f)
		outMaterialID |= 0x80;
	if(tangentFrame.w < 0.0f)
		outMaterialID |= 0x40;

	outUVGradient = vec4(dFdx(inTexCoord), dFdy(inTexCoord));
}