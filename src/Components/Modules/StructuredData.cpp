#include "STDInclude.hpp"

namespace Components
{
	Utils::Memory::Allocator StructuredData::MemAllocator;

	const char* StructuredData::EnumTranslation[ENUM_MAX] =
	{
		"features",
		"weapons",
		"attachements",
		"challenges",
		"camos",
		"perks",
		"killstreaks",
		"accolades",
		"cardicons",
		"cardtitles",
		"cardnameplates",
		"teams",
		"gametypes"
	};

	void StructuredData::PatchPlayerDataEnum(Game::StructuredDataDef* data, StructuredData::PlayerDataType type, std::vector<std::string>& entries)
	{
		if (!data || type >= StructuredData::PlayerDataType::ENUM_MAX) return;

		Game::StructuredDataEnum* dataEnum = &data->enums[type];

		// Build index-sorted data vector
		std::vector<const char*> dataVector;
		for (int i = 0; i < dataEnum->entryCount; ++i)
		{
			int index = 0;
			for (; index < dataEnum->entryCount; ++index)
			{
				if (dataEnum->entries[index].index == i)
				{
					break;
				}
			}

			dataVector.push_back(dataEnum->entries[index].string);
		}

		// Rebase or add new entries
		for (auto entry : entries)
		{
			const char* value = nullptr;
			for (auto i = dataVector.begin(); i != dataVector.end(); ++i)
			{
				if (*i == entry)
				{
					value = *i;
					dataVector.erase(i);
					Logger::Print("Playerdatadef entry '%s' will be rebased!\n", value);
					break;
				}
			}

			if (!value) value = StructuredData::MemAllocator.duplicateString(entry);
			dataVector.push_back(value);
		}

		// Map data back to the game structure
		Game::StructuredDataEnumEntry* indices = StructuredData::MemAllocator.allocateArray<Game::StructuredDataEnumEntry>(dataVector.size());
		for (unsigned short i = 0; i < dataVector.size(); ++i)
		{
			indices[i].index = i;
			indices[i].string = dataVector[i];
		}

		// Sort alphabetically
		qsort(indices, dataVector.size(), sizeof(Game::StructuredDataEnumEntry), [](void const* first, void const* second)
		{
			const Game::StructuredDataEnumEntry* entry1 = reinterpret_cast<const Game::StructuredDataEnumEntry*>(first);
			const Game::StructuredDataEnumEntry* entry2 = reinterpret_cast<const Game::StructuredDataEnumEntry*>(second);

			return std::string(entry1->string).compare(entry2->string);
		});

		// Apply our patches
		dataEnum->entryCount = dataVector.size();
		dataEnum->entries = indices;
	}

	void StructuredData::PatchCustomClassLimit(Game::StructuredDataDef* data, int count)
	{
		const int customClassSize = 64;

		for (int i = 0; i < data->structs[0].propertyCount; ++i)
		{
			// 3003 is the offset of the customClasses structure
			if (data->structs[0].properties[i].offset >= 3643)
			{
				// -10 because 10 is the default amount of custom classes.
				data->structs[0].properties[i].offset += ((count - 10) * customClassSize);
			}
		}

		// update structure size
		data->size += ((count - 10) * customClassSize);

		// Update amount of custom classes
		data->indexedArrays[5].arraySize = count;
	}

	void StructuredData::PatchAdditionalData(Game::StructuredDataDef* data, std::unordered_map<std::string, std::string>& patches)
	{
		for (auto& item : patches)
		{
			if (item.first == "classes")
			{
				StructuredData::PatchCustomClassLimit(data, atoi(item.second.data()));
			}
		}
	}

	bool StructuredData::UpdateVersionOffsets(Game::StructuredDataDefSet *set, Game::StructuredDataBuffer *buffer, Game::StructuredDataDef *whatever)
	{
		Game::StructuredDataDef* newDef = &set->defs[0];
		Game::StructuredDataDef* oldDef = &set->defs[0];

		for (unsigned int i = 0; i < set->defCount; ++i)
		{
			if (newDef->version < set->defs[i].version)
			{
				newDef = &set->defs[i];
			}

			if (set->defs[i].version == *reinterpret_cast<int*>(buffer->data))
			{
				oldDef = &set->defs[i];
			}
		}

		if (newDef->version >= 159 && oldDef->version <= 158)
		{
			// this should move the data 320 bytes infront
			std::memmove(&buffer->data[3963], &buffer->data[3643], oldDef->size - 3643);
		}

		// StructuredData_UpdateVersion
		return Utils::Hook::Call<bool(void*, void*, void*)>(0x456830)(set, buffer, whatever);
	}

