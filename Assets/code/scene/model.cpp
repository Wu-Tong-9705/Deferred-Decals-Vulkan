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
	//从文件中读取数据
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, 0);
	uint32 flags = 
		aiProcess_CalcTangentSpace |
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_MakeLeftHanded |
		aiProcess_RemoveRedundantMaterials |
		aiProcess_FlipUVs |
		aiProcess_FlipWindingOrder |
		aiProcess_PreTransformVertices |
		aiProcess_OptimizeMeshes;
	scene = importer.ApplyPostProcessing(flags);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
		return;
	}
	m_directory = path.substr(0, path.find_last_of('/'));

	//加载材质
	for (unsigned int i = 0; i < scene->mNumMaterials; i++)
	{
		m_materials.push_back(make_shared<Material>(i));
		m_materials.back()->set_texture(load_texture_for_material(scene->mMaterials[i], aiTextureType_DIFFUSE), aiTextureType_DIFFUSE);
		m_materials.back()->set_texture(load_texture_for_material(scene->mMaterials[i], aiTextureType_HEIGHT), aiTextureType_HEIGHT);
		m_materials.back()->set_texture(load_texture_for_material(scene->mMaterials[i], aiTextureType_SHININESS, i), aiTextureType_SHININESS);
		m_materials.back()->set_texture(load_texture_for_material(scene->mMaterials[i], aiTextureType_AMBIENT), aiTextureType_AMBIENT);
	}
	init_texture_indices();

	//加载网格
	for (int i = 0; i < scene->mNumMeshes; i++)
	{
		m_meshes.push_back(make_shared<Mesh>(scene->mMeshes[i], i));
		m_meshes.back()->set_material(m_materials[scene->mMeshes[i]->mMaterialIndex]);
	}

}

shared_ptr<Texture> Model::load_texture_for_material(aiMaterial* mat, aiTextureType type, unsigned int id)
{
	//获取该贴图的文件路径
	aiString str;
	if (type == aiTextureType_SHININESS)//模型原因，assimp无法读取？
	{
		static const char* SponzaRoughnessMaps[] = {
			"Sponza_Thorn_roughness.png",
			"VasePlant_roughness.png",
			"VaseRound_roughness.png",
			"Background_Roughness.png",
			"Sponza_Bricks_a_Roughness.png",
			"Sponza_Arch_roughness.png",
			"Sponza_Ceiling_roughness.png",
			"Sponza_Column_a_roughness.png",
			"Sponza_Floor_roughness.png",
			"Sponza_Column_c_roughness.png",
			"Sponza_Details_roughness.png",
			"Sponza_Column_b_roughness.png",
			"",
			"Sponza_FlagPole_roughness.png",
			"",
			"",
			"",
			"",
			"",
			"",
			"ChainTexture_Roughness.png",
			"VaseHanging_roughness.png",
			"Vase_roughness.png",
			"Lion_Roughness.png",
			"Sponza_Roof_roughness.png"
		};
		str.Set(SponzaRoughnessMaps[id]);
	}
	else
	{
		mat->GetTexture(type, 0, &str);
	}
	if (_access(((m_directory + "/" )+ str.C_Str()).c_str(), 0) == -1 || str.length == 0)
	{
		switch (type)
		{
			case aiTextureType_DIFFUSE:
				strcpy_s(str.data, "DefaultAlbedo.png");
				break;
			case aiTextureType_HEIGHT:
				strcpy_s(str.data, "DefaultNormal.png");
				break;
			case aiTextureType_SHININESS:
				strcpy_s(str.data, "DefaultRoughness.png");
				break;
			case aiTextureType_AMBIENT:
				strcpy_s(str.data, "DefaultMetallic.png");
				break;
		}
	}

	//检查该纹理是否已经加载（与已加载纹理比对文件路径）
	for (int i = 0; i < m_textures.size(); i++)
	{
		if (strcmp(m_textures[i]->get_path(), str.C_Str()) == 0)
		{
			return m_textures[i];
		}
	}
	
	//未加载，先创建该纹理
	m_textures.push_back(make_shared<Texture>(str.C_Str(), this->m_directory, m_textures.size()));
	return m_textures.back();
}

int Model::get_texture_num()
{
	return m_textures.size();
}

int Model::get_material_num()
{
	return m_materials.size();
}


void Model::add_combined_image_samplers()
{
	for (int i = 0; i < m_textures.size(); i++)
	{
		m_textures[i]->add_combined_image_sampler();
	}
}

void Model::init_texture_indices()
{
	for (int i = 0; i < m_materials.size(); i++)
	{
		TextureIndicesUniform texure_indices;
		texure_indices.albedo = m_materials[i]->get_texture_id(aiTextureType_DIFFUSE);
		texure_indices.normal = m_materials[i]->get_texture_id(aiTextureType_HEIGHT);
		texure_indices.roughness = m_materials[i]->get_texture_id(aiTextureType_SHININESS);
		texure_indices.metallic = m_materials[i]->get_texture_id(aiTextureType_AMBIENT);
		m_texture_indices_uniform_data.push_back(texure_indices);
	}
}

vector<TextureIndicesUniform>* Model::get_texture_indices()
{
	return &m_texture_indices_uniform_data;
}

void Model::draw(PrimaryCommandBuffer* cmd_buffer_ptr)
{
	for (int i = 0; i < m_meshes.size(); i++)
	{
		m_meshes[i]->draw(cmd_buffer_ptr);
	}
}
