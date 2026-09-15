/**
 * @file buffer.cppm
 * @brief Borrow a contiguous byte range with an owned multipart filename.
 */
export module mcr.buffer;

export import mcr.error;
import std;

export namespace mcr {

    /**
     * @brief Describe borrowed upload bytes and own their filename, following cpr's Buffer interface.
     * @note The source storage must remain alive and at the same address until all users finish
     * reading it. Copies and moves borrow the same bytes; only the filename is owned.
     */
    struct Buffer {
        using data_t = char const*; ///< Read-only view of the source bytes, retaining cpr's alias.

        /**
         * @brief Borrow a byte range without copying its contents or accessing the filesystem.
         * @tparam Iterator Contiguous iterator over elements whose size is one byte.
         * @param begin First byte in the source range.
         * @param end Position past the last byte; equal iterators describe an empty buffer.
         * @param filename_param Filename to own, supplied as a temporary or moved standard path.
         * @pre Iterators must belong to the same live contiguous sequence, or both be null pointers.
         * @return Borrowed buffer, or BAD_FUNCTION_ARGUMENT for a reversed range.
         * @note Invalid iterator and element types are rejected by constructor static assertions.
         */
        template <typename Iterator>
        [[nodiscard]] static auto Create(Iterator begin, Iterator end, std::filesystem::path filename_param) -> Result<Buffer> {
            static_assert(std::contiguous_iterator<Iterator>, "Buffer requires contiguous iterators");
            static_assert(sizeof(std::iter_value_t<Iterator>) == 1, "Only byte buffers can be used");
            if (begin == end) {
                return Buffer{ nullptr, 0, std::move(filename_param) };
            }
            auto const length{ end - begin };
            if (length < 0) {
                return std::unexpected{
                    Error{ ErrorCode::BAD_FUNCTION_ARGUMENT, "Buffer end must not precede begin" }
                };
            }
            return Buffer{ reinterpret_cast<data_t>(std::to_address(begin)), static_cast<std::size_t>(length), std::move(filename_param) };
        }

    private:
        /**
         * @brief Store a validated borrowed byte range.
         */
        Buffer(data_t bytes, std::size_t size, std::filesystem::path name)
            : data{ bytes }, datalen{ size }, filename{ std::move(name) } {}

    public:
        data_t                      data;     ///< Borrowed byte address, null for an empty range at construction.
        std::size_t                 datalen;  ///< Number of bytes, including any embedded nulls in the range.
        std::filesystem::path const filename; ///< Owned, immutable upload filename, stored without normalization.
    };

} // namespace mcr
