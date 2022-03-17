# ECS
一个简易但是快速的Entity-component-system，基于Archetype的方式存储, 纯C++实现
  
Features:  
* 基础的Entity,Component,Systems实现
* Singleton component
* Query
* Tag
* Prefab
  
 Example:  
```cpp
struct PositionComponent
{
    COMPONENT(PositionComponent);
    float x = 0.0f;
    float y = 0.0f;
};

struct VelocityComponent
{
    COMPONENT(VelocityComponent);
    float x = 0.0f;
    float y = 0.0f;
};

int main()
{
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    world->CreateEntity("a1")
        .with<PositionComponent>()
        .with<VelocityComponent>();

    world->CreateEntity("a2")
        .with<PositionComponent>()
        .with<VelocityComponent>();

    ECS::EntityID system = world->CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel)
        {
            std::cout << "System update:" << entity << std::endl;
        }
    );
    world->RunSystem(system);
    return 0;
}
```
