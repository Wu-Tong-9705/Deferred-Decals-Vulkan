#pragma once
class Key
{
public:

	/** ���캯�� */
	Key();

	/** ������еİ�����Ϣ */
	void Clear();

	/** �ж�ĳ�����Ƿ��� */
	bool IsPressed(KeyID key);

	/** ����ĳ���������� */
	void SetPressed(KeyID key);

	/** ����ĳ�������ͷ� */
	void SetReleased(KeyID key);

private:
	bool m_key_down[256]; /**< ����256��������״̬ */
};
