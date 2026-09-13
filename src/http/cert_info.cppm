/**
 * @file cert_info.cppm
 * @brief Ordered, owned strings describing one certificate.
 */
export module mcr.cert_info;

import std;

export namespace mcr {

    /**
     * @brief Store certificate information entries, following cpr's CertInfo interface.
     * @note Entries retain their order and all bytes without parsing, validation, or deduplication.
     * Standard container member names are retained. References and iterators follow vector's
     * invalidation rules. Copy and move construction are supported; assignment is unavailable as in cpr.
     */
    class CertInfo {
    private:
        std::vector<std::string> m_cert_info; ///< Owned certificate entries in insertion order.

    public:
        /**
         * @brief Construct an empty certificate information collection.
         */
        CertInfo()                          = default;

        /**
         * @brief Copy all entries into independent storage.
         * @param other Collection to copy.
         */
        CertInfo(CertInfo const& other)     = default;

        /**
         * @brief Transfer the entries from another collection.
         * @param other Collection to move from.
         */
        CertInfo(CertInfo&& other) noexcept = default;

        /**
         * @brief Copy an ordered list of certificate entries, including duplicates and empty strings.
         * @param entries Text entries to store without modification.
         */
        CertInfo(std::initializer_list<std::string> const& entries) : m_cert_info{ entries } {}

        /**
         * @brief Release the owned entries.
         */
        ~CertInfo() noexcept = default;

        using iterator       = std::vector<std::string>::iterator;       ///< Mutable entry iterator.
        using const_iterator = std::vector<std::string>::const_iterator; ///< Read-only entry iterator.

        /**
         * @brief Access an entry by its insertion position.
         * @param pos Zero-based entry index.
         * @return A mutable reference to the stored text.
         * @pre pos must be less than the entry count, as with std::vector::operator[].
         */
        auto operator[](std::size_t pos) -> std::string& {
            return m_cert_info[pos];
        }

        /**
         * @brief Begin mutable iteration.
         * @return An iterator to the first entry.
         */
        auto begin() noexcept -> iterator { return m_cert_info.begin(); }

        /**
         * @brief End mutable iteration.
         * @return An iterator past the last entry.
         */
        auto end() noexcept -> iterator { return m_cert_info.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first entry.
         */
        [[nodiscard]] auto begin() const noexcept -> const_iterator { return m_cert_info.begin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last entry.
         */
        [[nodiscard]] auto end() const noexcept -> const_iterator { return m_cert_info.end(); }

        /**
         * @brief Begin read-only iteration.
         * @return An iterator to the first entry.
         */
        [[nodiscard]] auto cbegin() const noexcept -> const_iterator { return m_cert_info.cbegin(); }

        /**
         * @brief End read-only iteration.
         * @return An iterator past the last entry.
         */
        [[nodiscard]] auto cend() const noexcept -> const_iterator { return m_cert_info.cend(); }

        /**
         * @brief Append a copy of one entry, retaining cpr's single-string emplace interface.
         * @param entry Text to copy, including any embedded nulls.
         */
        auto emplace_back(std::string const& entry) -> void { m_cert_info.emplace_back(entry); }

        /**
         * @brief Append a copy of one entry.
         * @param entry Text to copy without modification.
         */
        auto push_back(std::string const& entry) -> void { m_cert_info.push_back(entry); }

        /**
         * @brief Remove the last entry.
         * @pre The collection must not be empty.
         */
        auto pop_back() -> void { m_cert_info.pop_back(); }
    };

} // namespace mcr
