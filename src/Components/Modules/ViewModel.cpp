#include "STDInclude.hpp"

namespace Components
{
	namespace
	{
		bool inspecting = false;

		void StartWeaponAnim(int localClientNum, int weaponIndex, int animIndex, float transitionTime)
		{
			const DWORD start_weapon_anim = 0x59B270;
			__asm
			{
				mov eax, weaponIndex
				mov edi, localClientNum
				push transitionTime
				push animIndex
				call start_weapon_anim
				add esp, 8
			}
		}

		void BG_CalculateWeaponMovement_Bob(Game::weaponState_t*)
		{
		}

		__declspec(naked) void BG_CalculateWeaponMovement_Bob_stub()
		{
			__asm
			{
				push esi
				call BG_CalculateWeaponMovement_Bob
				add esp, 4
				retn
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

		void DisableInspectIfAds(int, float weaponPos, int /*anim*/)
		{
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
	}

	ViewModel::ViewModel()
	{
		// vm_bob_move_scale = Game::Dvar_RegisterFloat("vm_bob_move_scale", 0.4f, 0.0f, 1000.0f, 0, "");
		// vm_bob_rotate_scale = Game::Dvar_RegisterFloat("vm_bob_rotate_scale", 1.4f, 0.0f, 1000.0f, 0, "");
		// vm_bob_time_scale = Game::Dvar_RegisterFloat("vm_bob_time_scale", 0.8f, 0.0f, 1000.0f, 0, "");
		
		// Utils::Hook(0x57CEB0, BG_CalculateWeaponMovement_Bob_stub, HOOK_JUMP).install()->quick();
		Utils::Hook(0x59B6BC, PlayAdsAnim_stub, HOOK_CALL).install()->quick();

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

#ifdef DEBUG
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
#endif
	}
		
}