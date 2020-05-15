#pragma once
#define N_DECALS (8)
#define N_DECAL_TYPE (4)
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

class DecalSettings
{
private:
    map<ParamType, Param> m_params;
    uint m_current_decal_type;
    uint m_current_decal_id;
    bool m_is_show_cursor_decal;
    bool m_is_show_all_decals;

public:
    DecalSettings();
    void reset();
    
    float getParam(ParamType paramType);
    void update(ParamType paramType, Direction direction, float deltaTime);
    
    uint get_decal_type()
    {
        return m_current_decal_type;
    };
    void set_decal_type(uint type)
    {
        m_current_decal_type = type;
        m_current_decal_id = 0;
        reset();
    };
    void tab_decal_type()
    {
        m_current_decal_type++;
        m_current_decal_id = 0;
        if (m_current_decal_type >= N_DECAL_TYPE)
        {
            m_current_decal_type = 0;
        }
        reset();
    }

    uint get_decal_id()
    {
        return m_current_decal_id;
    };
    void set_decal_id(uint id)
    {
        m_current_decal_id = id;
        reset();
    };
    void tab_decal()
    {
        m_current_decal_id++;
        if (m_current_decal_id >= N_DECALS)
        {
            m_current_decal_id = 0;
        }
        reset();
    }

    bool get_is_show_cursor_decal()
    {
        return m_is_show_cursor_decal;
    };
    void set_is_show_cursor_decal(bool isShow)
    {
        m_is_show_cursor_decal = isShow;
    };
    bool get_is_show_all_decals()
    {
        return m_is_show_all_decals;
    };
    void set_is_show_all_decals(bool isShow)
    {
        m_is_show_all_decals = isShow;
    };
};

