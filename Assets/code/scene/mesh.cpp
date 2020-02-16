#include "stdafx.h"
#include "mesh.h"

Mesh::Mesh(const aiMesh* mesh, int i)
{
	load_mesh(mesh, i);
}

void Mesh::load_mesh(const aiMesh* mesh, int i)
{
	auto allocator_ptr = MemoryAllocator::create_oneshot(Engine::Instance()->getDevice());

	#pragma region ��ȡ��������
	vector<Vertex> vertices;
	for (int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;

		vertex.pos.x = mesh->mVertices[i].x;
		vertex.pos.y = mesh->mVertices[i].y;
		vertex.pos.z = mesh->mVertices[i].z;

		//vertex.normal.x = mesh->mNormals[i].x;
		//vertex.normal.y = mesh->mNormals[i].y;
		//vertex.normal.z = mesh->mNormals[i].z;
		
		vertex.normal.x = 0;
		vertex.normal.y = 0;
		vertex.normal.z = 0;

		if (mesh->mTextureCoords[0])
		{
			vertex.texCoord.x = mesh->mTextureCoords[0][i].x;
			vertex.texCoord.y = mesh->mTextureCoords[0][i].y;
		}
		else
		{
			vertex.texCoord = vec2(0.0f, 0.0f);
		}

		vertices.push_back(vertex);
	}
	#pragma endregion

	#pragma region �������㻺��
	VkDeviceSize vertex_buffer_size = sizeof(vertices[0]) * vertices.size();
	auto vertex_buffer_create_info_ptr = BufferCreateInfo::create_no_alloc(
		Engine::Instance()->getDevice(),
		vertex_buffer_size,
		QueueFamilyFlagBits::GRAPHICS_BIT,
		SharingMode::EXCLUSIVE,
		BufferCreateFlagBits::NONE,
		BufferUsageFlagBits::VERTEX_BUFFER_BIT);
	m_vertex_buffer_ptr = Buffer::create(move(vertex_buffer_create_info_ptr));
	m_vertex_buffer_ptr->set_name_formatted("Vertices buffer #%d",i);

	allocator_ptr->add_buffer(
		m_vertex_buffer_ptr.get(),
		Anvil::MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
	#pragma endregion

	#pragma region ��ȡ��������
	vector<uint32_t> indices;
	for (int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}
	m_index_size = indices.size();
	#pragma endregion

	#pragma region ������������
	VkDeviceSize index_buffer_size = sizeof(indices[0]) * indices.size();
	auto index_buffer_create_info_ptr = BufferCreateInfo::create_no_alloc(
		Engine::Instance()->getDevice(),
		index_buffer_size,
		QueueFamilyFlagBits::GRAPHICS_BIT,
		SharingMode::EXCLUSIVE,
		BufferCreateFlagBits::NONE,
		BufferUsageFlagBits::INDEX_BUFFER_BIT);
	m_index_buffer_ptr = Buffer::create(move(index_buffer_create_info_ptr));
	m_index_buffer_ptr->set_name_formatted("Indices buffer #%d", i);

	allocator_ptr->add_buffer(
		m_index_buffer_ptr.get(),
		MemoryFeatureFlagBits::NONE); /* in_required_memory_features */
	#pragma endregion

	#pragma region д�뻺������
	m_vertex_buffer_ptr->write(
		0, /* start_offset */
		vertex_buffer_size,
		vertices.data());

	m_index_buffer_ptr->write(
		0, /* start_offset */
		index_buffer_size,
		indices.data());
	#pragma endregion

}

void Mesh::set_material(shared_ptr<Material> material)
{
	m_material = material;
}

void Mesh::draw(PrimaryCommandBuffer* cmd_buffer_ptr, DescriptorSet* ds_ptr[2], int first, uint32_t data_ub_offset)
{
	ds_ptr[first] = Engine::Instance()->getDsg()->get_descriptor_set(m_material->get_n_set());

	cmd_buffer_ptr->record_bind_descriptor_sets(
		Anvil::PipelineBindPoint::GRAPHICS,
		Engine::Instance()->getPineLine(),
		0, /* firstSet */
		2, /* setCount */
		ds_ptr,
		1,                /* dynamicOffsetCount */
		&data_ub_offset); /* pDynamicOffsets    */

	Anvil::Buffer* buffer_raw_ptrs[] = { m_vertex_buffer_ptr.get() };
	const VkDeviceSize buffer_offsets[] = { 0 };
	cmd_buffer_ptr->record_bind_vertex_buffers(
		0, /* start_binding */
		1, /* binding_count */
		buffer_raw_ptrs,
		buffer_offsets);

	cmd_buffer_ptr->record_bind_index_buffer(
		m_index_buffer_ptr.get(),
		0,
		Anvil::IndexType::UINT32);

	cmd_buffer_ptr->record_draw_indexed(
		static_cast<uint32_t>(m_index_size),
		1,
		0,
		0,
		0);
}

Mesh::~Mesh()
{
	m_vertex_buffer_ptr.reset();
	m_index_buffer_ptr.reset();
}