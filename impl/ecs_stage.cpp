#include "ecs_priv_types.h"
#include "ecs_stage.h"
#include "ecs_world.h"

namespace ECS
{
	void InitStage(WorldImpl* world, Stage* stage)
	{
		ECS_NEW_PLACEMENT(stage, Stage);
		ECS_INIT_OBJECT(stage, Stage);
		stage->world = world;
		stage->threadCtx = &world->base;
	}

	void FiniStage(WorldImpl* world, Stage* stage)
	{
		stage->~Stage();
	}

	Stage* GetStage(WorldImpl* world, I32 stageID)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT(world->stageCount > stageID);
		return &world->stages[stageID];
	}

	Stage* GetStageFromWorld(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			WorldImpl* world = (WorldImpl*)threadCtx;
			ECS_ASSERT(world->stageCount <= 1 || !world->isReadonly);
			return &world->stages[0];
		}
		else if(ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage;
		}

		ECS_ASSERT(false);
		return nullptr;
	}

	WorldImpl* GetWorld(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			return (WorldImpl*)threadCtx;
		}
		else if (ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage->world;
		}

		ECS_ASSERT(false);
		return nullptr;
	}

	I32 GetStageID(ObjectBase* threadCtx)
	{
		ECS_ASSERT(threadCtx != nullptr);
		if (ECS_CHECK_OBJECT(threadCtx, Stage))
		{
			Stage* stage = (Stage*)threadCtx;
			return stage->id;
		}
		else if (ECS_CHECK_OBJECT(threadCtx, WorldImpl))
		{
			return 0;
		}
		ECS_ASSERT(false);
		return 0;
	}

	I32 GetStageCount(WorldImpl* world)
	{
		return world->stageCount;
	}

	void SetStageCount(WorldImpl* world, I32 stageCount)
	{
		ECS_ASSERT(world != nullptr);
		ECS_ASSERT((stageCount > 1 && !world->isFini) || stageCount <= 1);

		if (stageCount == world->stageCount)
			return;

		// Fini existing stages
		if (world->stageCount > 0 && stageCount != world->stageCount)
		{
			for (int i = 0; i < world->stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				ECS_ASSERT(stage->thread == 0);
				FiniStage(world, stage);
			}

			ECS_FREE(world->stages);
		}

		world->stages = nullptr;
		world->stageCount = stageCount;

		// Init new stages
		if (stageCount > 0)
		{
			world->stages = ECS_MALLOC_T_N(Stage, stageCount);
			for (int i = 0; i < stageCount; i++)
			{
				Stage* stage = &world->stages[i];
				InitStage(world, stage);
				stage->id = i;
				stage->threadCtx = &stage->base;
			}
		}
	}
}