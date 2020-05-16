#pragma once
#include <stdafx.h>
#include "../support/single_active.h"

//相机能做的运动：前后左右
enum Camera_Movement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT
};

//默认的相机参数
const float YAW = -180.0f;//偏航角
const float PITCH = 0.0f;//俯仰角
const float NEARZ = 0.1f;//近裁剪面
const float FARZ = 35.0f;//远裁剪面
const float SPEED = 5.0f;//相机移动速度
const float SENSITIVITY = 0.1f;//相机转向敏感度
const float ZOOM = 45.0f;//缩放比例

class Camera : public SingleActive<Camera>
{
public:

	Camera(vec3 position = vec3(0.0f, 0.0f, 0.0f), vec3 up = vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);

	Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

	mat4 GetViewMatrix();
	mat4 GetProjMatrix();
	float GetNearZ();
	float GetFarZ();
	float GetYaw();
	float GetPitch();
	float GetZoom();
	vec3 GetNearZCenterWorldPos();

	vec3 GetCameraWorldPos();
	mat3 GetCameraWorldOrientation();


	void ProcessKeyboard(Camera_Movement direction, float deltaTime);
	void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
	void ProcessMouseScroll(float yoffset);

private:
	vec3 m_position;//相机位置
	vec3 m_front;//观察空间的-z轴朝向
	vec3 m_up;//观察空间的y轴朝向
	vec3 m_right;//观察空间的x轴朝向
	vec3 m_world_up;//世界空间的y轴朝向
	
	float m_yaw;//偏航角
	float m_pitch;//俯仰角
	float m_near_z;//近裁剪面
	float m_far_z;//远裁剪面
	float m_movement_speed;//相机移动速度
	float m_mouse_sensitivity;//相机转向敏感度
	float m_zoom;//缩放比例

	mat4 m_viewM;//观察坐标

	//通过偏航角和俯仰角计算相机方向
	void updateCameraVectors();
};
