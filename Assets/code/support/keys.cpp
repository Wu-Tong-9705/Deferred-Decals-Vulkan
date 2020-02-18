#include "stdafx.h"
#include "keys.h"

/** 构造函数 */
Key::Key()
{ 
	Clear();
}

/** 清空所有的按键信息 */
void Key::Clear() 
{
	ZeroMemory(&m_key_down, sizeof(m_key_down));
}

/** 判断某个键是否按下 */
bool Key::IsPressed(KeyID key)
{ 
	return m_key_down[key]; 
}

/** 设置某个键被按下 */
void Key::SetPressed(KeyID key)
{ 
	m_key_down[key] = true;
}

/** 设置某个键被释放 */
void Key::SetReleased(KeyID key)
{ 
	m_key_down[key] = false;
}
