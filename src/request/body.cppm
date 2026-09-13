/**
 * @file body.cppm
 * @brief Own request-body bytes copied from text, a buffer, or a binary file.
 */
export module mcr.body;

export import mcr.buffer;
export import mcr.file;
export import mcr.types;

import std;

export namespace mcr {

    /**
     * @brief Own a request body, following cpr's Body option and StringHolder interface.
     * @note Bytes are stored verbatim without encoding or content-type inference.
     * Source views, buffers, and files need not remain available after construction.
     */
    class Body : public StringHolder<Body> {
    public:
        /**
         * @brief Construct an empty request body.
         */
        Body() = default;

        /**
         * @brief Take ownership of a body string.
         * @param body Bytes to store.
         */
        Body(std::string body) : StringHolder<Body>(std::move(body)) {}

        /**
         * @brief Copy a body view, including embedded null bytes.
         * @param body View to copy.
         */
        Body(std::string_view body) : StringHolder<Body>(body) {}

        /**
         * @brief Copy a null-terminated body string.
         * @param body Nonnull pointer to a valid C string.
         */
        Body(char const* body) : StringHolder<Body>(body) {}

        /**
         * @brief Copy an exact byte range without requiring null termination.
         * @param str Pointer to at least len readable bytes; may be null when len is zero.
         * @param len Number of bytes to copy, including embedded null bytes.
         */
        Body(char const* str, std::size_t len) : StringHolder<Body>(str, len) {}

        /**
         * @brief Join body fragments without separators.
         * @param args Fragments to copy in order.
         */
        Body(std::initializer_list<std::string> args) : StringHolder<Body>(args) {}

        /**
         * @brief Copy a buffer's bytes into independent body storage.
         * @param buffer Descriptor whose data and datalen form a valid readable byte range.
         * @note Empty buffers are accepted; the filename is ignored.
         */
        Body(Buffer const& buffer) : StringHolder<Body>(buffer.data, buffer.datalen) {}

        /**
         * @brief Read a file to EOF in binary mode and own its complete contents.
         * @param file Descriptor whose filepath is opened; the filename override is ignored.
         * @throws std::invalid_argument If the file cannot be opened.
         * @throws std::runtime_error If reading fails before normal EOF.
         * @throws std::bad_alloc If body storage cannot be allocated.
         * @throws std::length_error If the contents exceed string capacity.
         * @note Empty files produce an empty body. The stream is closed on success and failure.
         */
        Body(File const& file) {
            std::ifstream stream{ file.filepath, std::ios::binary };
            if (!stream) {
                throw std::invalid_argument{ "Can't open the file for HTTP request body!" };
            }

            std::array<char, 16 * 1024> chunk{};
            while (stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()))) {
                m_str.append(chunk.data(), chunk.size());
            }
            if (stream.bad() || !stream.eof()) {
                throw std::runtime_error{ "Can't read the file for HTTP request body!" };
            }
            m_str.append(chunk.data(), static_cast<std::size_t>(stream.gcount()));
        }

        /**
         * @brief Copy body bytes into independent storage.
         * @param other Body to copy.
         */
        Body(Body const& other)                        = default;

        /**
         * @brief Move owned body storage.
         * @param other Body to move from.
         */
        Body(Body&& other) noexcept                    = default;

        /**
         * @brief Release owned body storage, including derived state when used polymorphically.
         */
        ~Body() override                               = default;

        /**
         * @brief Copy body bytes.
         * @param other Source body.
         * @return This body after assignment.
         */
        auto operator=(Body const& other) -> Body&     = default;

        /**
         * @brief Move body storage.
         * @param other Source body.
         * @return This body after assignment.
         */
        auto operator=(Body&& other) noexcept -> Body& = default;
    };

} // namespace mcr
