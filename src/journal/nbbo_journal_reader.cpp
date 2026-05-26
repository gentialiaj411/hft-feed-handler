#include "mf/journal/nbbo_journal_reader.hpp"

#include <cstring>

#include "mf/core/crc32.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mf::journal {

namespace {
template <typename T>
T read_scalar(const std::byte* p) noexcept {
  T out{};
  std::memcpy(&out, p, sizeof(T));
  return out;
}
}  // namespace

NbboJournalReader::~NbboJournalReader() { close(); }

bool NbboJournalReader::open(const std::string& path) {
  close();
#if !defined(__linux__)
  (void)path;
  return false;
#else
  fd_ = ::open(path.c_str(), O_RDONLY);
  if (fd_ < 0) return false;

  struct stat st {};
  if (::fstat(fd_, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(NbboJournalHeader))) {
    close();
    return false;
  }

  size_ = static_cast<std::size_t>(st.st_size);
  void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (mapped == MAP_FAILED) {
    close();
    return false;
  }
  base_ = reinterpret_cast<const std::byte*>(mapped);
  const auto* hdr = reinterpret_cast<const NbboJournalHeader*>(base_);
  if (hdr->magic != kNbboJournalMagic || hdr->version != kNbboJournalVersion) {
    close();
    return false;
  }
  off_ = sizeof(NbboJournalHeader);
  stats_ = {};
  payload_crc_ = 0;
  return true;
#endif
}

bool NbboJournalReader::next(NbboEvent& out) {
#if !defined(__linux__)
  (void)out;
  return false;
#else
  if (base_ == nullptr) return false;
  constexpr std::size_t kHeaderBytes = sizeof(NbboJournalRecord);
  constexpr std::size_t kPayloadBytes = sizeof(NbboEvent);
  constexpr std::size_t kRecBytes = kHeaderBytes + kPayloadBytes;

  while (off_ + kHeaderBytes <= size_) {
    const std::byte* rec = base_ + off_;
    const std::uint32_t len = read_scalar<std::uint32_t>(rec + 16);
    const std::uint32_t crc = read_scalar<std::uint32_t>(rec + 20);
    if (len != kNbboPayloadLenV1 || off_ + kRecBytes > size_) {
      ++stats_.crc_failures;
      off_ = size_;
      return false;
    }
    const std::byte* payload = rec + kHeaderBytes;
    const std::uint32_t got = mf::core::crc32_update(0, payload, kPayloadBytes);
    off_ += kRecBytes;
    stats_.bytes_read += kRecBytes;
    if (got != crc) {
      ++stats_.crc_failures;
      continue;
    }
    std::memcpy(&out, payload, kPayloadBytes);
    payload_crc_ = mf::core::crc32_update(payload_crc_, payload, kPayloadBytes);
    ++stats_.records_read;
    return true;
  }
  return false;
#endif
}

void NbboJournalReader::close() {
#if defined(__linux__)
  if (base_ != nullptr && size_ > 0) {
    (void)::munmap(const_cast<std::byte*>(base_), size_);
  }
  if (fd_ >= 0) {
    ::close(fd_);
  }
#endif
  base_ = nullptr;
  size_ = 0;
  off_ = 0;
  fd_ = -1;
}

}  // namespace mf::journal
