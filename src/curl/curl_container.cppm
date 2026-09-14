/**
 * @file curl_container.cppm
 * @brief Ordered parameter and form-pair storage with cpr-compatible serialization.
 */
export module mcr.curl_container;

export import mcr.curlholder;

import std;

export namespace mcr {

    /**
     * @brief Own a query parameter whose empty value is serialized without an equals sign.
     */
    struct Parameter {
        /**
         * @brief Take ownership of a parameter's key and value.
         * @param key_param Key bytes to store without normalization.
         * @param value_param Value bytes to store, including an empty string.
         */
        Parameter(std::string key_param, std::string value_param)
            : key{ std::move(key_param) }, value{ std::move(value_param) } {}

        std::string key;   ///< Publicly mutable, owned key bytes.
        std::string value; ///< Publicly mutable, owned value bytes.
    };

    /**
     * @brief Own a form pair whose key and value are always separated by an equals sign.
     */
    struct Pair {
        /**
         * @brief Take ownership of a form pair's key and value.
         * @param key_param Key bytes, emitted verbatim even when encoding is enabled.
         * @param value_param Value bytes to store, including an empty string.
         */
        Pair(std::string key_param, std::string value_param)
            : key{ std::move(key_param) }, value{ std::move(value_param) } {}

        std::string key;   ///< Publicly mutable, owned key bytes.
        std::string value; ///< Publicly mutable, owned value bytes.
    };

} // namespace mcr

export namespace mcr::curl {

    /**
     * @brief Own ordered query parameters or form pairs with optional percent encoding.
     * @tparam T Parameter or Pair, the two element types supported by cpr's implementation.
     * @note Retains duplicates and empty entries without sorting or validation.
     * The protected storage is available to derived Parameters and Payload implementations.
     */
    template <typename T>
    requires(std::same_as<T, Parameter> || std::same_as<T, Pair>)
    class CurlContainer {
    public:
        bool encode{ true }; ///< Encode parameter keys/values or pair values when a holder is supplied.

        /**
         * @brief Construct an empty container with encoding enabled.
         */
        CurlContainer() = default;

        /**
         * @brief Copy the supplied elements in their original order.
         * @param elements Initial parameter or pair values, including an empty list.
         */
        CurlContainer(std::initializer_list<T> const& elements) : m_container_list{ elements } {}

        /**
         * @brief Append copies of a list of elements without replacing existing keys.
         * @param elements Values to append in order; an empty list has no effect.
         */
        auto Add(std::initializer_list<T> const& elements) -> void {
            m_container_list.append_range(elements);
        }

        /**
         * @brief Append a copy of one element without changing the caller's value.
         * @param element Parameter or pair to append, including a duplicate key.
         */
        auto Add(T const& element) -> void {
            m_container_list.push_back(element);
        }

        /**
         * @brief Serialize the elements, applying percent encoding when encode is true.
         * @param holder Curl holder used only when encoding a component.
         * @return Owned content with no question-mark prefix and cpr's ampersand separators.
         * @throws std::logic_error If encoding a component with a moved-from holder.
         * @throws std::length_error If an encoded component exceeds curl's int length limit.
         * @note Parameter encodes keys and nonempty values; Pair only encodes values.
         * Curl encoding allocation failures retain CurlHolder's empty-component behavior.
         */
        [[nodiscard]] auto GetContent(CurlHolder const& holder) const -> std::string {
            return getContent(encode ? &holder : nullptr);
        }

        /**
         * @brief Serialize keys and values verbatim, ignoring encode and requiring no curl holder.
         * @return Owned content with cpr's type-specific equals-sign and separator rules.
         */
        [[nodiscard]] auto GetContent() const -> std::string {
            return getContent(nullptr);
        }

    protected:
        std::vector<T> m_container_list; ///< Owned elements in insertion order; accessible to derived containers.

    private:
        /**
         * @brief Append raw bytes or their curl percent-encoded representation.
         * @param output Destination string.
         * @param input Component bytes, including embedded nulls.
         * @param holder Encoding helper, or null to append the input verbatim.
         */
        static auto appendComponent(std::string& output, std::string_view input, CurlHolder const* holder) -> void {
            if (holder) {
                auto const escaped{ holder->UrlEncode(input) };
                output.append(escaped.data(), escaped.size());
            } else {
                output.append(input);
            }
        }

        /**
         * @brief Apply the distinct query-parameter and form-pair serialization policies.
         * @param holder Encoding helper, or null to disable encoding for this call.
         * @return A newly allocated string without changing stored elements.
         */
        auto getContent(CurlHolder const* holder) const -> std::string {
            std::string content;
            for (auto const& element : m_container_list) {
                // cpr bases separators on emitted text, so leading empty parameters disappear.
                if (!content.empty()) {
                    content += '&';
                }
                if constexpr (std::same_as<T, Parameter>) {
                    appendComponent(content, element.key, holder);
                    if (element.value.empty()) {
                        continue;
                    }
                } else {
                    content += element.key;
                }
                content += '=';
                appendComponent(content, element.value, holder);
            }
            return content;
        }
    };

} // namespace mcr::curl
