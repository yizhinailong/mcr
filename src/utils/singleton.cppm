/**
 * @file singleton.cppm
 * @brief A CRTP singleton with lazy initialization and explicit, one-time shutdown.
 */
export module mcr.singleton;

import std;

export namespace mcr::utils {

    /**
     * @brief Provide cpr's singleton lifecycle without declaration or implementation macros.
     * @tparam T Derived type, which befriends Singleton<T> and hides its default constructor.
     * @note GetInstance calls may run concurrently, as may ExitInstance calls.
     * Shutdown must not overlap GetInstance or any use of previously returned pointers.
     * Instances require explicit shutdown and are never recreated after destruction.
     */
    template <typename T>
    class Singleton {
    private:
        friend T;

        inline static T*             s_instance{ nullptr }; ///< Owned until explicit ExitInstance.
        inline static std::once_flag s_get_flag;            ///< Allows one successful construction per T.
        inline static std::once_flag s_exit_flag;           ///< Allows one successful destruction per T.

        /**
         * @brief Allow only T to construct its singleton base.
         */
        Singleton()  = default;

        /**
         * @brief Destroy the base as part of T; deletion through the base is forbidden.
         */
        ~Singleton() = default;

    public:
        Singleton(Singleton const&)            = delete;
        Singleton(Singleton&&)                 = delete;
        Singleton& operator=(Singleton const&) = delete;
        Singleton& operator=(Singleton&&)      = delete;

        /**
         * @brief Lazily construct one instance, retrying if construction throws.
         * @return The shared instance, or nullptr after successful ExitInstance.
         * @throws std::bad_alloc If allocation fails; exceptions from T's constructor propagate.
         * @throws std::system_error If std::call_once cannot complete.
         */
        [[nodiscard]] static auto GetInstance() -> T* {
            // Allocate here so T may keep its constructor private; lifetime is explicit as in cpr.
            std::call_once(s_get_flag, [] { s_instance = new T; });
            return s_instance;
        }

        /**
         * @brief Destroy the instance once; later calls have no effect.
         * @throws std::logic_error If no GetInstance call has successfully initialized T yet.
         * @throws std::system_error If std::call_once cannot complete.
         * @note T's destructor must not throw. Stop all instance users before shutdown.
         */
        static auto ExitInstance() -> void {
            std::call_once(s_exit_flag, [] {
                if (s_instance == nullptr) {
                    throw std::logic_error{ "mcr::utils::Singleton: ExitInstance requires successful initialization" };
                }
                delete s_instance;
                s_instance = nullptr;
            });
        }
    };

} // namespace mcr::utils
