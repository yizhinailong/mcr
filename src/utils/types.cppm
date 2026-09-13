/**
 * @file types.cppm
 * @brief Curl-compatible transfer types, owned string options, and HTTP headers.
 */
module;

#include <curl/curl.h>

export module mcr.types;

import std;

export namespace mcr {

    /**
     * @brief Curl's transfer offset type, available without including curl headers.
     */
    using CprOffT   = curl_off_t;

    /**
     * @brief Integral progress callback argument type used by the current curl dependency.
     */
    using CprPfArgT = CprOffT;

    /**
     * @brief An owned string whose concatenation preserves its derived option type.
     * @tparam T Derived option type, such as Url, which can construct this base.
     */
    template <typename T>
    class StringHolder {
    private:
        friend T;

        /**
         * @brief Construct an empty string for the derived option.
         */
        StringHolder() = default;

        /**
         * @brief Take ownership of a string.
         * @param str String to store.
         */
        explicit StringHolder(std::string str) : m_str{ std::move(str) } {}

        /**
         * @brief Copy a string view, including any embedded null bytes.
         * @param str View to copy; its lifetime need not outlive this object.
         */
        explicit StringHolder(std::string_view str) : m_str{ str } {}

        /**
         * @brief Copy a null-terminated string.
         * @param str Pointer to a valid null-terminated string.
         */
        explicit StringHolder(char const* str) : m_str{ str } {}

        /**
         * @brief Copy a specified number of bytes.
         * @param str Pointer to at least len readable bytes.
         * @param len Number of bytes to copy, including any embedded null bytes.
         */
        StringHolder(char const* str, std::size_t len) : m_str{ str, len } {}

        /**
         * @brief Join string fragments without separators.
         * @param args Fragments to append in order.
         */
        StringHolder(std::initializer_list<std::string> args) {
            for (auto const& arg : args) {
                m_str += arg;
            }
        }

        StringHolder(StringHolder const& other)     = default;
        StringHolder(StringHolder&& other) noexcept = default;

    public:
        virtual ~StringHolder()                                = default;

        StringHolder& operator=(StringHolder const& other)     = default;
        StringHolder& operator=(StringHolder&& other) noexcept = default;

        /**
         * @brief Copy the stored text into a standard string.
         * @return An independent copy of the text.
         */
        [[nodiscard]] explicit operator std::string() const {
            return m_str;
        }

        /**
         * @brief Concatenate a null-terminated string while preserving the option type.
         * @param rhs Null-terminated suffix to append.
         * @return A new option containing the combined text.
         */
        [[nodiscard]] T operator+(char const* rhs) const {
            return T(m_str + rhs);
        }

        /**
         * @brief Concatenate a standard string while preserving the option type.
         * @param rhs Suffix to append.
         * @return A new option containing the combined text.
         */
        [[nodiscard]] T operator+(std::string const& rhs) const {
            return T(m_str + rhs);
        }

        /**
         * @brief Concatenate another option of the same type.
         * @param rhs Option whose text is appended.
         * @return A new option containing the combined text.
         */
        [[nodiscard]] T operator+(StringHolder<T> const& rhs) const {
            return T(m_str + rhs.m_str);
        }

        /**
         * @brief Append a null-terminated string in place.
         * @param rhs Null-terminated suffix to append.
         */
        void operator+=(char const* rhs) {
            m_str += rhs;
        }

        /**
         * @brief Append a standard string in place.
         * @param rhs Suffix to append.
         */
        void operator+=(std::string const& rhs) {
            m_str += rhs;
        }

        /**
         * @brief Append another option's text in place.
         * @param rhs Option of the same type whose text is appended.
         */
        void operator+=(StringHolder<T> const& rhs) {
            m_str += rhs.m_str;
        }

        /**
         * @brief Compare the text with a null-terminated string.
         * @param rhs Null-terminated string to compare.
         * @return True if both strings have identical contents.
         */
        [[nodiscard]] bool operator==(char const* rhs) const {
            return m_str == rhs;
        }

        /**
         * @brief Compare the text with a standard string.
         * @param rhs String to compare.
         * @return True if both strings have identical contents.
         */
        [[nodiscard]] bool operator==(std::string const& rhs) const {
            return m_str == rhs;
        }

        /**
         * @brief Compare the text with another option of the same type.
         * @param rhs Option to compare.
         * @return True if both options have identical contents.
         */
        [[nodiscard]] bool operator==(StringHolder<T> const& rhs) const {
            return m_str == rhs.m_str;
        }

        /**
         * @brief Compare string contents for inequality, independent of pointer identity.
         * @param rhs Null-terminated string to compare.
         * @return True if the string contents differ.
         */
        [[nodiscard]] bool operator!=(char const* rhs) const {
            return m_str != rhs;
        }

        /**
         * @brief Compare the text with a standard string for inequality.
         * @param rhs String to compare.
         * @return True if the string contents differ.
         */
        [[nodiscard]] bool operator!=(std::string const& rhs) const {
            return m_str != rhs;
        }

        /**
         * @brief Compare two options of the same type for inequality.
         * @param rhs Option to compare.
         * @return True if the stored contents differ.
         */
        [[nodiscard]] bool operator!=(StringHolder<T> const& rhs) const {
            return m_str != rhs.m_str;
        }

