#pragma once
#include <stdafx.h>
#include "../support/single_active.h"

//����������˶���ǰ������
enum Camera_Movement {
	FORWARD,
	BACKWARD,
	LEFT,
	RIGHT
};

//Ĭ�ϵ��������
const float YAW = -180.0f;//ƫ����
const float PITCH = 0.0f;//������
const float NEARZ = 0.1f;//���ü���
const float FARZ = 35.0f;//Զ�ü���
const float SPEED = 5.0f;//����ƶ��ٶ�
const float SENSITIVITY = 0.1f;//���ת�����ж�
const float ZOOM = 45.0f;//���ű���

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
	vec3 m_position;//���λ��
	vec3 m_front;//�۲�ռ��-z�ᳯ��
	vec3 m_up;//�۲�ռ��y�ᳯ��
	vec3 m_right;//�۲�ռ��x�ᳯ��
	vec3 m_world_up;//����ռ��y�ᳯ��
	
	float m_yaw;//ƫ����
	float m_pitch;//������
	float m_near_z;//���ü���
	float m_far_z;//Զ�ü���
	float m_movement_speed;//����ƶ��ٶ�
	float m_mouse_sensitivity;//���ת�����ж�
	float m_zoom;//���ű���

	mat4 m_viewM;//�۲�����

	//ͨ��ƫ���Ǻ͸����Ǽ����������
	void updateCameraVectors();
};
