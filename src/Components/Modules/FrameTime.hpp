#pragma once

namespace Components
{
	class FrameTime : public Component
	{
	public:
		FrameTime();

		uint32_t *GetFrameTime();

	private:
		static void SVFrameWaitStub();
		static void SVFrameWaitFunc();

		static void NetSleep(int msec);

		static int ComTimeVal(int minMsec);
		static uint32_t ComFrameWait(int minMsec);
		static void ComFrameWaitStub();
	};
}
