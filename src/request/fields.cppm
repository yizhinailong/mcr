/**
 * @file fields.cppm
 * @brief Ordered URL query parameters and form payloads built on the common curl container.
 */
export module mcr.fields;

import mcr.curl_container;
import std;

export namespace mcr {

    using mcr::Pair;
    using mcr::Parameter;

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

    /**
     * @brief Own form pairs, following cpr's Payload interface.
     * @note Inherits encode, Add(), and both GetContent() overloads from curl::CurlContainer.
     * Keys are emitted verbatim; values are optionally encoded and always follow an equals sign.
     */
    class Payload : public curl::CurlContainer<Pair> {
    public:
        /**
         * @brief Copy a range of form pairs in one pass, retaining order and duplicate keys.
         * @tparam Iterator Copyable input iterator whose dereferenced values can be passed to Add().
         * @param begin First pair in the range.
         * @param end Position past the last pair, of the same iterator type as begin.
         * @pre The range must be valid and end reachable from begin; equal iterators are allowed.
         * @note Add() copies each pair even when supplied through a move iterator.
         */
        template <typename Iterator>
        Payload(Iterator const begin, Iterator const end) {
            for (auto pair{ begin }; pair != end; ++pair) {
                Add(*pair);
            }
        }

        /**
         * @brief Copy a list of form pairs with encoding enabled.
         * @param pairs Initial entries; an empty list also permits construction as Payload{}.
         */
        Payload(std::initializer_list<Pair> const& pairs) : curl::CurlContainer<Pair>{ pairs } {}
    };

} // namespace mcr
