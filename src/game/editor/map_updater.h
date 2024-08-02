#ifndef GAME_EDITOR_MAP_UPDATER_H
#define GAME_EDITOR_MAP_UPDATER_H

#include <base/types.h>
#include <functional>

class CEditor;

/**
 * Map updater class
 * Used to update map files from older version to the current map version
 */
class CMapUpdater
{
	friend class CEditor;
	using TErrorHandler = std::function<void(const char *pErrorMessage)>;

public:
	CMapUpdater();

	/**
	 * Prepares the map updater for a specific map
	 * @param pFilename The map file
	 * @param StorageType The storage type of the loaded file
	 * @param Version The current version of the map file
	 */
	void Prepare(const char *pFilename, int StorageType, int Version);

	/**
	 * Original map file name
	 * @return The filename the original map will be saved as
	 */
	const char *OriginalMapFilename() const
	{
		return m_aOriginalMapFilename;
	}

	/**
	 * Executes the map update process
	 * @param ErrorHandler Error handling function to catch and process errors
	 */
	void Execute(const TErrorHandler &ErrorHandler);

private:
	/* The filename where the original map will be saved */
	char m_aOriginalMapFilename[IO_MAX_PATH_LENGTH];
	/* The map filename to update */
	char m_aFilename[IO_MAX_PATH_LENGTH];
	/* The current version of the map file */
	int m_MapFileVersion;
	/* The storage type used when loading the map file */
	int m_StorageType;

	/* Tells if the updater has been prepared or not */
	bool m_Prepared;

	/* The current editor instance */
	CEditor *m_pEditor;
};

#endif
