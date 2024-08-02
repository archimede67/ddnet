#include "map_updater.h"

#include <base/system.h>
#include <game/editor/editor.h>

CMapUpdater::CMapUpdater()
{
	m_MapFileVersion = -1;
	m_aOriginalMapFilename[0] = '\0';
	m_aFilename[0] = '\0';
	m_StorageType = 0;
	m_pEditor = nullptr;
}

void CMapUpdater::Prepare(const char *pFilename, int StorageType, int Version)
{
	str_copy(m_aFilename, pFilename);
	m_MapFileVersion = Version;
	m_StorageType = StorageType;

	// Set up the outdated file name
	char aFilename[IO_MAX_PATH_LENGTH];
	fs_split_file_extension(pFilename, aFilename, sizeof(aFilename));

	// Rename to <map file>_v<version>_outdated_<k>.map
	int k = 0;
	do
	{
		str_format(m_aOriginalMapFilename, sizeof(m_aOriginalMapFilename), "%s_v%d_outdated_%d.map", aFilename, m_MapFileVersion, k);
		k++;
	} while(m_pEditor->Storage()->FileExists(m_aOriginalMapFilename, StorageType));

	m_Prepared = true;
}

void CMapUpdater::Execute(const TErrorHandler &ErrorHandler)
{
	if(!m_Prepared)
		ErrorHandler("The map updater needs to be prepared to execute the map updating process");

	printf("Processing map update from %d to %d: saving original to '%s', updating '%s'\n", m_MapFileVersion, CMapItemVersion::CURRENT_VERSION, m_aOriginalMapFilename, m_aFilename);
	m_Prepared = false;
}
