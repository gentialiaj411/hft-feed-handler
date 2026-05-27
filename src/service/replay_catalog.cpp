#include "mf/service/replay_catalog.hpp"

#include "mf/journal/journal_format.hpp"
#include "mf/journal/nbbo_journal_reader.hpp"
#include "mf/journal/nbbo_event.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mf::service {

bool ReplayCatalog::load(const std::string& book_path, const std::string& nbbo_path) {
  book_journal_path = book_path;
  nbbo_journal_path = nbbo_path;
  book_record_count = 0;
  nbbo_record_count = 0;
  nbbo_journal_crc = 0;
  nbbo_events.clear();
  started_at = std::chrono::steady_clock::now();

#if !defined(__linux__)
  (void)book_path;
  (void)nbbo_path;
  return false;
#else
  // O(1) record count from fixed-size v1 layout (avoids a full-tape scan at startup).
  const int fd = ::open(book_path.c_str(), O_RDONLY);
  if (fd < 0) {
    return false;
  }
  struct stat st {};
  if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(mf::journal::JournalHeader))) {
    ::close(fd);
    return false;
  }
  ::close(fd);
  constexpr std::size_t k_record_bytes =
      sizeof(mf::journal::JournalRecord) + sizeof(mf::core::BookEvent);
  const std::size_t payload_bytes =
      static_cast<std::size_t>(st.st_size) - sizeof(mf::journal::JournalHeader);
  if (payload_bytes % k_record_bytes != 0) {
    return false;
  }
  book_record_count = payload_bytes / k_record_bytes;

  if (nbbo_path.empty()) {
    return true;
  }

  mf::journal::NbboJournalReader nbbo;
  if (!nbbo.open(nbbo_path)) {
    return false;
  }
  mf::journal::NbboEvent row{};
  while (nbbo.next(row)) {
    nbbo_events.push_back(row);
    ++nbbo_record_count;
  }
  nbbo_journal_crc = nbbo.payload_crc();
  return true;
#endif
}

std::int64_t ReplayCatalog::uptime_seconds() const noexcept {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::steady_clock::now() - started_at)
      .count();
}

}  // namespace mf::service
