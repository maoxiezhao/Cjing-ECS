# ECS
Cjing-ECS is a lightweight and fast ECS  
一个轻量、快速的Entity-component-system，基于Archetype的方式存储, 纯C++实现
  
Features:  
* basic entity, component, system
* Archetype storage 
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
    ECS::EntityID prefab = world->CreatePrefab("TestPrefab")
        .With<PositionComponent>()
        .With<VelocityComponent>()
        .entity;

    ECS::EntityID test1 = world->CreateEntity("Test1")
        .Instantiate(prefab);
    
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
