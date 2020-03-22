#pragma once
#define N_DECALS (8)
class Param
{
private:
    float m_value;
    float m_init;
    float m_min;
    float m_max;
    float m_update_speed;

public:
    Param(float min, float init,  float max, float all_time = 5)
        :m_min(min), m_init(init), m_max(max)
    {
        m_value = m_init;
        m_update_speed = (m_max - m_min) / all_time;
    }

    Param() = default;

    void reset()
    {
        m_value = m_init;
    }

    void up(float deltaTime)
    {
        m_value += deltaTime * m_update_speed;
        if (m_value > m_max)
        {
            m_value = m_max;
        }
    }

    void down(float deltaTime)
    {
        m_value -= deltaTime * m_update_speed;
        if (m_value < m_min)
        {
            m_value = m_min;
        }
    }

    float getValue()
    {
        return m_value;
    }
};

enum class ParamType
{
    DECAL_SCALE_X = 0,
    DECAL_SCALE_Y,
    DECAL_THICKNESS,
    DECAL_ROTATION,
    DECAL_ANGLE_FADE,
    DECAL_INDENSITY,
    DECAL_ALBEDO
};

enum class Direction
{
    UP = 0,
    DOWN
};

class AppSettings
{
private:
    map<ParamType, Param> m_params;
    uint m_current_decal_Id;

public:
    AppSettings();
    void reset();
    
    float getParam(ParamType paramType);
    void update(ParamType paramType, Direction direction, float deltaTime);
    
    uint get_decal_id()
    {
        return m_current_decal_Id;
    };
    void set_decal_id(uint id)
    {
        m_current_decal_Id = id;
        reset();
    };
    void tab_decal()
    {
        m_current_decal_Id++;
        if (m_current_decal_Id >= N_DECALS)
        {
            m_current_decal_Id = 0;
        }
    }
};

