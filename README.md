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
    ECS::World world;
    for (int i = 0; i < 500; i++)
    {
        world.Entity((std::string("A") + std::to_string(i)).c_str())
            .Add<PositionComponent>()
            .Add<VelocityComponent>();
    }
    for (int i = 0; i < 5; i++)
    {
        world.Entity().Add<PositionComponent>();
    }
    for (int i = 0; i < 25; i++)
    {
        world.Entity().Add<PositionComponent>()
            .Add<TestComponent>();
    }

    int times = 0;
    world.Each<PositionComponent>([&](ECS::Entity entity, PositionComponent& pos) {
        times++;
    });
    CHECK(times == 530);
}
```
Exmaple2
```cpp
     I32 updateTimes = 0;
    ECS::World world;
    world.SetThreads(10);
    for (int i = 0; i < 500; i++)
    {
        world.Entity((std::string("A") + std::to_string(i)).c_str())
            .Add<PositionComponent>()
            .Add<VelocityComponent>();
    }

    // ForEach
    ECS::System system = world.CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([&updateTimes](ECS::Entity entity, PositionComponent& pos, VelocityComponent& vel)
        {
            updateTimes++;
        }
    );
    system.Run();
    CHECK(updateTimes == 500);

    // Iter
    ECS::System system1 = world.CreateSystem<PositionComponent, VelocityComponent>()
        .Iter([&updateTimes](ECS::EntityIterator iter, PositionComponent* pos, VelocityComponent* vel)
        {
            for(int i = 0; i < iter.Count(); i++)
                updateTimes++;
        }
    );
    system1.Run();
```