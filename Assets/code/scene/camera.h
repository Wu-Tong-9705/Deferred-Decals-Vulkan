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
const float YAW = -90.0f;//ƫ����
const float PITCH = 0.0f;//������
const float SPEED = 0.005f;//����ƶ��ٶ�
const float SENSITIVITY = 0.1f;//���ת�����ж�
const float ZOOM = 45.0f;//���ű���

class Camera : public SingleActive<Camera>
{
public:
	vec3 m_position;//���λ��
	vec3 m_front;//�۲�ռ��-z�ᳯ��
	vec3 m_up;//�۲�ռ��y�ᳯ��
	vec3 m_right;//�۲�ռ��x�ᳯ��
	vec3 m_world_up;//����ռ��y�ᳯ��
	
	float m_yaw;//ƫ����
	float m_pitch;//������
	float m_movement_speed;//����ƶ��ٶ�
	float m_mouse_sensitivity;//���ת�����ж�
	float m_zoom;//���ű���

	Camera(vec3 position = vec3(0.0f, 0.0f, 0.0f), vec3 up = vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH);

	Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

	//���ع۲����
	mat4 GetViewMatrix();

	//����ͶӰ����
	mat4 GetProjMatrix();

	//��������¼������������λ��ֵ
	void ProcessKeyboard(Camera_Movement direction, float deltaTime);

	//��������ƶ��¼������������ƫ���Ǻ͸�����
	void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

	//�����������¼����������ű���
	void ProcessMouseScroll(float yoffset);

private:
	//ͨ��ƫ���Ǻ͸����Ǽ����������
	void updateCameraVectors();
};
