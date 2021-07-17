#include "STDInclude.hpp"

namespace Components
{
	namespace
	{
		uint32_t frameTime = 0;
		bool inspecting = false;

		void StartWeaponAnim(int localClientNum, int weaponIndex, int animIndex, float transitionTime)
		{
			__asm
			{
				mov ebx, 0x59B270
				mov eax, weaponIndex
				mov edi, localClientNum
				push transitionTime
				push animIndex
				call ebx
				add esp, 8
			}
		}

		int GetCurrentWeaponAnimIndex()
		{
			// 0x7F0F78 is playerState in gc
			return *reinterpret_cast<int*>(0x7F0F78 + 488) & 0xFFFFFDFF;
		}

		float GetCurrentWeaponAdsProgress()
		{
			return *reinterpret_cast<float*>(0x7F0F78 + 704);
		}

		float ads_progress = 0.0f;

		void DisableInspectIfAds(int, float weaponPos, int /*anim*/)
		{
			ads_progress = weaponPos;

			// restore to idle animation if in ads state
			if (weaponPos > 0 && inspecting)
			{
				int weap_index = Game::BG_GetViewmodelWeaponIndex(0x7F0F78);
				StartWeaponAnim(0, weap_index, 1, 0.1f);

				inspecting = false;
			}
		}

		__declspec(naked) void PlayAdsAnim_stub()
		{
			__asm
			{
				call DisableInspectIfAds
				mov eax, 59B420h
				jmp eax
			}
		}

		struct Attachment
		{
			std::string xmodel;
			std::string bone;
		};

		static std::unordered_map<std::string, std::vector<Attachment>> weapon_attachment_map;

		void AddWeaponAttachments(const std::string& weaponName, std::vector<Game::DObjModel_s>& models)
		{
			if (weapon_attachment_map.find(weaponName) != weapon_attachment_map.end())
			{
				for (auto& attach : weapon_attachment_map[weaponName])
				{
					auto* xmodel = Game::DB_FindXAssetHeader(Game::ASSET_TYPE_XMODEL, attach.xmodel.data()).model;
					auto boneName = Game::SL_GetString(attach.bone.data(), 0);

					Game::DObjModel_s dobjModel = { xmodel, boneName, true };
					models.push_back(dobjModel);
				}
			}
		}

		void* CreateViewmodelDObj(Game::DObjModel_s* dobjModels, uint16_t numModels, void* tree, int handle, int localClientNum)
		{
			std::vector<Game::DObjModel_s> models;
			models.resize(numModels);

			std::memcpy(models.data(), dobjModels, numModels * sizeof(Game::DObjModel_s));

			int weap_index = Game::BG_GetViewmodelWeaponIndex(0x7F0F78);
			std::string weaponName = Game::BG_GetWeaponName(weap_index);

			AddWeaponAttachments(weaponName, models);

			return Game::Com_ClientDObjCreate(models.data(), static_cast<uint16_t>(models.size()), tree, handle, localClientNum);
		}

		Game::dvar_t* vm_offset_x;
		Game::dvar_t* vm_offset_y;
		Game::dvar_t* vm_offset_z;

		float recoil_speed = 0.0f;

#ifdef SHIT_GUNKICK
		float fire = 0;
		float lastzAng = 0;
#endif

		Game::weaponState_t* MoveWeaponPosition(Game::weaponState_t* ws)
		{
			float frac = 1.0f - ads_progress;

			ws->baseOrigin[0] = vm_offset_x->current.value * frac;
			ws->baseOrigin[1] = vm_offset_y->current.value * frac;
			ws->baseOrigin[2] = vm_offset_z->current.value * frac;

#ifdef SHIT_GUNKICK
			ws->bobAngles[2] -= lastzAng;
#endif

			return ws;
		}

		__declspec(naked) void BG_CalculateWeaponMovement_Bob_stub()
		{
			__asm
			{
				push esi
				mov eax, 0x57CEB0
				call eax

				call MoveWeaponPosition
				add esp, 4

				retn
			}
		}

		void BG_CalculateViewMovement_Angles(Game::viewState_t* vs, float* angles)
		{
			// Call orig first
			__asm {
				mov eax, 0x44B6F0
				push angles
				push vs
				call eax
				add esp, 8
			}

#ifdef SHIT_GUNKICK
			static int lastAnim = 0;
			int curAnim = GetCurrentWeaponAnimIndex();

			if (curAnim != lastAnim)
			{
				lastAnim = curAnim;

				if (curAnim == 2 || curAnim == 3 || curAnim == 5 || curAnim == 6)
				{
					fire = 30;
				}
			}

			if (fire > 0)
			{
				lastzAng = angles[2] = (((float)rand() / RAND_MAX) * 2.f - 1.f) * (fire / 30.f) * 30.f;

				fire -= 1;
			}
#endif
		}

		Game::dvar_t* vm_fov = nullptr;
		Game::dvar_t* vm_fov_enabled = nullptr;

		float GetGunFov()
		{
			static Game::dvar_t* cg_fov;
			static Game::dvar_t* cg_fovScale;
			
			if (!cg_fov || !cg_fovScale)
			{
				cg_fov = Game::Dvar_FindVar("cg_fov");
				cg_fovScale = Game::Dvar_FindVar("cg_fovScale");
			}
			
			float fov = static_cast<float>(Game::CG_GetViewFov());
			float ratio = fov / (cg_fov->current.value * cg_fovScale->current.value);

			return vm_fov->current.value * ratio;
		}

