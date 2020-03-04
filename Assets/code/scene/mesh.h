#pragma once
#include "stdafx.h"
#include "material.h"

class Mesh
{
public:
	Mesh(const aiMesh* mesh, int i);
	void set_material(shared_ptr<Material> material);
	void draw(PrimaryCommandBuffer* cmd_buffer_ptr);

	~Mesh();

private:
	void load_mesh(const aiMesh* mesh, int i);
	uint32_t m_index_size;
	BufferUniquePtr m_vertex_buffer_ptr;
	BufferUniquePtr m_index_buffer_ptr;
	shared_ptr<Material> m_material;

};

