#include "stdafx.h"
#include "settings.h"

void Settings::reset()
{
    for (map<ParamType, Param>::iterator itr = m_params.begin(); itr != m_params.end(); itr++) 
    {
        itr->second.reset();
    }
}

float Settings::getParam(ParamType paramType)
{
    return m_params[paramType].getValue();
}

void Settings::update(ParamType paramType, Direction direction, float deltaTime)
{
    switch (direction)
    {
        case Direction::UP:
            m_params[paramType].up(deltaTime);
            break;
        case Direction::DOWN:
            m_params[paramType].down(deltaTime);
            break;
    }
}



DecalSettings::DecalSettings()
    : m_current_decal_id (0),
      m_current_decal_type(0),
      m_is_show_cursor_decal (false),
      m_is_show_all_decals (true)
{
    m_params[ParamType::DECAL_SCALE_X] = Param(0.0001f, 0.001f, 0.01f);
    m_params[ParamType::DECAL_SCALE_Y] = Param(0.0001f, 0.001f, 0.01f);
    m_params[ParamType::DECAL_THICKNESS] = Param(0.01f, 0.125f, 1.0f);
    m_params[ParamType::DECAL_ROTATION] = Param(-3.14f, 0.0f, 3.14f);
    m_params[ParamType::DECAL_ANGLE_FADE] = Param(-1.0f, 0.5f, 1.0f);
    m_params[ParamType::DECAL_INDENSITY] = Param(0.5f, 1.0f, 1.0f);
    m_params[ParamType::DECAL_ALBEDO] = Param(0.3f, 1.0f, 1.0f);
}

LightSettings::LightSettings()
{
    m_params[ParamType::SUNLIGHT_DIRECTION_X] = Param(-1.0f, 0.5f, 1.0f);
    m_params[ParamType::SUNLIGHT_DIRECTION_Y] = Param(-1.0f, 0.1f, 1.0f);
    m_params[ParamType::SUNLIGHT_DIRECTION_Z] = Param(-1.0f, 0.5f, 1.0f);
    m_params[ParamType::SUNLIGHT_IRRADIANCE_R] = Param(0.0f, 5.0f, 10.0f);
    m_params[ParamType::SUNLIGHT_IRRADIANCE_G] = Param(0.0f, 4.5f, 10.0f);
    m_params[ParamType::SUNLIGHT_IRRADIANCE_B] = Param(0.0f, 4.0f, 10.0f);
}