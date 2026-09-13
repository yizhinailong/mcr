/**
 * @file test_singleton.cpp
 * @brief Verify CRTP access, singleton identity, concurrent lifecycle calls, and initialization retry.
 */
import std;
import mcr;

namespace {

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error{ std::string{ message } };
        }
    }

    template <int Tag>
    class TestSingleton final : public mcr::utils::Singleton<TestSingleton<Tag>> {
    private:
        friend mcr::utils::Singleton<TestSingleton<Tag>>;

        TestSingleton() {
            if (++attempts == 1 && fail_first) {
                throw std::runtime_error{ "initialization failed" };
            }
            ++constructions;
        }

    public:
        inline static std::atomic<int> attempts{ 0 };
        inline static std::atomic<int> constructions{ 0 };
        inline static std::atomic<int> destructions{ 0 };
        inline static bool             fail_first{ false };

        int const value{ 42 };

        ~TestSingleton() { ++destructions; }
    };

    class PrivateSingleton final : public mcr::utils::Singleton<PrivateSingleton> {
    private:
        friend mcr::utils::Singleton<PrivateSingleton>;

        PrivateSingleton() = default;

        ~PrivateSingleton() { ++destructions; }

    public:
        inline static int destructions{ 0 };
    };

    static_assert(!std::is_default_constructible_v<TestSingleton<0>>);
    static_assert(!std::is_copy_constructible_v<TestSingleton<0>>);
    static_assert(!std::is_move_constructible_v<TestSingleton<0>>);
    static_assert(!std::is_copy_assignable_v<TestSingleton<0>>);
    static_assert(!std::is_move_assignable_v<TestSingleton<0>>);
    static_assert(!std::is_default_constructible_v<mcr::utils::Singleton<TestSingleton<0>>>);
    static_assert(!std::is_destructible_v<mcr::utils::Singleton<TestSingleton<0>>>);
    static_assert(!std::is_destructible_v<PrivateSingleton>);
    static_assert(std::is_same_v<decltype(TestSingleton<0>::GetInstance()), TestSingleton<0>*>);
    static_assert(std::is_same_v<decltype(TestSingleton<0>::ExitInstance()), void>);

    void check_concurrent_lifecycle() {
        using Instance = TestSingleton<0>;
        using Other    = TestSingleton<1>;

        require(Instance::constructions == 0 && Other::constructions == 0, "singletons must initialize lazily");
        std::array<Instance*, 16> instances{};
        std::latch                start{ 1 };
        {
            std::vector<std::jthread> threads;
            for (auto& instance : instances) {
                threads.emplace_back([&instance, &start] {
                    start.wait();
                    instance = Instance::GetInstance();
                });
            }
            start.count_down();
        }
        auto* instance = instances.front();
        require(instance != nullptr && instance->value == 42, "GetInstance must publish a fully initialized object");
        for (auto* result : instances) {
            require(result == instance, "concurrent GetInstance calls must return the same address");
        }
        require(Instance::attempts == 1 && Instance::constructions == 1, "concurrent access must construct exactly once");
        require(Instance::GetInstance() == instance, "repeated access must preserve identity");
        require(Other::constructions == 0, "each derived type must have independent initialization state");
        auto* other = Other::GetInstance();
        require(other != nullptr && Other::constructions == 1, "a second derived type must initialize separately");

        std::latch shutdown{ 1 };
        {
            std::vector<std::jthread> threads;
            for (std::size_t index{ 0 }; index < instances.size(); ++index) {
                threads.emplace_back([&shutdown] {
                    shutdown.wait();
                    Instance::ExitInstance();
                });
            }
            shutdown.count_down();
        }
        require(Instance::destructions == 1, "concurrent shutdown must destroy exactly once");
        require(Instance::GetInstance() == nullptr && Instance::constructions == 1, "shutdown must prevent recreation, as in cpr");
        Instance::ExitInstance();
        require(Instance::destructions == 1, "repeated shutdown must have no effect");
        require(Other::GetInstance() == other && Other::destructions == 0, "shutdown must not affect another derived type");
        Other::ExitInstance();
        require(Other::destructions == 1 && Other::GetInstance() == nullptr, "each type must shut down independently");
    }

    void check_failed_initialization() {
        using Instance       = TestSingleton<2>;
        Instance::fail_first = true;
        bool threw{ false };
        try {
            (void)Instance::GetInstance();
        } catch (std::runtime_error const& error) {
            threw = std::string_view{ error.what() } == "initialization failed";
        }
        require(threw && Instance::attempts == 1 && Instance::constructions == 0, "constructor exceptions must propagate without completing initialization");
        require(Instance::GetInstance() != nullptr && Instance::attempts == 2 && Instance::constructions == 1, "GetInstance must retry failed construction");
        Instance::ExitInstance();
        require(Instance::destructions == 1 && Instance::GetInstance() == nullptr, "a retried instance must retain normal shutdown behavior");
    }

    void check_early_shutdown() {
        using Instance = TestSingleton<3>;
        bool threw{ false };
        try {
            Instance::ExitInstance();
        } catch (std::logic_error const&) {
            threw = true;
        }
        require(threw && Instance::constructions == 0 && Instance::destructions == 0, "shutdown before initialization must reject the call without creating or destroying an instance");
        require(Instance::GetInstance() != nullptr, "rejected shutdown must not prevent initialization");
        Instance::ExitInstance();
        require(Instance::destructions == 1 && Instance::GetInstance() == nullptr, "rejected shutdown must not consume the one-time destruction flag");
    }

    void check_private_destructor() {
        require(PrivateSingleton::GetInstance() != nullptr, "friendship must allow a private constructor");
        PrivateSingleton::ExitInstance();
        require(PrivateSingleton::destructions == 1 && PrivateSingleton::GetInstance() == nullptr, "friendship must allow a private destructor");
    }

} // namespace

int main() {
    try {
        check_concurrent_lifecycle();
        check_failed_initialization();
        check_early_shutdown();
        check_private_destructor();
    } catch (std::exception const& error) {
        std::println("test_singleton: {}", error.what());
        return 1;
    }
    std::println("test_singleton: ok");
    return 0;
}
