#include "stdafx.h"
#include "keys.h"

/** ���캯�� */
Key::Key()
{ 
	Clear();
}

/** ������еİ�����Ϣ */
void Key::Clear() 
{
	ZeroMemory(&m_key_down, sizeof(m_key_down));
}

/** �ж�ĳ�����Ƿ��� */
bool Key::IsPressed(KeyID key)
{ 
	return m_key_down[key]; 
}

/** ����ĳ���������� */
void Key::SetPressed(KeyID key)
{ 
	m_key_down[key] = true;
}

/** ����ĳ�������ͷ� */
void Key::SetReleased(KeyID key)
{ 
	m_key_down[key] = false;
}
