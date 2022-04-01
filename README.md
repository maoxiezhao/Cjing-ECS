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
* Hierarchy
  
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
Exmaple2
```cpp
    std::unique_ptr<ECS::World> world = ECS::World::Create();
    std::vector<ECS::EntityID> queue;
    auto e1 = world->CreateEntity("e1")
        .With<Position, Local>({ 1.0f, 1.0f })
        .With<Position, Global>({ 11.0f, 11.0f });

    auto e2 = world->CreateEntity("e2")
        .ChildOf(e1.entity)
        .With<Position, Local>({ 2.0f, 2.0f })
        .With<Position, Global>({ 22.0f, 22.0f });

    auto e3 = world->CreateEntity("e3")
        .ChildOf(e2.entity)
        .With<Position, Local>({ 3.0f, 3.0f })
        .With<Position, Global>({ 33.0f, 33.0f });

    auto e4 = world->CreateEntity("e4")
        .ChildOf(e1.entity)
        .With<Position, Local>({ 4.0f, 4.0f })
        .With<Position, Global>({ 44.0f, 44.0f })
        .With<Health>();

    // Query
    auto query = world->CreateQuery<Position, Position, Position>()
        .Item(0).Obj<Local>()
        .Item(1).Obj<Global>()
        .Item(2).Obj<Global>()
        .Item(2)
        .Set(ECS::QueryItemFlagParent | ECS::QueryItemFlagCascade)
        .Build();
    query.ForEach([&](ECS::EntityID entity, Position& p1, Position& p2, Position& pOut) {
        queue.push_back(entity);
    });

    CHECK(queue[0] == e1.entity);
    CHECK(queue[1] == e2.entity);
    CHECK(queue[2] == e4.entity);
    CHECK(queue[3] == e3.entity);
```


![image](https://github.com/maoxiezhao/Cjing-ECS/blob/master/img/ECS_architecture_small.png)
