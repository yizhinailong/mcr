/**
 * @file body_view.cppm
 * @brief Borrow request-body bytes without taking ownership of their storage.
 */
export module mcr.body_view;

export import mcr.buffer;

import std;

export namespace mcr {

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
