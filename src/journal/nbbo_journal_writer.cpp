#include "mf/journal/nbbo_journal_writer.hpp"

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

NbboJournalWriter::NbboJournalWriter(std::size_t buffer_bytes, bool fsync_on_flush)
    : buffer_bytes_(buffer_bytes), fsync_on_flush_(fsync_on_flush) {
  buf_a_.resize(buffer_bytes_);
  buf_b_.resize(buffer_bytes_);
  active_ = buf_a_.data();
}

NbboJournalWriter::~NbboJournalWriter() { close(); }

bool NbboJournalWriter::open(const std::string& path) {
#if !defined(__linux__)
  (void)path;
  return false;
#else
  close();
  fd_ = ::open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if (fd_ < 0) return false;
  (void)::posix_fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
  if (!write_header_if_needed()) {
    close();
    return false;
  }
  return true;
#endif
}

void NbboJournalWriter::append(const NbboEvent& ev) noexcept {
#if !defined(__linux__)
  (void)ev;
#else
  if (fd_ < 0) return;

  const std::uint32_t crc = mf::core::crc32_update(0, reinterpret_cast<const std::byte*>(&ev), sizeof(ev));
  constexpr std::size_t kRecBytes = sizeof(NbboJournalRecord) + sizeof(NbboEvent);
  if (active_used_ + kRecBytes > buffer_bytes_) {
    flush_active_buffer();
  }

  std::size_t off = active_used_;
  append_scalar(active_, off, next_seq_++);
  append_scalar(active_, off, ev.ingest_ts_ns);
  append_scalar(active_, off, kNbboPayloadLenV1);
  append_scalar(active_, off, crc);
  std::memcpy(active_ + off, &ev, sizeof(ev));
  off += sizeof(ev);
  active_used_ = off;
#endif
}

void NbboJournalWriter::flush() {
#if defined(__linux__)
  flush_active_buffer();
  if (fd_ >= 0 && fsync_on_flush_) {
    (void)::fsync(fd_);
  }
#endif
}

void NbboJournalWriter::close() {
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

bool NbboJournalWriter::write_header_if_needed() {
#if !defined(__linux__)
  return false;
#else
  const NbboJournalHeader hdr{kNbboJournalMagic, kNbboJournalVersion};
  return ::write(fd_, &hdr, sizeof(hdr)) == static_cast<ssize_t>(sizeof(hdr));
#endif
}

bool NbboJournalWriter::validate_existing_header() {
  (void)this;
  return false;
}

void NbboJournalWriter::flush_active_buffer() noexcept {
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
