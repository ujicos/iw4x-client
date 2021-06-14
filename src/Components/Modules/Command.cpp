#include "STDInclude.hpp"

namespace Components
{
	namespace
	{
		bool CanCheat()
		{
			static Game::dvar_t* sv_cheats = nullptr;

			if (sv_cheats == nullptr)
				sv_cheats = Game::Dvar_FindVar("sv_cheats");

			return sv_cheats->current.enabled;
		}
	}

	std::unordered_map<std::string, Utils::Slot<Command::Callback>> Command::FunctionMap;
	std::unordered_map<std::string, Utils::Slot<Command::Callback>> Command::FunctionMapSV;
	std::unordered_map<std::string, Utils::Slot<Command::CallbackSv>> Command::FunctionMapSVAlt;

	std::string Command::Params::join(size_t startIndex)
	{
		std::string result;

		for (size_t i = startIndex; i < this->length(); ++i)
		{
			if (i > startIndex) result.append(" ");
			result.append(this->operator[](i));
		}

		return result;
	}

	char* Command::Params::operator[](size_t index)
	{
		return this->get(index);
	}

	char* Command::ClientParams::get(size_t index)
	{
		if (index >= this->length()) return const_cast<char*>("");
		return Game::cmd_argv[this->commandId][index];
	}

	size_t Command::ClientParams::length()
	{
		return Game::cmd_argc[this->commandId];
	}

	char* Command::ServerParams::get(size_t index)
	{
		if (index >= this->length()) return const_cast<char*>("");
		return Game::cmd_argv_sv[this->commandId][index];
	}

	size_t Command::ServerParams::length()
	{
		return Game::cmd_argc_sv[this->commandId];
	}

	void Command::Add(const char* name, Utils::Slot<Command::Callback> callback)
	{
		std::string command = Utils::String::ToLower(name);

		if (Command::FunctionMap.find(command) == Command::FunctionMap.end())
		{
			Command::AddRaw(name, Command::MainCallback);
		}

		Command::FunctionMap[command] = callback;
	}

	void Command::AddSV(const char* name, Utils::Slot<Command::Callback> callback)
	{
		if (Loader::IsPregame())
		{
			MessageBoxA(nullptr, "Registering server commands in pregamestate is illegal!", nullptr, MB_ICONERROR);

#ifdef DEBUG
			__debugbreak();
#endif

			return;
		}

		std::string command = Utils::String::ToLower(name);

		if (Command::FunctionMapSV.find(command) == Command::FunctionMapSV.end())
		{
			Command::AddRawSV(name, Command::MainCallbackSV);

			// If the main command is registered as Cbuf_AddServerText, the command will be redirected to the SV handler
			Command::AddRaw(name, Game::Cbuf_AddServerText);
		}

		Command::FunctionMapSV[command] = callback;
	}

	void Command::AddRaw(const char* name, void(*callback)(), bool key)
	{
		Game::Cmd_AddCommand(name, callback, Command::Allocate(), key);
	}

	void Command::AddRawSV(const char* name, void(*callback)())
	{
		Game::Cmd_AddServerCommand(name, callback, Command::Allocate());

		// If the main command is registered as Cbuf_AddServerText, the command will be redirected to the SV handler
		Command::AddRaw(name, Game::Cbuf_AddServerText);
	}

	void Command::Execute(std::string command, bool sync)
	{
		command.append("\n"); // Make sure it's terminated

		if (sync)
		{
			Game::Cmd_ExecuteSingleCommand(0, 0, command.data());
		}
		else
		{
			Game::Cbuf_AddText(0, command.data());
		}
	}

	Game::cmd_function_t* Command::Find(const std::string& command)
	{
		Game::cmd_function_t* cmdFunction = *Game::cmd_functions;

		while (cmdFunction)
		{
			if (cmdFunction->name && cmdFunction->name == command)
			{
				return cmdFunction;
			}

			cmdFunction = cmdFunction->next;
		}

		return nullptr;
	}

	Game::cmd_function_t* Command::Allocate()
	{
		return Utils::Memory::GetAllocator()->allocate<Game::cmd_function_t>();
	}

	void Command::MainCallback()
	{
		Command::ClientParams params(*Game::cmd_id);

		std::string command = Utils::String::ToLower(params[0]);

		if (Command::FunctionMap.find(command) != Command::FunctionMap.end())
		{
			Command::FunctionMap[command](&params);
		}
	}

	void Command::MainCallbackSV()
	{
		Command::ServerParams params(*Game::cmd_id_sv);

		std::string command = Utils::String::ToLower(params[0]);

		if (Command::FunctionMapSV.find(command) != Command::FunctionMapSV.end())
		{
			Command::FunctionMapSV[command](&params);
		}
	}

	void Command::AddSvAlt(const char* name, Utils::Slot<Command::CallbackSv> callback)
	{
		std::string command = Utils::String::ToLower(name);

		if (Command::FunctionMapSVAlt.find(command) == Command::FunctionMapSVAlt.end())
		{
			// If the main command is registered as Cbuf_AddServerText, the command will be redirected to the SV handler
			Command::AddRaw(name, nullptr);
		}

		Command::FunctionMapSVAlt[command] = callback;
	}

