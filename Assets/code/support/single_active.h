#pragma once
#include <memory>

//全局访问模板类（同一个类只有一个对象能激活）
template<class T> class SingleActive
{
	public:
		static void ResetActive();//取消激活
		virtual void SetAsActive();//激活
		bool IsActive();//是否激活
		
		static std::unique_ptr<T> &Active();//返回单例，可以在全局访问

	protected:
		static std::unique_ptr<T> current;//指针
		~SingleActive();
};

template<class T>
std::unique_ptr<T> SingleActive<T>::current = nullptr;

template<class T>
SingleActive<T>::~SingleActive()
{
	//如果该对象为激活对象，释放指针的控制权；已由外部删除，莫要多管闲事
	if (static_cast<T *>(this) == current.get())
	{
		current.release();
	}
}

template <class T>
void SingleActive<T>::ResetActive()
{
	current.release();
	current.reset(static_cast<T *>(nullptr));
}

template<class T>
void SingleActive<T>::SetAsActive()
{
	current.release();
	current.reset(static_cast<T *>(this));
}

template <class T>
bool SingleActive<T>::IsActive()
{
	return current.get() == static_cast<T *>(this);
}

template<class T>
std::unique_ptr<T> &SingleActive<T>::Active()
{
	return current;
}
