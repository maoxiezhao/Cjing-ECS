#include "ecs_def.h"

namespace ECS
{
	struct TriggerComponent
	{
		Trigger* trigger = nullptr;
	};

	struct ObserverComponent
	{
		Observer* observer = nullptr;
	};

	extern EntityID ECS_ENTITY_ID(TriggerComponent);
	extern EntityID ECS_ENTITY_ID(ObserverComponent);

	EntityID CreateTrigger(WorldImpl* world, const TriggerDesc& desc);
	void FiniTrigger(Trigger* trigger);
	EntityID CreateObserver(WorldImpl* world, const ObserverDesc& desc);
	void FiniObserver(Observer* observer);

	void EmitEvent(WorldImpl* world, const EventDesc& desc);
}