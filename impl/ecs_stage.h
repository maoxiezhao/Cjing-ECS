#include "ecs_def.h"

namespace ECS
{
	struct ObjectBase;
	struct Stage;

	void InitStage(WorldImpl* world, Stage* stage);
	void FiniStage(WorldImpl* world, Stage* stage);
	void SetStageCount(WorldImpl* world, I32 stageCount);
	I32 GetStageCount(WorldImpl* world);
	I32 GetStageID(ObjectBase* threadCtx);
	Stage* GetStage(WorldImpl* world, I32 stageID);
	Stage* GetStageFromWorld(ObjectBase* threadCtx);
	WorldImpl* GetWorld(ObjectBase* threadCtx);

	void BeginDefer(WorldImpl* world);
	void EndDefer(WorldImpl* world);
}