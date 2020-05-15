#include "stdafx.h"
#include "decalSettings.h"

DecalSettings::DecalSettings()
    : m_current_decal_id (0),
      m_current_decal_type(0),
      m_is_show_cursor_decal (true),
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

void DecalSettings::reset()
{
    for (map<ParamType, Param>::iterator itr = m_params.begin(); itr != m_params.end(); itr++) 
    {
        itr->second.reset();
    }
}

float DecalSettings::getParam(ParamType paramType)
{
    return m_params[paramType].getValue();
}

void DecalSettings::update(ParamType paramType, Direction direction, float deltaTime)
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
