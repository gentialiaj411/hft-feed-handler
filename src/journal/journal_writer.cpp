#include "mf/journal/journal_writer.hpp"

#include <array>
#include <cstring>

#include "mf/core/crc32.hpp"

#if defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace mf::journal {

namespace {
template <typename T>
void append_scalar(std::byte* dst, std::size_t& off, T v) noexcept {
  std::memcpy(dst + off, &v, sizeof(T));
  off += sizeof(T);
}
}  // namespace

JournalWriter::JournalWriter(std::size_t buffer_bytes, bool fsync_on_flush)
    : buffer_bytes_(buffer_bytes), fsync_on_flush_(fsync_on_flush) {
  buf_a_.resize(buffer_bytes_);
  buf_b_.resize(buffer_bytes_);
  active_ = buf_a_.data();
}

JournalWriter::~JournalWriter() { close(); }

bool JournalWriter::open(const std::string& path) {
#if !defined(__linux__)
  (void)path;
  return false;
#else
  close();
  fd_ = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
  if (fd_ < 0) return false;
  (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
  if (!write_header_if_needed()) {
    close();
    return false;
  }
  return true;
#endif
}

void JournalWriter::append(const mf::core::BookEvent& ev, std::uint64_t ingest_ts_ns) noexcept {
#if !defined(__linux__)
  (void)ev;
  (void)ingest_ts_ns;
#else
  if (fd_ < 0) return;

  const std::uint32_t crc = mf::core::crc32_update(0, reinterpret_cast<const std::byte*>(&ev), sizeof(ev));
  constexpr std::size_t kRecBytes = sizeof(JournalRecord) + sizeof(mf::core::BookEvent);
  if (active_used_ + kRecBytes > buffer_bytes_) {
    flush_active_buffer();
  }

  std::size_t off = active_used_;
  append_scalar(active_, off, next_seq_++);
  append_scalar(active_, off, ingest_ts_ns);
  append_scalar(active_, off, kJournalPayloadLenV1);
  append_scalar(active_, off, crc);
  std::memcpy(active_ + off, &ev, sizeof(ev));
  off += sizeof(ev);
  active_used_ = off;
#endif
}

void JournalWriter::flush() {
#if defined(__linux__)
  flush_active_buffer();
  if (fd_ >= 0 && fsync_on_flush_) {
    (void)::fsync(fd_);
  }
#endif
}

void JournalWriter::close() {
#if defined(__linux__)
  flush_active_buffer();
  if (fd_ >= 0 && fsync_on_flush_) {
    (void)::fsync(fd_);
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
#endif
  active_used_ = 0;
  active_ = buf_a_.empty() ? nullptr : buf_a_.data();
}

bool JournalWriter::write_header_if_needed() {
#if !defined(__linux__)
  return false;
#else
  struct stat st {};
  if (::fstat(fd_, &st) != 0) return false;
  if (st.st_size == 0) {
    const JournalHeader hdr{kJournalMagic, kJournalVersion};
    return ::write(fd_, &hdr, sizeof(hdr)) == static_cast<ssize_t>(sizeof(hdr));
  }
  return validate_existing_header();
#endif
}

bool JournalWriter::validate_existing_header() {
#if !defined(__linux__)
  return false;
#else
  JournalHeader hdr{};
  if (::pread(fd_, &hdr, sizeof(hdr), 0) != static_cast<ssize_t>(sizeof(hdr))) return false;
  if (hdr.magic != kJournalMagic) return false;
  if (hdr.version != kJournalVersion) return false;

  struct stat st {};
  if (::fstat(fd_, &st) != 0) return false;
  if (st.st_size < static_cast<off_t>(sizeof(JournalHeader))) return false;
  const auto data_bytes = static_cast<std::uint64_t>(st.st_size - static_cast<off_t>(sizeof(JournalHeader)));
  constexpr std::uint64_t kRecBytes = sizeof(JournalRecord) + sizeof(mf::core::BookEvent);
  next_seq_ = (data_bytes / kRecBytes) + 1;
  return true;
#endif
}

void JournalWriter::flush_active_buffer() noexcept {
#if defined(__linux__)
  if (fd_ < 0 || active_used_ == 0 || active_ == nullptr) return;
  std::size_t written = 0;
  while (written < active_used_) {
    ::iovec iov{};
    iov.iov_base = active_ + written;
    iov.iov_len = active_used_ - written;
    const ssize_t n = ::writev(fd_, &iov, 1);
    if (n <= 0) break;
    written += static_cast<std::size_t>(n);
  }

  active_ = (active_ == buf_a_.data()) ? buf_b_.data() : buf_a_.data();
  active_used_ = 0;
#endif
}

}  // namespace mf::journal
