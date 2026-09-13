/**
 * @file json.cppm
 * @brief JSON values, owned request bodies, and parsing diagnostics.
 */
export module mcr.json;

import nlohmann.json;
import std;

export namespace mcr {

    using Json = nlohmann::json; ///< JSON value with nlohmann's construction and conversion APIs.

    /**
     * @brief Own the compact serialization of a JSON value for an HTTP request.
     * @note Serialization occurs during construction. Later changes to the source value
     * cannot affect this body. A JSON string value is serialized with quotes and escaping;
     * already serialized text must first be parsed or sent using Body and an explicit header.
     */
    class JsonBody final {
    private:
        std::string m_text; ///< Independent serialized request bytes.

    public:
        /**
         * @brief Serialize a JSON value into independent UTF-8 request bytes.
         * @param value Value to serialize using nlohmann's default compact, strict encoding.
         * @throws Json::type_error If a string contains invalid UTF-8.
         * @throws std::bad_alloc If storage cannot be allocated.
         * @note Session supplies application/json when this body is sent without an explicit Content-Type.
         */
        explicit JsonBody(Json const& value) : m_text{ value.dump() } {}

        /**
         * @brief Inspect the serialized bytes without copying them.
         * @return A reference valid until this body is modified, moved from, or destroyed.
         */
        [[nodiscard]] auto Str() const noexcept -> std::string const& { return m_text; }
    };

    /**
     * @brief Describe a JSON parsing failure independently of HTTP and transport outcomes.
     */
    struct JsonError {
        int                        id{};    ///< Exception identifier supplied by nlohmann.
        std::string                message; ///< Owned diagnostic message.
        std::optional<std::size_t> byte;    ///< One-based parse-error byte, possibly input size + 1 at EOF; absent for other JSON errors.
    };

} // namespace mcr