	StructuredData::StructuredData()
	{
		if (Dedicated::IsEnabled()) return;

		// Do not execute this when building zones
		if (!ZoneBuilder::IsEnabled())
		{
			// Correctly upgrade stats
			Utils::Hook(0x42F088, StructuredData::UpdateVersionOffsets, HOOK_CALL).install()->quick();

			// 15 or more custom classes
			Utils::Hook::Set<BYTE>(0x60A2FE, NUM_CUSTOM_CLASSES);

			// Reset empty names
			Command::Add("checkClasses", [](Command::Params*)
			{
				for (int i = 0; i < NUM_CUSTOM_CLASSES; ++i)
				{
					// TODO: Correctly lookup using structured data
					char* className = (reinterpret_cast<char*>(0x1AD3694) - 4 + 3003 + (64 * i) + 0x29);
					if (!*className) strcpy_s(className, 24, Game::SEH_StringEd_GetString(Utils::String::VA("CLASS_SLOT%i", i + 1)));
				}
			});

			return;
		}

		AssetHandler::OnLoad([](Game::XAssetType type, Game::XAssetHeader asset, const std::string& filename, bool* /*restrict*/)
		{
			// Only intercept playerdatadef loading
			if (type != Game::XAssetType::ASSET_TYPE_STRUCTURED_DATA_DEF || filename != "mp/playerdata.def") return;

			// Store asset
			Game::StructuredDataDefSet* data = asset.structuredDataDefSet;
			if (!data) return;

#if 0
			for (size_t i = 0; i < data->defCount; i++)
			{
				auto* defData = &data->defs[i];
				Game::Com_Printf(0, "%d.json\n", defData->version);
				Game::Com_Printf(0, "{\n");
				
				for (int t = 0; t < StructuredData::PlayerDataType::ENUM_MAX; t++)
				{
					Game::Com_Printf(0, "\t\"%s\": [\n", StructuredData::EnumTranslation[t]);

					auto* pEnum = &defData->enums[t];
					for (int j = 0; j < pEnum->entryCount; j++)
					{
						Game::Com_Printf(0, "\t\t\"%s\",\n", pEnum->entries[j].string);
					}

					Game::Com_Printf(0, "\t]\n", defData->version);
				}
				Game::Com_Printf(0, "}\n");
			}
#endif

			if (data->defCount != 1)
			{
				Logger::Error("PlayerDataDefSet contains more than 1 definition!");
				return;
			}

			if (data->defs[0].version != 155)
			{
				Logger::Error("Initial PlayerDataDef is not version 155, patching not possible!");
				return;
			}

			std::map<int, std::vector<std::vector<std::string>>> patchDefinitions;
			std::map<int, std::unordered_map<std::string, std::string>> otherPatchDefinitions;

			// First check if all versions are present
			for (int i = 156;; ++i)
			{
				FileSystem::File definition(Utils::String::VA("%s/%d.json", filename.data(), i));
				if (!definition.exists()) break;

				std::vector<std::vector<std::string>> enumContainer;
				std::unordered_map<std::string, std::string> otherPatches;

				std::string errors;
				json11::Json defData = json11::Json::parse(definition.getBuffer(), errors);

				if (!errors.empty())
				{
					Logger::Error("Parsing patch file '%s' for PlayerDataDef version %d failed: %s", definition.getName().data(), i, errors.data());
					return;
				}

				if (!defData.is_object())
				{
					Logger::Error("PlayerDataDef patch for version %d is invalid!", i);
					return;
				}

				for (unsigned int pType = 0; pType < StructuredData::PlayerDataType::ENUM_MAX; ++pType)
				{
					auto enumData = defData[StructuredData::EnumTranslation[pType]];

					std::vector<std::string> entryData;

					if (enumData.is_array())
					{
						for (auto rawEntry : enumData.array_items())
						{
							if (rawEntry.is_string())
							{
								entryData.push_back(rawEntry.string_value());
							}
						}
					}

					enumContainer.push_back(entryData);
				}

				auto other = defData["other"];

				if (other.is_object())
				{
					for (auto& item : other.object_items())
					{
						if (item.second.is_string())
						{
							otherPatches[item.first] = item.second.string_value();
						}
					}
				}

				patchDefinitions[i] = enumContainer;
				otherPatchDefinitions[i] = otherPatches;
			}

			// Nothing to patch
			if (patchDefinitions.empty()) return;

			// Reallocate the definition
			Game::StructuredDataDef* newData = StructuredData::MemAllocator.allocateArray<Game::StructuredDataDef>(data->defCount + patchDefinitions.size());
			std::memcpy(&newData[patchDefinitions.size()], data->defs, sizeof Game::StructuredDataDef * data->defCount);

			// Prepare the buffers
			for (unsigned int i = 0; i < patchDefinitions.size(); ++i)
			{
				std::memcpy(&newData[i], data->defs, sizeof Game::StructuredDataDef);
				newData[i].version = (patchDefinitions.size() - i) + 155;

				// Reallocate the enum array
				Game::StructuredDataEnum* newEnums = StructuredData::MemAllocator.allocateArray<Game::StructuredDataEnum>(data->defs->enumCount);
				std::memcpy(newEnums, data->defs->enums, sizeof Game::StructuredDataEnum * data->defs->enumCount);
				newData[i].enums = newEnums;
			}

			// Apply new data
			data->defs = newData;
			data->defCount += patchDefinitions.size();

			// Patch the definition
			for (unsigned int i = 0; i < data->defCount; ++i)
			{
				// No need to patch version 155
				if (newData[i].version == 155) continue;

				if (patchDefinitions.find(newData[i].version) != patchDefinitions.end())
				{
					auto patchData = patchDefinitions[newData[i].version];
					auto otherData = otherPatchDefinitions[newData[i].version];

					// Invalid patch data
					if (patchData.size() != StructuredData::PlayerDataType::ENUM_MAX)
					{
						Logger::Error("PlayerDataDef patch for version %d wasn't parsed correctly!", newData[i].version);
						continue;
					}

					// Apply the patch data
					for (unsigned int pType = 0; pType < StructuredData::PlayerDataType::ENUM_MAX; ++pType)
					{
						if (!patchData[pType].empty())
						{
							StructuredData::PatchPlayerDataEnum(&newData[i], static_cast<StructuredData::PlayerDataType>(pType), patchData[pType]);
						}
					}

					StructuredData::PatchAdditionalData(&newData[i], otherData);
				}
			}
		});
	}

	StructuredData::~StructuredData()
	{
		StructuredData::MemAllocator.clear();
	}
}
