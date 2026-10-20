/**
 * @file interface.cppm
 * @brief Owned network interface selection for HTTP connections.
 */
export module mcr:interface;

export import :types;

import std;

export namespace mcr::options {

    /**
     * @brief Store a network interface selector, following cpr's Interface option.
     * @note Text is owned without validation, normalization, or interface lookup.
     * Empty text represents no explicit interface selection. String access and
     * operations are inherited from StringHolder<Interface>.
     */
    class Interface : public StringHolder<Interface> {
    public:
        /**
         * @brief Construct an empty interface selector.
         */
        Interface() = default;

        /**
         * @brief Take ownership of an interface selector string.
         * @param iface Selector text to store verbatim.
         */
        Interface(std::string iface) : StringHolder<Interface>(std::move(iface)) {}

        /**
         * @brief Copy an interface selector from a string view.
         * @param iface Borrowed text; its storage need not outlive this option.
         */
        Interface(std::string_view iface) : StringHolder<Interface>(iface) {}

        /**
         * @brief Copy a null-terminated interface selector.
         * @param iface Pointer to a valid null-terminated string.
         */
        Interface(char const* iface) : StringHolder<Interface>(iface) {}

        /**
         * @brief Copy an interface selector from a byte range.
         * @param str Pointer to at least len readable bytes.
         * @param len Byte count, including any embedded null bytes.
         */
        Interface(char const* str, std::size_t len) : StringHolder<Interface>(str, len) {}

        /**
         * @brief Join interface selector fragments without separators.
         * @param args Fragments to copy in order, preserving all bytes.
         */
        Interface(std::initializer_list<std::string> args) : StringHolder<Interface>(args) {}

        Interface(Interface const& other) = default;
        Interface(Interface&& other) noexcept = default;
        ~Interface() override = default;

        auto operator=(Interface const& other) -> Interface& = default;
        auto operator=(Interface&& other) noexcept -> Interface& = default;
    };

} // namespace mcr::options
