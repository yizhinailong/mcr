/**
 * @file file.cppm
 * @brief File-upload descriptors and ordered file collections using only the standard library.
 */
export module mcr.file;

import std;

export namespace mcr {

    /**
     * @brief Store a file path and an optional filename to send with a multipart upload.
     * @note Paths and filenames are stored verbatim without filesystem access or normalization.
     * The public overriden_filename spelling is retained from cpr.
     */
    struct File {
        /**
         * @brief Own a file path and copy an optional upload filename.
         * @param filepath_param Path text to store; empty paths are accepted.
         * @param overriden_filename_param Filename override, with an empty view meaning no override.
         */
        explicit File(std::string filepath_param, std::string_view overriden_filename_param = {})
            : filepath{ std::move(filepath_param) }, overriden_filename{ overriden_filename_param } {}

        std::string filepath;           ///< Publicly mutable path text; does not own an open file.
        std::string overriden_filename; ///< Publicly mutable upload filename override, empty when absent.

        /**
         * @brief Check whether an upload filename override is present.
         * @return True exactly when overriden_filename is nonempty.
         */
        [[nodiscard]] auto HasOverridenFilename() const noexcept -> bool {
            return !overriden_filename.empty();
        }
    };

    /**
     * @brief Own an ordered collection of file descriptors, following cpr's Files interface.
     * @note Standard container method names are retained. Duplicates and empty paths are allowed;
     * references and iterators follow std::vector's invalidation rules.
     */
    class Files {
    private:
        std::vector<File> m_files; ///< Owned file descriptors in insertion order.

    public:
        /**
         * @brief Construct an empty file collection.
         */
        Files() = default;

        /**
         * @brief Implicitly construct a one-file collection.
         * @param file Descriptor to copy.
         */
        Files(File const& file) : m_files{ file } {}

        /**
         * @brief Copy all file descriptors into independent storage.
         * @param other Collection to copy.
         */
        Files(Files const& other)     = default;

        /**
         * @brief Transfer the owned collection.
         * @param other Collection to move from.
         */
        Files(Files&& other) noexcept = default;

        /**
         * @brief Copy an ordered list of descriptors.
         * @param files File descriptors to retain.
         */
        Files(std::initializer_list<File> const& files) : m_files{ files } {}

        /**
         * @brief Copy paths into descriptors without filename overrides.
         * @param filepaths Path strings to retain in order, including duplicates and empty strings.
         */
        Files(std::initializer_list<std::string> const& filepaths) : m_files{ filepaths.begin(), filepaths.end() } {}

        /**
         * @brief Release the owned descriptor storage.
         */
        ~Files() noexcept = default;

        /**
         * @brief Replace this collection with an independent copy.
         * @param other Collection to copy; self-assignment leaves it unchanged.
         * @return This collection after assignment.
         */
        auto operator=(Files const& other) -> Files& {
            if (this != &other) {
                m_files = other.m_files;
            }
            return *this;
        }

        /**
         * @brief Transfer another collection's descriptors.
         * @param other Collection to move from; self-move leaves it unchanged, as in cpr.
         * @return This collection after assignment.
         */
        auto operator=(Files&& other) noexcept -> Files& {
            if (this != &other) {
                m_files = std::move(other.m_files);
            }
            return *this;
        }

        using iterator       = std::vector<File>::iterator;       ///< Mutable descriptor iterator.
        using const_iterator = std::vector<File>::const_iterator; ///< Read-only descriptor iterator.

        /**
         * @brief Begin mutable iteration.
         * @return An iterator to the first descriptor.
         */
        auto begin() noexcept -> iterator { return m_files.begin(); }

        /**
         * @brief End mutable iteration.
         * @return An iterator past the last descriptor.
         */
        auto end() noexcept -> iterator { return m_files.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first descriptor.
         */
        [[nodiscard]] auto begin() const noexcept -> const_iterator { return m_files.begin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last descriptor.
         */
        [[nodiscard]] auto end() const noexcept -> const_iterator { return m_files.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first descriptor.
         */
        [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return m_files.cbegin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last descriptor.
         */
        [[nodiscard]] auto cend() const noexcept -> const_iterator { return m_files.cend(); }

        /**
         * @brief Append a copy, retaining cpr's single-descriptor interface.
         * @param file Descriptor to copy.
         */
        auto emplace_back(File const& file) -> void { m_files.emplace_back(file); }

        /**
         * @brief Append a copy of a descriptor.
         * @param file Descriptor to copy without modification.
         */
        auto push_back(File const& file) -> void { m_files.push_back(file); }

        /**
         * @brief Remove the last descriptor.
         * @pre The collection must not be empty.
         */
        auto pop_back() -> void { m_files.pop_back(); }
    };

} // namespace mcr
