#include "ecs.h"

#include <string>
#include <array>

// #define CATCH_CONFIG_MAIN
#include "catch.hpp"

static int clearTimes = 0;
struct TestComponent
{
    COMPONENT(TestComponent);
    std::vector<int> vec;

    TestComponent(TestComponent&& rhs) noexcept
    {
        vec = std::move(rhs.vec);
    }

    ~TestComponent()
    {
        clearTimes++;
    }

    void operator=(TestComponent&& rhs) noexcept
    {
        vec = std::move(rhs.vec);
    }

    void operator=(const TestComponent& rhs)
    {
        vec = rhs.vec;
    }
};
//
//TEST_CASE("Reflect", "ECS")
//{
//    ECS::Reflect::ReflectInfo info = {};
//    info.ctor = ECS::Reflect::Ctor<TestComponent>();
//    info.dtor = ECS::Reflect::Dtor<TestComponent>();
//    info.copy = ECS::Reflect::Copy<TestComponent>();
//    info.move = ECS::Reflect::Move<TestComponent>();
//    info.copyCtor = ECS::Reflect::CopyCtor<TestComponent>();
//    info.moveCtor = ECS::Reflect::MoveCtor<TestComponent>();
//
//    TestComponent a[2];
//    a[0].vec.push_back(1);
//    a[0].vec.push_back(2);
//    a[0].vec.push_back(3);
//    
//    TestComponent* b = static_cast<TestComponent*>(malloc(sizeof(TestComponent) * 2));
//    info.moveCtor(nullptr, nullptr, nullptr, sizeof(TestComponent), 2, a, b);
//    CHECK(b[0].vec.size() == 3);
//    CHECK(a[0].vec.size() == 0);
//
//    info.copy(nullptr, nullptr, nullptr, sizeof(TestComponent), 2, b, a);
//    CHECK(b[0].vec.size() == 3);
//    CHECK(a[0].vec.size() == 3);
//
//    info.dtor(nullptr, nullptr, sizeof(TestComponent), 2, b);
//    CHECK(clearTimes == 2);
//}
//
//TEST_CASE("SimpleFlow", "ECS")
//{
//    std::unique_ptr<ECS::World> world = ECS::World::Create();
//    world->CreateEntity("AABB").with(TestComponent());
//    //ECS::EntityID entity = world.CreateEntity("Test").with(TestComponent()).entity;
//    //ECS::NameComponent* nameComp = world.GetComponent<ECS::NameComponent>(entity);
//    //if (nameComp)
//    //    std::cout << nameComp->name << std::endl;
//
//    //TestComponent* testComp = world.GetComponent<TestComponent>(entity);
//    //if (testComp)
//    //    testComp->vec.push_back(1);
//
//    //testComp = world.GetComponent<TestComponent>(entity);
//    int temp = 1;
//    CHECK(temp == 1);
//}
//TEST_CASE("System", "ECS")
//{
//}

template <typename T, typename = int>
struct EachColumn { };

struct EachColumnBase 
{
    EachColumnBase(void* ptr_) : ptr(ptr_) {}

protected:
    void* ptr;
};

template<typename T>
struct EachColumn<T, std::enable_if_t<!std::is_pointer_v<T>, int>> : EachColumnBase
{
    EachColumn(void* ptr) : EachColumnBase(ptr) {};

    T& Get()
    {
        return *static_cast<T*>(ptr);
    }
};

template<typename... Comps>
struct CompArray
{
    using Array = std::array<void*, sizeof...(Comps)>;
    Array compArray;

    void Populate(ECS::World& world, ECS::EntityID entity)
    {
        return PopulateImpl(world, entity, 0, static_cast<std::decay_t<Comps>*>(nullptr)...);
    }

private:
    void PopulateImpl(ECS::World& world, ECS::EntityID entity, size_t) { return; }

    template<typename CompPtr, typename... Comps>
    void PopulateImpl(ECS::World& world, ECS::EntityID entity, size_t index, CompPtr, Comps... comps)
    {
        compArray[index] = world.GetComponent<std::remove_pointer_t<CompPtr>>(entity);
        return PopulateImpl(world, entity, index + 1, comps...);
    }
};

template<typename Func, typename... Comps>
struct EachInvoker
{
    using CompArr = typename CompArray<Comps ...>::Array;

    static void Invoke(ECS::World& world, ECS::EntityID entity, Func&& func)
    {
        CompArray<Comps...> compArray;
        compArray.Populate(world, entity);
        InvokeImpl(entity, func, 0, compArray.compArray);
    }

private:

    template<typename... Args, std::enable_if_t<sizeof...(Comps) == sizeof...(Args), int> = 0>
    static void InvokeImpl(ECS::EntityID entity, const Func& func, size_t index, CompArr&, Args... comps)
    {
        func(entity, (EachColumn<std::remove_reference_t<Comps>>(comps).Get())...);
    }

    template<typename... Args, std::enable_if_t<sizeof...(Comps) != sizeof...(Args), int> = 0>
    static void InvokeImpl(ECS::EntityID entity, const Func& func, size_t index, CompArr& compArr, Args... comps)
    {
        std::cout << "Comps:" << sizeof...(Comps) << " Args:" << sizeof...(Args) << std::endl;
        InvokeImpl(entity, func, index + 1, compArr, comps..., compArr[index]);
    }
};

template<typename... Comps>
struct EachBuilder
{
    template<typename Func>
    static void Run(ECS::World& world, ECS::EntityID entity, Func&& func)
    {
        using Invoker = EachInvoker<Func, Comps...>;
        Invoker::Invoke(world, entity, std::forward<Func>(func));
    }
};






















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
    ECS::EntityID entity = world->CreateEntity("a1")
        .with<PositionComponent>()
        .with<VelocityComponent>().entity;

    PositionComponent* pos = world->GetComponent<PositionComponent>(entity);
    pos->x = 2000.0f;

    world->CreateEntity("a2")
        .with<PositionComponent>()
        .with<VelocityComponent>();

    for (int i = 0; i < 500; i++)
    {
        world->CreateEntity((std::string("A") + std::to_string(i)).c_str())
            .with<PositionComponent>()
            .with<VelocityComponent>();
    }

    ECS::EntityID system = world->CreateSystem<PositionComponent, VelocityComponent>()
        .ForEach([](ECS::EntityID entity, PositionComponent& pos, VelocityComponent& vel)
        {
            std::cout << "System update:" << entity << std::endl;
        }
    );
    world->RunSystem(system);
    return 0;
}