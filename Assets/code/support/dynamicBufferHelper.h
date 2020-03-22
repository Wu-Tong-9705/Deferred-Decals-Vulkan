#pragma once
#include "misc/buffer_create_info.h"
#include "misc/memory_allocator.h"
using namespace Anvil;

#include <string>
using namespace std;

#define N_SWAPCHAIN_IMAGES (3) 
const uint64_t ALIGNMENT = 256;

template<typename T> class DynamicBufferHelper
{
private:
	BufferUniquePtr           m_uniform_buffer_ptr;
	VkDeviceSize              m_size_per_swapchain_image;
	VkDeviceSize              m_size_total;

public:
	DynamicBufferHelper(BaseDevice* device, string name)
	{
		auto allocator_ptr = MemoryAllocator::create_oneshot(device);

		m_size_per_swapchain_image = Utils::round_up(sizeof(T), ALIGNMENT);
		m_size_total = N_SWAPCHAIN_IMAGES * m_size_per_swapchain_image;

		auto create_info_ptr = BufferCreateInfo::create_no_alloc(
			device,
			m_size_total,
			QueueFamilyFlagBits::GRAPHICS_BIT,
			SharingMode::EXCLUSIVE,
			BufferCreateFlagBits::NONE,
			BufferUsageFlagBits::UNIFORM_BUFFER_BIT);
		m_uniform_buffer_ptr = Buffer::create(move(create_info_ptr));
		m_uniform_buffer_ptr->set_name(name + "unfiorm buffer");

		allocator_ptr->add_buffer(
			m_uniform_buffer_ptr.get(),
			MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
	}

	void update(Queue* queue_ptr, T* data, int n)
	{
		m_uniform_buffer_ptr->write(
			n * m_size_per_swapchain_image, /* start_offset */
			sizeof(T),
			data,
			queue_ptr);
	}

	~DynamicBufferHelper()
	{
		m_uniform_buffer_ptr.reset();
	}

	Buffer* getUniform()
	{
		return m_uniform_buffer_ptr.get();
	}

	VkDeviceSize getSizePerSwapchainImage()
	{
		return m_size_per_swapchain_image;
	}

	VkDeviceSize getSizeTotal()
	{
		return m_size_total;
	}
};