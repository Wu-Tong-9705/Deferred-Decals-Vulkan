#include "stdafx.h"
#include "camera.h"

Camera::Camera(vec3 position, vec3 up, float yaw, float pitch)
	: m_front(vec3(0.0f, 0.0f, -1.0f)), m_movement_speed(SPEED), m_mouse_sensitivity(SENSITIVITY), m_zoom(ZOOM)
{
	m_position = position;
	m_world_up = up;
	m_yaw = yaw;
	m_pitch = pitch;
	updateCameraVectors();
}

Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch)
	: m_front(vec3(0.0f, 0.0f, -1.0f)), m_movement_speed(SPEED), m_mouse_sensitivity(SENSITIVITY), m_zoom(ZOOM)
{
	m_position = vec3(posX, posY, posZ);
	m_world_up = vec3(upX, upY, upZ);
	m_yaw = yaw;
	m_pitch = pitch;
	updateCameraVectors();
}

mat4 Camera::GetViewMatrix()
{
	return lookAt(m_position, m_position + m_front, m_up);
}

mat4 Camera::GetProjMatrix()
{
	mat4 proj = perspective(radians(m_zoom), Engine::Instance()->getAspect(), 0.1f, 100.0f);
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
