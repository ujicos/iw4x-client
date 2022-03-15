#include "STDInclude.hpp"

namespace Components
{
	namespace
	{
		void ExportMap(Command::Params*)
		{
			Game::clipMap_t* cm = nullptr;
			Game::DB_EnumXAssets(Game::XAssetType::ASSET_TYPE_CLIPMAP_MP, [](Game::XAssetHeader header, void* cm)
			{
				*reinterpret_cast<Game::clipMap_t**>(cm) = header.clipMap;
			}, &cm, false);

			if (cm)
			{
				// cm->brushes[0].sides[0].
			}
			else
			{
				Logger::Print("No map loaded, unable to dump anything!\n");
			}
		}

	}

	MapExport::MapExport()
	{

	}
}