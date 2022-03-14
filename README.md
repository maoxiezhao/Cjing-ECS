# ECS
![image]("https://github.com/maoxiezhao/Cjing-ECS/blob/b4680d81d4465efa833517a5481be0cd7201d729/ECS_architecture.jpg")

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