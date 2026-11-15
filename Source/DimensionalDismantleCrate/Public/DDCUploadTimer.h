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
	 *
	 * One deliberate difference: while idle, the "anything uploadable?" check runs at most every IdleRecheckSeconds
	 * instead of every tick (a crate full of items the depot won't take would otherwise scan all its slots every frame).
	 * A source that just received items (it was empty) or was reset is checked on its next tick. So uploading can start
	 * up to IdleRecheckSeconds later than an uploader would; the upload rate once started is unchanged.
	 */
	struct FUploadTimer
	{
		static constexpr float Idle = -1.0f;
		static constexpr float IdleRecheckSeconds = 0.25f;

		float Time = Idle;
		/** Time since the last idle check; starts at IdleRecheckSeconds so the first check happens right away. */
		float SinceIdleCheck = IdleRecheckSeconds;

		bool IsIdle() const { return Time < 0.0f; }
		/** Goes idle and checks again on the next tick. */
		void Reset() { Time = Idle; SinceIdleCheck = IdleRecheckSeconds; }

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
				SinceIdleCheck += DeltaSeconds;
				if (SinceIdleCheck < IdleRecheckSeconds)
				{
					return false;
				}
				SinceIdleCheck = 0.0f;
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
			// Nothing could be uploaded: go idle and wait a full recheck interval before looking again.
			Time = Idle;
			SinceIdleCheck = 0.0f;
			return false;
		}
	};
}
