/**
 * @file multipart.cppm
 * @brief Multipart descriptors for owned text, file paths, and borrowed byte buffers.
 */
export module mcr.multipart;

export import mcr.buffer;
export import mcr.file;

import std;

export namespace mcr {

    /**
     * @brief Describe a multipart field, following cpr's public Part representation.
     * @note Text and file descriptors are owned. Buffer data is borrowed and must remain valid
     * until consumers finish reading it. Construction performs no file or network I/O.
     */
    struct Part {
        /**
         * @brief Copy a text field without encoding or interpreting its bytes.
         * @param name_param Field name to own.
         * @param value_param Field value to own, including embedded null bytes.
         * @param content_type_param Optional content type; an empty view leaves it unspecified.
         */
        Part(std::string_view name_param, std::string_view value_param, std::string_view content_type_param = {})
            : name{ name_param }, value{ value_param }, content_type{ content_type_param } {}

        /**
         * @brief Store an integer field as signed decimal text.
         * @param name_param Field name to own.
         * @param value_param Integer to format, including the full int32_t range.
         * @param content_type_param Optional content type to own.
         */
        Part(std::string_view name_param, std::int32_t value_param, std::string_view content_type_param = {})
            : name{ name_param }, value{ std::to_string(value_param) }, content_type{ content_type_param } {}

        /**
         * @brief Copy a collection of file descriptors into a file field.
         * @param name_param Field name shared by the files.
         * @param files_param Ordered file descriptors to copy, including an empty collection.
         * @param content_type_param Optional content type to own.
         * @note A File implicitly converts to a one-element Files collection.
         */
        Part(std::string_view name_param, Files const& files_param, std::string_view content_type_param = {})
            : name{ name_param }, content_type{ content_type_param }, is_file{ true }, files{ files_param } {}

        /**
         * @brief Move a collection of file descriptors into a file field.
         * @param name_param Field name shared by the files.
         * @param files_param Ordered file descriptors whose storage is transferred.
         * @param content_type_param Optional content type to own.
         */
        Part(std::string_view name_param, Files&& files_param, std::string_view content_type_param = {})
            : name{ name_param }, content_type{ content_type_param }, is_file{ true }, files{ std::move(files_param) } {}

        /**
         * @brief Borrow buffer bytes and own the buffer's filename as a string.
         * @param name_param Field name to own.
         * @param buffer Descriptor whose address and length are retained without copying its bytes.
         * @param content_type_param Optional content type to own.
         * @note value stores buffer.filename.string(), including any directory components.
         */
        Part(std::string_view name_param, Buffer const& buffer, std::string_view content_type_param = {})
            : name{ name_param }, value{ buffer.filename.string() }, content_type{ content_type_param }, data{ buffer.data }, datalen{ buffer.datalen }, is_buffer{ true } {}

        std::string    name;               ///< Owned field name.
        std::string    value;              ///< Owned text or buffer filename; empty for a file field.
        std::string    content_type;       ///< Owned optional content type, empty when unspecified.
        Buffer::data_t data{ nullptr };    ///< Borrowed byte address, used only for buffer fields.
        std::size_t    datalen{ 0 };       ///< Borrowed byte count, zero for text and file fields.
        bool           is_file{ false };   ///< True for a field containing file descriptors.
        bool           is_buffer{ false }; ///< True for a field containing borrowed buffer data.
        Files          files;              ///< Owned ordered file descriptors, empty for text and buffer fields.
    };

    /**
     * @brief Own an ordered collection of multipart descriptors, retaining duplicate field names.
     * @note Copying parts owns their strings and files but continues borrowing buffer data.
     * Empty brace construction uses the initializer-list constructor; there is no default constructor.
     */
    class Multipart {
    public:
        /**
         * @brief Copy a list of descriptors in order.
         * @param parts_param Parts to copy, including an empty list.
         */
        Multipart(std::initializer_list<Part> const& parts_param) : parts{ parts_param } {}

        /**
         * @brief Copy a vector of descriptors.
         * @param parts_param Parts whose strings and files are copied.
         */
        explicit Multipart(std::vector<Part> const& parts_param) : parts{ parts_param } {}

        /**
         * @brief Transfer a vector of descriptors without copying.
         * @param parts_param Vector whose storage is moved.
         */
        explicit Multipart(std::vector<Part>&& parts_param) noexcept : parts{ std::move(parts_param) } {}

        std::vector<Part> parts; ///< Publicly mutable descriptors in insertion order.
    };

} // namespace mcr
