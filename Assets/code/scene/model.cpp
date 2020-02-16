#include "stdafx.h"
#include "model.h"

Model::Model(string const& path)
{
	load_model(path);
}

Model::~Model()
{
}

void Model::load_model(string const& path)
{
	//���ļ��ж�ȡ����
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs | aiProcess_CalcTangentSpace);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
		return;
	}
	m_directory = path.substr(0, path.find_last_of('/'));

	//���ز���
	for (unsigned int i = 0; i < scene->mNumMaterials; i++)
	{
		m_materials.push_back(make_shared<Material>(scene->mMaterials[i]));
		m_materials.back()->set_texture(load_texture_for_material(scene->mMaterials[i], aiTextureType_DIFFUSE));
	}

	//��������
	for (int i = 0; i < scene->mNumMeshes; i++)
	{
		m_meshes.push_back(make_shared<Mesh>(scene->mMeshes[i], i));
		m_meshes.back()->set_material(m_materials[scene->mMeshes[i]->mMaterialIndex]);
	}

}

shared_ptr<Texture> Model::load_texture_for_material(aiMaterial* mat, aiTextureType type)
{
	//��ȡ����ͼ���ļ�·��
	aiString str;
	mat->GetTexture(type, 0, &str);
	if (_access(((m_directory + "/" )+ str.C_Str()).c_str(), 0) == -1 || str.length == 0)
	{
		strcpy_s(str.data, "default.png");
	}

	//���������Ƿ��Ѿ����أ����Ѽ�������ȶ��ļ�·����
	for (int i = 0; i < m_textures.size(); i++)
	{
		if (strcmp(m_textures[i]->get_path(), str.C_Str()) == 0)
		{
			return m_textures[i];
		}
	}
	
	//δ���أ��ȴ���������
	m_textures.push_back(make_shared<Texture>(str.C_Str(), this->m_directory));
	return m_textures.back();
}

int Model::get_texture_num()
{
	return m_textures.size();
}

void Model::set_dsg_binding_item(int first)
{
	for (int i = 0; i < m_textures.size(); i++)
	{
		m_textures[i]->set_dsg_binding_item(i + first);
	}
}

void Model::draw(PrimaryCommandBuffer* cmd_buffer_ptr, DescriptorSet* ds_ptr[2], int first, uint32_t data_ub_offset)
{
	for (int i = 0; i < m_meshes.size(); i++)
	{
		m_meshes[i]->draw(cmd_buffer_ptr, ds_ptr, first, data_ub_offset);
	}
}
