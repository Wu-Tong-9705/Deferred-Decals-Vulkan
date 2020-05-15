#pragma once
class Mouse
{

public:

	Mouse()
	{
		m_LButton = false;
		m_RButton = false;
	};

	void click(bool isLeft = true)
	{
		if(isLeft) m_LButton = true;
		else m_RButton = true;
	}

	bool isClick(bool isLeft = true)
	{
		if(isLeft) return m_LButton;
		else return m_RButton;
	}

	void release(bool isLeft = true)
	{
		if (isLeft) m_LButton = false;
		else m_RButton = false;
	}

private:
	bool m_LButton;
	bool m_RButton;
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