		void UpdateFov(Game::GfxViewParms* parms)
		{
			if (vm_fov_enabled->current.enabled)
			{
				const float w_fov = 0.75f * tanf(GetGunFov() * 0.01745329238474369f * 0.5f);

				const float tanHalfX = (16.0f / 9.0f) * w_fov;
				const float tanHalfY = w_fov;

				Game::InfinitePerspectiveMatrix(tanHalfX, tanHalfY, parms->zNear, parms->projectionMatrix.m);
			}

			// left hand, but breaks normal
			// parms->projectionMatrix.m[0][0] *= -1.f;
		}
		
		__declspec(naked) void R_SetViewParmsForScene_Stub()
		{
			const static uint32_t retn_pt = 0x50C2F7;

			__asm
			{ 
				pushad
				push esi // ViewParms
				call UpdateFov
				add esp, 4
				popad

				// orig
				lea     ecx, [esp + 5Ch]
				push    ecx
				push    edi

				jmp retn_pt
			}
		}
	}

	void ViewModel::LoadOrUpdateAttachmentSet(const std::string &weaponName)
	{
		auto file = FileSystem::File(Utils::String::VA("attachsets/%s", weaponName.data()));

		if (file.exists())
		{
			std::string errors;
			auto attachset = json11::Json::parse(file.getBuffer(), errors);

			if (!errors.empty())
			{
				Components::Logger::Error("Attachment set %s is broken: %s.", weaponName.data(), errors.data());
			}

			if (!attachset.is_object())
			{
				Components::Logger::Error("Attachment set %s is invaild.", weaponName.data(), errors.data());
			}

			std::vector<Attachment> attachs;

			for (auto& attach : attachset.object_items())
			{
				attachs.push_back({ attach.first, attach.second.string_value() });	
			}

			weapon_attachment_map[weaponName] = attachs;
		}
	}

	ViewModel::ViewModel()
	{
		vm_offset_x = Game::Dvar_RegisterFloat("vm_offset_x", 0.0f, -1000.0f, 1000.0f, Game::DVAR_FLAG_SAVED, "Viewmodel offset of x.");
		vm_offset_y = Game::Dvar_RegisterFloat("vm_offset_y", 0.0f, -1000.0f, 1000.0f, Game::DVAR_FLAG_SAVED, "Viewmodel offset of y.");
		vm_offset_z = Game::Dvar_RegisterFloat("vm_offset_z", 0.0f, -1000.0f, 1000.0f, Game::DVAR_FLAG_SAVED, "Viewmodel offset of z.");

		vm_fov = Game::Dvar_RegisterFloat("vm_fov", 65.f, 0.0f, 360.0f, Game::DVAR_FLAG_SAVED, "Viewmodel field of view.");
		vm_fov_enabled = Game::Dvar_RegisterBool("vm_fov_enabled", true, Game::DVAR_FLAG_SAVED, "Enabled viewmodel fov.");
		
		Utils::Hook(0x4EC45A, BG_CalculateWeaponMovement_Bob_stub, HOOK_CALL).install()->quick();
		Utils::Hook(0x59B6BC, PlayAdsAnim_stub, HOOK_CALL).install()->quick();
		Utils::Hook(0x59C196, CreateViewmodelDObj, HOOK_CALL).install()->quick();

		Utils::Hook(0x4936ED, BG_CalculateViewMovement_Angles, HOOK_CALL).install()->quick();
		Utils::Hook(0x596B73, BG_CalculateViewMovement_Angles, HOOK_CALL).install()->quick();

		Utils::Hook(0x50C2F1, R_SetViewParmsForScene_Stub, HOOK_JUMP).install()->quick();

		// Utils::Hook(0x59B794, OnFireStub, HOOK_CALL).install()->quick();
		// Utils::Hook(0x59B7A9, OnFireStub, HOOK_CALL).install()->quick();
		// Utils::Hook(0x59B7D3, OnFireStub, HOOK_CALL).install()->quick();
		// Utils::Hook(0x59B7E8, OnFireStub, HOOK_CALL).install()->quick();

		Command::Add("inspect", [](Command::Params*)
		{
			if (!Game::CL_IsCgameInitialized())
				return;

			int current_animation = GetCurrentWeaponAnimIndex();
			float weaponPosFrac = GetCurrentWeaponAdsProgress();

			// stunnedAnimLoop
			const int anim_index = 27;

			int weap_index = Game::BG_GetViewmodelWeaponIndex(0x7F0F78);

			// only idle or our inspect animation is allowed to use this
			if (weaponPosFrac == 0.0f && 
			   (current_animation == 0 || current_animation == anim_index))
			{
				StartWeaponAnim(0, weap_index, anim_index, 0.5f);
				inspecting = true;
			}
		});

//#ifdef DEBUG
		Command::Add("vm_curanim", [](Command::Params*)
		{
			int current_animation = GetCurrentWeaponAnimIndex();
			Game::Com_Printf(0, "Current weapon animation index: %d\n", current_animation);
		});

		Command::Add("vm_anim", [](Command::Params* p) 
		{
			int weap_index = Game::BG_GetViewmodelWeaponIndex(0x7F0F78);
			StartWeaponAnim(0, weap_index, std::atoi(p->get(1)), 0.5f);
		});
//#endif

		Scheduler::OnFrame([]() 
		{
			static uint32_t lastTime = 0;

			auto time = Game::Sys_Milliseconds();
			frameTime = time - lastTime;
			lastTime = time;

		}, true);
	}
		
}