	void Command::ClientCommandStub(int clientNum)
	{
		Command::ServerParams params(*Game::cmd_id_sv);
		std::string command = Utils::String::ToLower(params[0]);

		if (Command::FunctionMapSVAlt.find(command) != Command::FunctionMapSVAlt.end())
		{
			Command::FunctionMapSVAlt[command](clientNum, &params);
		}
		else
		{
			// call original function
			const int orig = 0x416790;

			__asm
			{
				push clientNum
				call orig
				add esp, 4
			}
		}
	}

	Command::Command()
	{
		AssertSize(Game::cmd_function_t, 24);

		static int toastDurationShort = 1000;
		static int toastDurationMedium = 2500;
		static int toastDurationLong = 5000;

		// Disable native noclip command
		Utils::Hook::Nop(0x474846, 5);

		Utils::Hook(0x6259FA, Command::ClientCommandStub, HOOK_CALL).install()->quick();

		Command::AddSvAlt("give", [](int clientNum, Command::Params* params)
		{
			if (!CanCheat())
			{
				Game::SV_GameSendServerCommand(clientNum, 1, "f \"Cheats are not enabled on this server\"");
				return;
			}

			if (params->length() < 2)
			{
				Game::SV_GameSendServerCommand(clientNum, 1, "f \"Missing weapon name\"");
				return;
			}

			int weapon = Game::BG_FindWeaponIndexForName(params->get(1));
			auto* ps = Game::g_entities[clientNum].client;
			
			if (Game::G_GivePlayerWeapon(ps, weapon, 0, 0))
			{
				Game::G_InitializeAmmo(&Game::g_entities[clientNum], weapon, 0, 0);

				// Actually G_SelectWeaponIndex but whatever
				Game::SV_GameSendServerCommand(clientNum, 1, Utils::String::VA("a %i", weapon));
			}
		});

		Command::Add("noclip", [](Command::Params*)
		{
			int clientNum = Game::CG_GetClientNum();
			if (!Game::CL_IsCgameInitialized() || clientNum >= 18 || clientNum < 0 || !Game::g_entities[clientNum].client)
			{
				Logger::Print("You are not hosting a match!\n");
				Toast::Show("cardicon_stop", "Error", "You are not hosting a match!", toastDurationMedium);
				return;
			}

			if (!Dvar::Var("sv_cheats").get<bool>())
			{
				Logger::Print("Cheats disabled!\n");
				Toast::Show("cardicon_stop", "Error", "Cheats disabled!", toastDurationMedium);
				return;
			}

			Game::g_entities[clientNum].client->flags ^= Game::PLAYER_FLAG_NOCLIP;

			Logger::Print("Noclip toggled\n");
			Toast::Show("cardicon_abduction", "Success", "Noclip toggled", toastDurationShort);
		});

		Command::Add("ufo", [](Command::Params*)
		{
			int clientNum = Game::CG_GetClientNum();
			if (!Game::CL_IsCgameInitialized() || clientNum >= 18 || clientNum < 0 || !Game::g_entities[clientNum].client)
			{
				Logger::Print("You are not hosting a match!\n");
				Toast::Show("cardicon_stop", "Error", "You are not hosting a match!", toastDurationMedium);
				return;
			}

			if (!Dvar::Var("sv_cheats").get<bool>())
			{
				Logger::Print("Cheats disabled!\n");
				Toast::Show("cardicon_stop", "Error", "Cheats disabled!", toastDurationMedium);
				return;
			}

			Game::g_entities[clientNum].client->flags ^= Game::PLAYER_FLAG_UFO;

			Logger::Print("UFO toggled\n");
			Toast::Show("cardicon_abduction", "Success", "UFO toggled", toastDurationShort);
		});

		Command::Add("setviewpos", [](Command::Params* params)
		{
			int clientNum = Game::CG_GetClientNum();
			if (!Game::CL_IsCgameInitialized() || clientNum >= 18 || clientNum < 0 || !Game::g_entities[clientNum].client)
			{
				Logger::Print("You are not hosting a match!\n");
				Toast::Show("cardicon_stop", "Error", "You are not hosting a match!", toastDurationMedium);
				return;
			}

			if (!Dvar::Var("sv_cheats").get<bool>())
			{
				Logger::Print("Cheats disabled!\n");
				Toast::Show("cardicon_stop", "Error", "Cheats disabled!", toastDurationMedium);
				return;
			}

			if (params->length() != 4 && params->length() != 6)
			{
				Logger::Print("Invalid coordinate specified!\n");
				Toast::Show("cardicon_stop", "Error", "Invalid coordinate specified!", toastDurationMedium);
				return;
			}

			float pos[3] = { 0.0f, 0.0f, 0.0f };
			float orientation[3] = { 0.0f, 0.0f, 0.0f };

			pos[0] = strtof(params->get(1), nullptr);
			pos[1] = strtof(params->get(2), nullptr);
			pos[2] = strtof(params->get(3), nullptr);

			if (params->length() == 6)
			{
				orientation[0] = strtof(params->get(4), nullptr);
				orientation[1] = strtof(params->get(5), nullptr);
			}

			Game::TeleportPlayer(&Game::g_entities[clientNum], pos, orientation);

			// Logging that will spam the console and screen if people use cinematics
			//Logger::Print("Successfully teleported player!\n");
			//Toast::Show("cardicon_abduction", "Success", "You have been teleported!", toastDurationShort);
		});

		Command::Add("openLink", [](Command::Params* params)
		{
			if (params->length() > 1)
			{
				Utils::OpenUrl(params->get(1));
			}
		});
	}

	Command::~Command()
	{
		Command::FunctionMap.clear();
		Command::FunctionMapSV.clear();
	}
}
