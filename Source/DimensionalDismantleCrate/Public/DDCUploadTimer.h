#pragma once

#include "CoreMinimal.h"

namespace DDC
{
	/** What one attempt to move an item out of a crate did. */
	enum class EMoveResult : uint8
	{
		/** One item left the crate (uploaded, or sunk). */
		Moved,
		/** Nothing in the source can go anywhere right now. */
		Nothing,
		/** Something could be uploaded, but the network budget for depot additions is used up for now. */
		Throttled,
	};

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
	 * Deliberate differences:
	 * - While idle, the "anything uploadable?" check runs at most every IdleRecheckSeconds instead of every tick (a crate
	 *   full of items the depot won't take would otherwise scan all its slots every frame). A source that just received
	 *   items (it was empty) or was reset is checked on its next tick. So uploading can start up to IdleRecheckSeconds
	 *   later than an uploader would; the upload rate once started is unchanged.
	 * - Throttled (network budget used up): the timer stays ready, without building up a burst, and tries again next tick.
	 * - StepMulti allows several items per tick, for a source that stands for several uploaders.
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
		/** Goes idle and waits a full recheck interval before looking again (nothing could be moved). */
		void GoIdle() { Time = Idle; SinceIdleCheck = 0.0f; }

		/**
		 * Advances the timer by one tick, moving at most MaxPerTick items, one every Interval seconds.
		 * @param bHasItems  the source holds any items at all
		 * @param CanMove    returns true if at least one item could be moved now (only asked while idle)
		 * @param TryMoveOne moves one item and says what happened (EMoveResult)
		 * @return the number of items moved this tick
		 */
		template <typename FCanMove, typename FTryMoveOne>
		int32 StepMulti(const float DeltaSeconds, const float Interval, const int32 MaxPerTick, const bool bHasItems, FCanMove&& CanMove, FTryMoveOne&& TryMoveOne)
		{
			if (!bHasItems)
			{
				Reset();
				return 0;
			}
			if (IsIdle())
			{
				SinceIdleCheck += DeltaSeconds;
				if (SinceIdleCheck < IdleRecheckSeconds)
				{
					return 0;
				}
				SinceIdleCheck = 0.0f;
				if (!CanMove())
				{
					return 0;
				}
				Time = 0.0f;
			}
			Time += DeltaSeconds;
			int32 Moved = 0;
			while (Time >= Interval && Moved < MaxPerTick)
			{
				Time -= Interval;
				switch (TryMoveOne())
				{
				case EMoveResult::Moved:
					++Moved;
					break;
				case EMoveResult::Throttled:
					// Ready again on the next tick, but no burst builds up while the budget is used up.
					Time = Interval;
					return Moved;
				case EMoveResult::Nothing:
				default:
					// Nothing could be moved: go idle and wait a full recheck interval before looking again.
					GoIdle();
					return Moved;
				}
			}
			return Moved;
		}

		/** One uploader: at most one item per tick, one every TimeToUpload seconds. Returns true if an item was moved. */
		template <typename FCanMove, typename FTryMoveOne>
		bool Step(const float DeltaSeconds, const float TimeToUpload, const bool bHasItems, FCanMove&& CanMove, FTryMoveOne&& TryMoveOne)
		{
			return StepMulti(DeltaSeconds, TimeToUpload, 1, bHasItems, Forward<FCanMove>(CanMove), Forward<FTryMoveOne>(TryMoveOne)) > 0;
		}

		/**
		 * Instant mode: true if this source should be tried this tick. A source that found nothing to move waits out the
		 * idle recheck interval first (the caller calls GoIdle when it finds nothing).
		 */
		bool ReadyForInstant(const float DeltaSeconds, const bool bHasItems)
		{
			if (!bHasItems)
			{
				Reset();
				return false;
			}
			if (!IsIdle())
			{
				return true;
			}
			SinceIdleCheck += DeltaSeconds;
			if (SinceIdleCheck < IdleRecheckSeconds)
			{
				return false;
			}
			SinceIdleCheck = 0.0f;
			Time = 0.0f;
			return true;
		}
	};
}
