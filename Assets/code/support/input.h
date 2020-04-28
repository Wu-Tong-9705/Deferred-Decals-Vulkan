#pragma once
class Mouse
{

public:

	Mouse()
	{
		m_LButton = false;
	};

	void click()
	{
		m_LButton = true;
	}

	bool isClick()
	{
		return m_LButton;
	}

	void release()
	{
		m_LButton = false;
	}

private:
	bool m_LButton;
};

class Key
{
public:

	Key()
	{
		Clear();
	}

	void Clear()
	{
		ZeroMemory(&m_key_down, sizeof(m_key_down));
	}

	bool IsPressed(KeyID key)
	{
		return m_key_down[key];
	}

	void SetPressed(KeyID key)
	{
		m_key_down[key] = true;
	}

	void SetReleased(KeyID key)
	{
		m_key_down[key] = false;
	}

private:
	bool m_key_down[256]; /**< 保存256个按键的状态 */
};
