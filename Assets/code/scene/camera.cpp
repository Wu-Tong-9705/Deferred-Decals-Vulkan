#include "stdafx.h"
#include "camera.h"

Camera::Camera(vec3 position, vec3 up, float yaw, float pitch)
	: m_front(vec3(0.0f, 0.0f, -1.0f)), m_movement_speed(SPEED), m_mouse_sensitivity(SENSITIVITY), m_zoom(ZOOM),
	  m_near_z(NEARZ),m_far_z(FARZ)
{
	m_position = position;
	m_world_up = up;
	m_yaw = yaw;
	m_pitch = pitch;
	updateCameraVectors();
}

Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
	: m_front(vec3(0.0f, 0.0f, -1.0f)), m_movement_speed(SPEED), m_mouse_sensitivity(SENSITIVITY), m_zoom(ZOOM),
	  m_near_z(NEARZ), m_far_z(FARZ)
{
	m_position = vec3(posX, posY, posZ);
	m_world_up = vec3(upX, upY, upZ);
	m_yaw = yaw;
	m_pitch = pitch;
	updateCameraVectors();
}

vec3 Camera::GetCameraWorldPos()
{
	return m_position;
}

mat3 Camera::GetCameraWorldOrientation()
{
	mat3 orientation;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			orientation[i][j] = m_viewM[i][j];
		}
	}

	return inverse(orientation);
}

float Camera::GetNearZ()
{
	return m_near_z;
}

float Camera::GetFarZ()
{
	return m_far_z;
}
vec3 Camera::GetNearZCenterWorldPos()
{
	return m_position + m_near_z * m_front;
}

mat4 Camera::GetViewMatrix()
{
	m_viewM = lookAt(m_position, m_position + m_front, m_up);
	return m_viewM;
}

mat4 Camera::GetProjMatrix()
{
	mat4 proj = perspective(radians(m_zoom), Engine::Instance()->getAspect(), m_near_z, m_far_z);
	proj[1][1] *= -1;
	return proj;
}

void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
	float velocity = m_movement_speed * deltaTime;
	if (direction == FORWARD)
		m_position += m_front * velocity;
	if (direction == BACKWARD)
		m_position -= m_front * velocity;
	if (direction == LEFT)
		m_position -= m_right * velocity;
	if (direction == RIGHT)
		m_position += m_right * velocity;
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
	xoffset *= m_mouse_sensitivity;
	yoffset *= m_mouse_sensitivity;

	m_yaw += xoffset;
	m_pitch += yoffset;

	//限制方向突变
	if (constrainPitch)
	{
		if (m_pitch > 89.0f)
			m_pitch = 89.0f;
		if (m_pitch < -89.0f)
			m_pitch = -89.0f;
	}

	//通过偏航角和俯仰角计算相机方向
	updateCameraVectors();
}

void Camera::ProcessMouseScroll(float yoffset)
{
	if (m_zoom >= 1.0f && m_zoom <= 45.0f)
		m_zoom -= yoffset;
	if (m_zoom <= 1.0f)
		m_zoom = 1.0f;
	if (m_zoom >= 45.0f)
		m_zoom = 45.0f;
}

void Camera::updateCameraVectors()
{
	vec3 front;
	front.x = cos(radians(m_yaw)) * cos(radians(m_pitch));
	front.y = sin(radians(m_pitch));
	front.z = sin(radians(m_yaw)) * cos(radians(m_pitch));

	m_front = normalize(front);
	m_right = normalize(cross(m_front, m_world_up));  // Normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
	m_up = normalize(cross(m_right, m_front));
}
