#pragma once
class Key
{
public:

	/** 构造函数 */
	Key();

	/** 清空所有的按键信息 */
	void Clear();

	/** 判断某个键是否按下 */
	bool IsPressed(KeyID key);

	/** 设置某个键被按下 */
	void SetPressed(KeyID key);

	/** 设置某个键被释放 */
	void SetReleased(KeyID key);

private:
	bool m_key_down[256]; /**< 保存256个按键的状态 */
};
