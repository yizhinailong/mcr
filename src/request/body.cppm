/**
 * @file body.cppm
 * @brief Owned request bodies and borrowed byte views for HTTP transfers.
 */
export module mcr.body;

export import mcr.buffer;
export import mcr.file;
export import mcr.types;

export import mcr.error;
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
         * @return Owned body, or FILE_COULDNT_READ_FILE / READ_ERROR on failure.
         * @throws std::bad_alloc If body storage cannot be allocated.
         * @throws std::length_error If the contents exceed string capacity.
         * @note Empty files produce an empty body. The stream is closed on success and failure.
         */
        [[nodiscard]] static auto FromFile(File const& file) -> Result<Body> {
            Body          result;
            std::ifstream stream{ file.filepath, std::ios::binary };
            if (!stream) {
                return std::unexpected{
                    Error{ ErrorCode::FILE_COULDNT_READ_FILE, "Can't open the file for HTTP request body!" }
                };
            }

            std::array<char, 16 * 1024> chunk{};
            while (stream.read(chunk.data(), static_cast<std::streamsize>(chunk.size()))) {
                result.m_str.append(chunk.data(), chunk.size());
            }
            if (stream.bad() || !stream.eof()) {
                return std::unexpected{
                    Error{ ErrorCode::READ_ERROR, "Can't read the file for HTTP request body!" }
                };
            }
            result.m_str.append(chunk.data(), static_cast<std::size_t>(stream.gcount()));
            return result;
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

    /**
     * @brief Hold a non-owning request-body view, following cpr's BodyView interface.
     * @note Source bytes must remain alive at the same address until all consumers finish.
     * Copying or moving this trivially copyable type does not extend the source lifetime.
     */
    class BodyView final {
    private:
        std::string_view m_body{}; ///< Borrowed byte address and length, with no owned storage.

    public:
        /**
         * @brief Construct an empty view with a null data pointer.
         */
        constexpr BodyView() noexcept = default;

        /**
         * @brief Borrow a string view without scanning or copying its bytes.
         * @param body View to retain.
         */
        constexpr BodyView(std::string_view body) noexcept : m_body{ body } {}

        /**
         * @brief Borrow a null-terminated string, excluding its terminator.
         * @param body Nonnull pointer to a valid null-terminated string.
         */
        constexpr BodyView(char const* body) noexcept : m_body{ body } {}

        /**
         * @brief Borrow an exact byte range, including embedded null bytes.
         * @param str Pointer to at least len readable bytes; may be null when len is zero.
         * @param len Number of bytes to retain; no null terminator is required.
         */
        constexpr BodyView(char const* str, std::size_t len) noexcept : m_body{ str, len } {}

        /**
         * @brief Borrow a buffer's byte range without retaining its filename or descriptor.
         * @param buffer Descriptor whose data and datalen form a valid byte range.
         * @note The Buffer object may be destroyed first, provided its source bytes remain valid.
         */
        constexpr BodyView(Buffer const& buffer) noexcept : m_body{ buffer.data, buffer.datalen } {}

        /**
         * @brief Copy the borrowed address and length.
         * @param other View to copy.
         */
        constexpr BodyView(BodyView const& other) noexcept                    = default;

        /**
         * @brief Copy the borrowed address and length without transferring ownership.
         * @param other Source view.
         */
        constexpr BodyView(BodyView&& other) noexcept                         = default;

        /**
         * @brief Destroy the descriptor without releasing the source bytes.
         */
        ~BodyView()                                                           = default;

        /**
         * @brief Rebind to another view's bytes.
         * @param other Source view.
         * @return This view after assignment.
         */
        constexpr auto operator=(BodyView const& other) noexcept -> BodyView& = default;

        /**
         * @brief Rebind to another view's bytes.
         * @param other Source view.
         * @return This view after assignment.
         */
        constexpr auto operator=(BodyView&& other) noexcept -> BodyView&      = default;

        /**
         * @brief Return the borrowed address and length as a string view.
         * @return A view of the original source bytes, without a null-termination guarantee.
         */
        [[nodiscard]] constexpr auto Str() const noexcept -> std::string_view {
            return m_body;
        }
    };

} // namespace mcr
