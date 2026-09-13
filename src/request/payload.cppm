/**
 * @file payload.cppm
 * @brief Ordered form payloads built on the common curl container.
 */
export module mcr.payload;

export import mcr.curl_container;

import std;

export namespace mcr {

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
