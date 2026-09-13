/**
 * @file callback.cppm
 * @brief Transfer callback options and shared cancellation state, following cpr's interfaces.
 */
export module mcr.callback;

export import mcr.types;

import std;

export namespace mcr {

    /**
     * @brief Provide upload bytes through a callback with a mutable byte count.
     * @note The buffer and count are forwarded unchanged, and consumer exceptions propagate.
     */
    class ReadCallback {
    public:
        /**
         * @brief Construct an empty callback with a declared upload size of zero.
         */
        ReadCallback() = default;

        /**
         * @brief Store an upload producer with an unknown total size.
         * @param callback_param Producer returning true to continue or false to abort.
         * @param userdata_param Opaque value passed unchanged to the producer.
         */
        ReadCallback(std::function<bool(char*, std::size_t&, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, size{ -1 }, callback{ std::move(callback_param) } {}

        /**
         * @brief Store an upload producer with a declared total size.
         * @param size_param Total upload byte count, or -1 for an unknown size; stored without validation.
         * @param callback_param Producer returning true to continue or false to abort.
         * @param userdata_param Opaque value passed unchanged to the producer.
         */
        ReadCallback(CprOffT size_param, std::function<bool(char*, std::size_t&, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, size{ size_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Ask the producer to fill an upload buffer.
         * @param buffer Writable storage for the producer.
         * @param buffer_size Available capacity on entry; the producer sets the number of bytes written.
         * A count of zero with a true return indicates end of input.
         * @return The producer's result, or true without changing the buffer or count if no producer is set.
         * @pre The producer must not write or report more bytes than the supplied capacity.
         */
        [[nodiscard]] auto operator()(char* buffer, std::size_t& buffer_size) const -> bool {
            if (!callback) {
                return true;
            }
            return callback(buffer, buffer_size, userdata);
        }

        std::intptr_t                                           userdata{}; ///< Publicly mutable opaque value passed to the producer.
        CprOffT                                                 size{};     ///< Declared upload size; zero by default and -1 for the callback-only constructor.
        std::function<bool(char*, std::size_t&, std::intptr_t)> callback;   ///< Upload producer; may be replaced or cleared.
    };

    /**
     * @brief Consume raw response header chunks without copying or modifying their bytes.
     * @note Views are borrowed for the duration of the call; consumer exceptions propagate.
     */
    class HeaderCallback {
    public:
        /**
         * @brief Construct a callback that accepts headers without processing them.
         */
        HeaderCallback() = default;

        /**
         * @brief Store a response header consumer.
         * @param callback_param Consumer returning true to continue or false to abort.
         * @param userdata_param Opaque value passed unchanged to the consumer.
         */
        HeaderCallback(std::function<bool(std::string_view, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Forward a raw header chunk and the current user data.
         * @param header Borrowed header bytes, preserving line terminators and embedded nulls.
         * @return The consumer's result, or true when no consumer is installed.
         */
        [[nodiscard]] auto operator()(std::string_view header) const -> bool {
            if (!callback) {
                return true;
            }
            return callback(header, userdata);
        }

        std::intptr_t                                        userdata{}; ///< Publicly mutable opaque value passed to the consumer.
        std::function<bool(std::string_view, std::intptr_t)> callback;   ///< Header consumer; may be replaced or cleared.
    };

    /**
     * @brief Consume response body chunks without copying or modifying their bytes.
     * @note Views are borrowed for the duration of the call; consumer exceptions propagate.
     */
    class WriteCallback {
    public:
        /**
         * @brief Construct a callback that accepts body chunks without processing them.
         */
        WriteCallback() = default;

        /**
         * @brief Store a response body consumer.
         * @param callback_param Consumer returning true to continue or false to abort.
         * @param userdata_param Opaque value passed unchanged to the consumer.
         */
        WriteCallback(std::function<bool(std::string_view, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Forward a body chunk and the current user data.
         * @param data Borrowed response bytes, including any embedded nulls.
         * @return The consumer's result, or true when no consumer is installed.
         */
        [[nodiscard]] auto operator()(std::string_view data) const -> bool {
            if (!callback) {
                return true;
            }
            return callback(data, userdata);
        }

        std::intptr_t                                        userdata{}; ///< Publicly mutable opaque value passed to the consumer.
        std::function<bool(std::string_view, std::intptr_t)> callback;   ///< Body consumer; may be replaced or cleared.
    };

    /**
     * @brief Report transfer progress using the project's curl-compatible counter type.
     * @note Counter values are forwarded without conversion, and consumer exceptions propagate.
     */
    class ProgressCallback {
    public:
        /**
         * @brief Construct a callback that permits the transfer to continue.
         */
        ProgressCallback() = default;

        /**
         * @brief Store a progress observer.
         * @param callback_param Observer receiving download total, download current, upload total,
         * upload current, and user data; returns true to continue or false to abort.
         * @param userdata_param Opaque value passed unchanged to the observer.
         */
        ProgressCallback(std::function<bool(CprPfArgT, CprPfArgT, CprPfArgT, CprPfArgT, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Forward all four progress counters and the current user data.
         * @param download_total Total download byte count reported by curl.
         * @param download_now Downloaded byte count reported by curl.
         * @param upload_total Total upload byte count reported by curl.
         * @param upload_now Uploaded byte count reported by curl.
         * @return The observer's result, or true when no observer is installed.
         */
        [[nodiscard]] auto operator()(CprPfArgT download_total, CprPfArgT download_now, CprPfArgT upload_total, CprPfArgT upload_now) const -> bool {
            if (!callback) {
                return true;
            }
            return callback(download_total, download_now, upload_total, upload_now, userdata);
        }

        std::intptr_t                                                                  userdata{}; ///< Publicly mutable opaque value passed to the observer.
        std::function<bool(CprPfArgT, CprPfArgT, CprPfArgT, CprPfArgT, std::intptr_t)> callback;   ///< Progress observer; may be replaced or cleared.
    };

    /**
     * @brief Receive typed transfer diagnostics using cpr's curl-compatible information values.
     * @note Views are borrowed for the duration of the call; consumer exceptions propagate.
     */
    class DebugCallback {
    public:
        /**
         * @brief Diagnostic categories matching curl_infotype values.
         */
        enum class InfoType : std::uint8_t {
            TEXT         = 0, ///< Informational diagnostic text.
            HEADER_IN    = 1, ///< Incoming protocol headers.
            HEADER_OUT   = 2, ///< Outgoing protocol headers.
            DATA_IN      = 3, ///< Incoming protocol data.
            DATA_OUT     = 4, ///< Outgoing protocol data.
            SSL_DATA_IN  = 5, ///< Incoming TLS data.
            SSL_DATA_OUT = 6, ///< Outgoing TLS data.
        };

        /**
         * @brief Construct a callback that ignores diagnostics.
         */
        DebugCallback() = default;

        /**
         * @brief Store a diagnostic consumer.
         * @param callback_param Consumer receiving category, raw bytes, and user data.
         * @param userdata_param Opaque value passed unchanged to the consumer.
         */
        DebugCallback(std::function<void(InfoType, std::string_view, std::intptr_t)> callback_param, std::intptr_t userdata_param = 0)
            : userdata{ userdata_param }, callback{ std::move(callback_param) } {}

        /**
         * @brief Forward a diagnostic category and its raw bytes.
         * @param type Diagnostic category, forwarded without validation.
         * @param data Borrowed diagnostic bytes, including any embedded nulls.
         */
        auto operator()(InfoType type, std::string_view data) const -> void {
            if (!callback) {
                return;
            }
            callback(type, data, userdata);
        }

        std::intptr_t                                                  userdata{}; ///< Publicly mutable opaque value passed to the consumer.
        std::function<void(InfoType, std::string_view, std::intptr_t)> callback;   ///< Diagnostic consumer; may be replaced or cleared.
    };

    /**
     * @brief Combine a shared cancellation flag with an optional borrowed progress observer.
     * @note Cancellation is checked before invoking the observer, as in cpr.
     * Unlike cpr's null dereference, a missing cancellation state is treated as not cancelled.
     * Only the flag is atomic; rebinding or modifying the observer must be synchronized by the caller.
     */
    class CancellationCallback {
    private:
        std::shared_ptr<std::atomic_bool>                       m_cancellation_state; ///< Shared flag; true requests cancellation.
        std::optional<std::reference_wrapper<ProgressCallback>> m_user_cb;            ///< Borrowed observer, observed rather than copied.

    public:
        /**
         * @brief Construct a callback with no cancellation flag or progress observer.
         */
        CancellationCallback() = default;

        /**
         * @brief Take shared ownership of a cancellation flag.
         * @param cancellation_state Flag to observe; an empty pointer means no cancellation flag.
         */
        explicit CancellationCallback(std::shared_ptr<std::atomic_bool>&& cancellation_state)
            : m_cancellation_state{ std::move(cancellation_state) } {}

        /**
         * @brief Take shared ownership of a cancellation flag and borrow a progress observer.
         * @param cancellation_state Flag to observe; an empty pointer means no cancellation flag.
         * @param user_callback Observer invoked only when the flag is not set.
         * @pre user_callback must outlive every invocation that references it, including through copies.
         */
        CancellationCallback(std::shared_ptr<std::atomic_bool>&& cancellation_state, ProgressCallback& user_callback)
            : m_cancellation_state{ std::move(cancellation_state) }, m_user_cb{ std::ref(user_callback) } {}

        /**
         * @brief Check cancellation, then forward progress if an observer is bound.
         * @param download_total Total download byte count.
         * @param download_now Downloaded byte count.
         * @param upload_total Total upload byte count.
         * @param upload_now Uploaded byte count.
         * @return False if cancelled or the observer returns false; true otherwise.
         * @note Observer exceptions propagate unless cancellation prevents its invocation.
         */
        [[nodiscard]] auto operator()(CprPfArgT download_total, CprPfArgT download_now, CprPfArgT upload_total, CprPfArgT upload_now) const -> bool {
            if (m_cancellation_state && m_cancellation_state->load()) {
                return false;
            }
            return !m_user_cb || m_user_cb->get()(download_total, download_now, upload_total, upload_now);
        }

        /**
         * @brief Bind or replace the borrowed progress observer.
         * @param user_callback Observer whose current function and user data are used on each invocation.
         * @pre user_callback must outlive every invocation that references it, including through copies.
         */
        auto SetProgressCallback(ProgressCallback& user_callback) -> void {
            m_user_cb = std::ref(user_callback);
        }
    };

} // namespace mcr
