#pragma once

#include <string>
#include <memory>

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/IO.h>
#include "nanovdb/util/CudaDeviceBuffer.h"

#include "../../Lumen/src/Lumen/Renderer/ILumenResources.h"

struct PTServiceLocator;

class PTVolume : public Lumen::ILumenVolume
{
public:
	PTVolume(PTServiceLocator& a_ServiceLocator);
	PTVolume(std::string a_FilePath, PTServiceLocator& a_ServiceLocator);
	~PTVolume();

	void Load(std::string a_FilePath);

	nanovdb::GridHandle<nanovdb::CudaDeviceBuffer>* GetHandle() { return &m_Handle; };
	
	PTServiceLocator& m_Services;
	
private:
	//TODO: make this a vector to support multiple grids
	nanovdb::GridHandle<nanovdb::CudaDeviceBuffer> m_Handle;
};