        /**
         * @brief Access the owned text without copying or permitting mutation.
         * @return A reference to the stored string.
         */
        [[nodiscard]] std::string const& Str() {
            return m_str;
        }

        /**
         * @brief Access the owned text of a const option.
         * @return A reference to the stored string.
         */
        [[nodiscard]] std::string const& Str() const {
            return m_str;
        }

        /**
         * @brief Access the text as a null-terminated string.
         * @return A pointer into owned storage, subject to std::string invalidation rules.
         */
        [[nodiscard]] char const* CStr() const {
            return m_str.c_str();
        }

        /**
         * @brief Access the contiguous stored bytes.
         * @return A pointer to Str().size() bytes followed by a null terminator.
         */
        [[nodiscard]] char const* Data() const {
            return m_str.data();
        }

    protected:
        std::string m_str; ///< Owned text, available to derived option types as in cpr.
    };

    /**
     * @brief Write all stored bytes to an output stream.
     * @tparam T Derived string option type.
     * @param os Stream to write to.
     * @param value Option whose text is written.
     * @return The supplied stream, allowing chained output.
     */
    template <typename T>
    std::ostream& operator<<(std::ostream& os, StringHolder<T> const& value) {
        return os << value.Str();
    }

    /**
     * @brief An owned URL string; construction does not validate or encode the URL.
     */
    class Url : public StringHolder<Url> {
    public:
        /**
         * @brief Construct an empty URL.
         */
        Url() = default;

        /**
         * @brief Construct a URL by taking ownership of a string.
         * @param url URL text to store.
         */
        Url(std::string url) : StringHolder<Url>(std::move(url)) {}

        /**
         * @brief Construct a URL by copying a string view.
         * @param url View of the URL text to copy.
         */
        Url(std::string_view url) : StringHolder<Url>(url) {}

        /**
         * @brief Construct a URL by copying a null-terminated string.
         * @param url Pointer to valid null-terminated URL text.
         */
        Url(char const* url) : StringHolder<Url>(url) {}

        /**
         * @brief Construct a URL from a byte range.
         * @param str Pointer to at least len readable bytes.
         * @param len Number of bytes to copy, including any embedded null bytes.
         */
        Url(char const* str, std::size_t len) : StringHolder<Url>(str, len) {}

        /**
         * @brief Construct a URL by joining fragments without separators.
         * @param args URL fragments to append in order.
         */
        Url(std::initializer_list<std::string> args) : StringHolder<Url>(args) {}

        Url(Url const& other)                = default;
        Url(Url&& other) noexcept            = default;
        ~Url() override                      = default;

        Url& operator=(Url const& other)     = default;
        Url& operator=(Url&& other) noexcept = default;
    };

    /**
     * @brief An owned User-Agent string; construction does not validate or modify the text.
     */
    class UserAgent : public StringHolder<UserAgent> {
    public:
        /**
         * @brief Construct an empty User-Agent.
         */
        UserAgent() = default;

        /**
         * @brief Construct a User-Agent by taking ownership of a string.
         * @param user_agent User-Agent text to store.
         */
        UserAgent(std::string user_agent) : StringHolder<UserAgent>(std::move(user_agent)) {}

        /**
         * @brief Construct a User-Agent by copying a string view.
         * @param user_agent View of the User-Agent text to copy.
         */
        UserAgent(std::string_view user_agent) : StringHolder<UserAgent>(user_agent) {}

        /**
         * @brief Construct a User-Agent by copying a null-terminated string.
         * @param user_agent Pointer to valid null-terminated User-Agent text.
         */
        UserAgent(char const* user_agent) : StringHolder<UserAgent>(user_agent) {}

        /**
         * @brief Construct a User-Agent from a byte range.
         * @param str Pointer to at least len readable bytes.
         * @param len Number of bytes to copy, including any embedded null bytes.
         */
        UserAgent(char const* str, std::size_t len) : StringHolder<UserAgent>(str, len) {}

        /**
         * @brief Construct a User-Agent by joining fragments without separators.
         * @param args User-Agent fragments to append in order.
         */
        UserAgent(std::initializer_list<std::string> args) : StringHolder<UserAgent>(args) {}

        UserAgent(UserAgent const& other)                = default;
        UserAgent(UserAgent&& other) noexcept            = default;
        ~UserAgent() override                            = default;

        UserAgent& operator=(UserAgent const& other)     = default;
        UserAgent& operator=(UserAgent&& other) noexcept = default;
    };

    /**
     * @brief Order header names lexicographically, ignoring case as in cpr.
     * @note Uses std::tolower in the current C locale, which must remain stable
     * while a Header contains keys.
     */
    struct CaseInsensitiveCompare {
        /**
         * @brief Compare two names after case folding each unsigned byte.
         * @param a Left header name.
         * @param b Right header name.
         * @return True if a precedes b; false for equivalent names.
         */
        [[nodiscard]] bool operator()(std::string const& a, std::string const& b) const noexcept {
            return std::lexicographical_compare(
                a.begin(),
                a.end(),
                b.begin(),
                b.end(),
                [](unsigned char ac, unsigned char bc) {
                    return std::tolower(ac) < std::tolower(bc);
                }
            );
        }
    };

    /**
     * @brief Owned HTTP header values indexed by case-insensitive names.
     * @note Equivalent names share one entry; key spelling and value case are preserved.
     */
    using Header = std::map<std::string, std::string, CaseInsensitiveCompare>;

} // namespace mcr
