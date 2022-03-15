#pragma once

namespace Components
{
	class UIFeeder : public Component
	{
	public:
		typedef Utils::Slot<unsigned int()> GetItemCount_t;
		typedef Utils::Slot<const char*(unsigned int /*index*/, int /*column*/)> GetItemText_t;
		typedef Utils::Slot<void(unsigned int /*index*/)> Select_t;

		struct Callbacks
		{
			GetItemCount_t getItemCount;
			GetItemText_t getItemText;
			Select_t select;
		};

		UIFeeder();
		~UIFeeder();

		static void Add(float feeder, GetItemCount_t itemCountCb, GetItemText_t itemTextCb, Select_t selectCb);
		static void Select(float feeder, unsigned int index);

	private:
		struct Container
		{
			float feeder;
			int index;
			int column;
		};

		static Container Current;

		static void GetItemCountStub();
		static unsigned int GetItemCount();

		static void GetItemTextStub();
		static const char* GetItemText();

		static void SetItemSelectionStub();
		static bool SetItemSelection();

		static bool CheckFeeder();
		static int CheckSelection(int feeder);
		static void CheckSelectionStub();

		static void MouseEnterStub();
		static void MouseSelectStub();
		static void HandleKeyStub();
		static void PlaySoundStub();

		static std::unordered_map<float, Callbacks> Feeders;

		static unsigned int GetMapCount();
		static const char* GetMapText(unsigned int index, int column);
		static void SelectMap(unsigned int index);
		static void ApplyMap(UIScript::Token token);
		static void ApplyInitialMap(UIScript::Token token);
	};
}
