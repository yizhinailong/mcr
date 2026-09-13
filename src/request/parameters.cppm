/**
 * @file parameters.cppm
 * @brief Ordered URL query parameters built on the common curl container.
 */
export module mcr.parameters;

export import mcr.curl_container;

import std;

export namespace mcr {

    /**
     * @brief Own query parameters, following cpr's Parameters interface.
     * @note Inherits encode, Add(), and both GetContent() overloads from curl::CurlContainer.
     * Order and duplicate keys are retained; empty values are emitted without an equals sign.
     */
    class Parameters : public curl::CurlContainer<Parameter> {
    public:
        /**
         * @brief Construct an empty parameter collection with encoding enabled.
         */
        Parameters() = default;

        /**
         * @brief Copy query parameters in their supplied order.
         * @param parameters Initial key/value entries, including an empty list or duplicate keys.
         */
        Parameters(std::initializer_list<Parameter> const& parameters) : curl::CurlContainer<Parameter>{ parameters } {}
    };

} // namespace mcr
