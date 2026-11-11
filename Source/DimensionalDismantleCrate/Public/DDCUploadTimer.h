#pragma once

#include "CoreMinimal.h"

namespace DDC
{
	/**
	 * Upload timer that copies the vanilla Dimensional Depot Uploader (AFGCentralStorageContainer, game CL517554):
	 *
	 *   Factory_Tick:  inventory empty            -> timer = -1 (idle)
	 *                  idle and nothing uploadable -> stay idle
	 *                  idle and something uploadable -> timer = 0
	 *                  timer += dt; if timer >= T -> TryUploadItem
	 *   TryUploadItem: timer -= T (leftover time carries over), upload the first slot that succeeds (one item);
	 *                  if no slot succeeds -> timer = -1 (idle)
	 *
	 * So it uploads at most one item per tick, at one item every T seconds on average, and after a hitch it catches
	 * up at one item per tick. This is kept exactly, including the leftover growing while T is shorter than a frame:
	 * the timer resets whenever it goes idle, so the backlog only lasts while the source keeps uploading.
	 */
	struct FUploadTimer
	{
		static constexpr float Idle = -1.0f;

		float Time = Idle;

		bool IsIdle() const { return Time < 0.0f; }
		void Reset() { Time = Idle; }

		/**
		 * Advances the timer by one tick.
		 * @param bHasItems    the source holds any items at all
		 * @param CanUpload    returns true if at least one item could be uploaded now (only asked while idle)
		 * @param TryUploadOne uploads one item and returns true, or returns false if nothing could be uploaded
		 * @return true if an item was uploaded this tick
		 */
		template <typename FCanUpload, typename FTryUploadOne>
		bool Step(const float DeltaSeconds, const float TimeToUpload, const bool bHasItems, FCanUpload&& CanUpload, FTryUploadOne&& TryUploadOne)
		{
			if (!bHasItems)
			{
				Reset();
				return false;
			}
			if (IsIdle())
			{
				if (!CanUpload())
				{
					return false;
				}
				Time = 0.0f;
			}
			Time += DeltaSeconds;
			if (Time < TimeToUpload)
			{
				return false;
			}
			Time -= TimeToUpload;
			if (TryUploadOne())
			{
				return true;
			}
			Reset();
			return false;
		}
	};
}
