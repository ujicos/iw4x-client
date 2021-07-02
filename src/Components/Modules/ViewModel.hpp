#pragma once

namespace Components
{
	class ViewModel : public Component
	{
	public:
		ViewModel();
		static void LoadOrUpdateAttachmentSet(const std::string& weaponName);
	};
}
