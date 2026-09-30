// The small part of Chromium's //base, //ui/gfx and //media/base that its
// H.264 parser and decoder (media/video/h264_parser.cc,
// media/gpu/h264_decoder.cc) use, so that those files build as they are, with
// no Chromium tree, inside a VLC plugin. Logging is discarded; CHECK aborts.
#ifndef RTV_SHIM_H_
#define RTV_SHIM_H_

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#define MEDIA_EXPORT
#define MEDIA_GPU_EXPORT
#define MEDIA_SHMEM_EXPORT
#define BUILDFLAG(x) (0)

namespace rtv_shim {
struct NullStream {
  template <typename T>
  NullStream& operator<<(const T&) { return *this; }
  NullStream& operator<<(std::ostream& (*)(std::ostream&)) { return *this; }
};
struct AbortStream {
  ~AbortStream() { abort(); }
  template <typename T>
  AbortStream& operator<<(const T&) { return *this; }
};
}  // namespace rtv_shim

#define RTV_NULL_LOG \
  if (true) {        \
  } else             \
    rtv_shim::NullStream()
#define LOG(severity) RTV_NULL_LOG
#define DLOG(severity) RTV_NULL_LOG
#define VLOG(level) RTV_NULL_LOG
#define DVLOG(level) RTV_NULL_LOG
#define LOG_IF(severity, cond) RTV_NULL_LOG
#define DLOG_IF(severity, cond) RTV_NULL_LOG
#define DVLOG_IF(level, cond) RTV_NULL_LOG
#define DCHECK(cond) RTV_NULL_LOG
#define DCHECK_EQ(a, b) RTV_NULL_LOG
#define DCHECK_NE(a, b) RTV_NULL_LOG
#define DCHECK_LE(a, b) RTV_NULL_LOG
#define DCHECK_LT(a, b) RTV_NULL_LOG
#define DCHECK_GE(a, b) RTV_NULL_LOG
#define DCHECK_GT(a, b) RTV_NULL_LOG
#define CHECK(cond) \
  if (cond) {       \
  } else            \
    rtv_shim::AbortStream()
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define NOTREACHED() RTV_NULL_LOG

namespace absl {
using std::make_optional;
using std::nullopt;
using std::optional;
}  // namespace absl

namespace base {

// Reference counting, as Chromium spells it.
template <class T>
class RefCountedThreadSafe {
 public:
  void AddRef() const { refs_.fetch_add(1, std::memory_order_relaxed); }
  void Release() const {
    if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1)
      delete static_cast<const T*>(this);
  }
  bool HasOneRef() const { return refs_.load() == 1; }

 protected:
  RefCountedThreadSafe() = default;
  ~RefCountedThreadSafe() = default;

 private:
  mutable std::atomic<int> refs_{0};
};
template <class T>
using RefCounted = RefCountedThreadSafe<T>;

template <class T>
class scoped_refptr_impl;

}  // namespace base

template <class T>
class scoped_refptr {
 public:
  scoped_refptr() = default;
  scoped_refptr(std::nullptr_t) {}
  scoped_refptr(T* p) : ptr_(p) {
    if (ptr_)
      ptr_->AddRef();
  }
  scoped_refptr(const scoped_refptr& o) : scoped_refptr(o.ptr_) {}
  template <class U>
  scoped_refptr(const scoped_refptr<U>& o) : scoped_refptr(o.get()) {}
  scoped_refptr(scoped_refptr&& o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
  template <class U>
  scoped_refptr(scoped_refptr<U>&& o) noexcept : ptr_(o.release_raw()) {}
  ~scoped_refptr() {
    if (ptr_)
      ptr_->Release();
  }
  scoped_refptr& operator=(scoped_refptr o) noexcept {
    std::swap(ptr_, o.ptr_);
    return *this;
  }
  T* get() const { return ptr_; }
  T& operator*() const { return *ptr_; }
  T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }
  void reset() { scoped_refptr().swap(*this); }
  void swap(scoped_refptr& o) noexcept { std::swap(ptr_, o.ptr_); }
  T* release_raw() {
    T* p = ptr_;
    ptr_ = nullptr;
    return p;
  }
  template <class U>
  bool operator==(const scoped_refptr<U>& o) const { return ptr_ == o.get(); }
  template <class U>
  bool operator!=(const scoped_refptr<U>& o) const { return ptr_ != o.get(); }
  bool operator==(std::nullptr_t) const { return ptr_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return ptr_ != nullptr; }

 private:
  T* ptr_ = nullptr;
};

namespace base {

template <class T, class... Args>
scoped_refptr<T> MakeRefCounted(Args&&... args) {
  return scoped_refptr<T>(new T(std::forward<Args>(args)...));
}

template <class T>
class span {
 public:
  constexpr span() = default;
  constexpr span(T* data, size_t size) : data_(data), size_(size) {}
  template <class U>
  constexpr span(const span<U>& o) : data_(o.data()), size_(o.size()) {}
  constexpr T* data() const { return data_; }
  constexpr size_t size() const { return size_; }
  constexpr bool empty() const { return size_ == 0; }
  constexpr T* begin() const { return data_; }
  constexpr T* end() const { return data_ + size_; }
  constexpr T& operator[](size_t i) const { return data_[i]; }

 private:
  T* data_ = nullptr;
  size_t size_ = 0;
};

template <class Dst, class Src>
constexpr Dst checked_cast(Src v) { return static_cast<Dst>(v); }
template <class Dst, class Src>
constexpr Dst strict_cast(Src v) { return static_cast<Dst>(v); }
template <class Dst, class Src>
constexpr bool IsValueInRangeForNumericType(Src v) {
  return v >= std::numeric_limits<Dst>::lowest() &&
         v <= std::numeric_limits<Dst>::max();
}

template <class T>
class CheckedNumeric {
 public:
  CheckedNumeric(T v = 0) : value_(v) {}
  CheckedNumeric& operator+=(T v) {
    if (__builtin_add_overflow(value_, v, &value_))
      valid_ = false;
    return *this;
  }
  bool IsValid() const { return valid_; }
  T ValueOrDefault(T d) const { return valid_ ? value_ : d; }

 private:
  T value_;
  bool valid_ = true;
};

namespace ranges {
template <class A, class B>
bool equal(const A& a, const B& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}
}  // namespace ranges

template <class T>
void UmaHistogramEnumeration(const char*, T) {}

class TimeDelta {};

}  // namespace base

namespace gfx {

class Size {
 public:
  constexpr Size() = default;
  constexpr Size(int w, int h) : w_(w), h_(h) {}
  constexpr int width() const { return w_; }
  constexpr int height() const { return h_; }
  constexpr bool IsEmpty() const { return w_ <= 0 || h_ <= 0; }
  int64_t Area64() const { return int64_t(w_) * h_; }
  std::string ToString() const {
    return std::to_string(w_) + "x" + std::to_string(h_);
  }
  bool operator==(const Size& o) const { return w_ == o.w_ && h_ == o.h_; }
  bool operator!=(const Size& o) const { return !(*this == o); }

 private:
  int w_ = 0, h_ = 0;
};

class Rect {
 public:
  constexpr Rect() = default;
  constexpr Rect(int x, int y, int w, int h) : x_(x), y_(y), w_(w), h_(h) {}
  explicit constexpr Rect(const Size& s) : w_(s.width()), h_(s.height()) {}
  constexpr int x() const { return x_; }
  constexpr int y() const { return y_; }
  constexpr int width() const { return w_; }
  constexpr int height() const { return h_; }
  constexpr bool IsEmpty() const { return w_ <= 0 || h_ <= 0; }
  Size size() const { return Size(w_, h_); }
  std::string ToString() const {
    return std::to_string(x_) + "," + std::to_string(y_) + " " +
           std::to_string(w_) + "x" + std::to_string(h_);
  }
  bool operator==(const Rect& o) const {
    return x_ == o.x_ && y_ == o.y_ && w_ == o.w_ && h_ == o.h_;
  }
  bool operator!=(const Rect& o) const { return !(*this == o); }

 private:
  int x_ = 0, y_ = 0, w_ = 0, h_ = 0;
};

class ColorSpace {
 public:
  enum class RangeID { INVALID, LIMITED, FULL, DERIVED };
};

struct ColorVolumeMetadata {
  struct Primaries {
    float fRX, fRY, fGX, fGY, fBX, fBY, fWX, fWY;
  } primaries = {};
  float luminance_max = 0;
  float luminance_min = 0;
};

struct HDRMetadata {
  ColorVolumeMetadata color_volume_metadata;
  unsigned max_content_light_level = 0;
  unsigned max_frame_average_light_level = 0;
};

}  // namespace gfx

namespace media {

// Only the fields the decoder compares; the plugin takes colour from VLC.
class VideoColorSpace {
 public:
  VideoColorSpace() = default;
  VideoColorSpace(int primaries, int transfer, int matrix,
                  gfx::ColorSpace::RangeID range)
      : primaries_(primaries), transfer_(transfer), matrix_(matrix),
        range_(range), specified_(true) {}
  bool IsSpecified() const { return specified_; }
  bool operator==(const VideoColorSpace& o) const {
    return primaries_ == o.primaries_ && transfer_ == o.transfer_ &&
           matrix_ == o.matrix_ && range_ == o.range_;
  }
  bool operator!=(const VideoColorSpace& o) const { return !(*this == o); }
  bool full_range() const { return range_ == gfx::ColorSpace::RangeID::FULL; }
  int matrix() const { return matrix_; }

 private:
  int primaries_ = 2, transfer_ = 2, matrix_ = 2;
  gfx::ColorSpace::RangeID range_ = gfx::ColorSpace::RangeID::INVALID;
  bool specified_ = false;
};

struct SubsampleEntry;

// Never encrypted here: the decoder only asks whether there is one.
class DecryptConfig {
 public:
  const std::vector<SubsampleEntry>& subsamples() const;
  std::unique_ptr<DecryptConfig> Clone() const { return nullptr; }
};

class DecoderBuffer {
 public:
  DecoderBuffer(const uint8_t* data, size_t size) : data_(data), size_(size) {}
  const uint8_t* data() const { return data_; }
  size_t data_size() const { return size_; }
  const DecryptConfig* decrypt_config() const { return nullptr; }

 private:
  const uint8_t* data_;
  size_t size_;
};

// Encoder-side metadata H264Picture can carry; never set when decoding.
struct H264Metadata {};

namespace limits {
constexpr int kMaxVideoFrames = 4;
}

}  // namespace media

#endif  // RTV_SHIM_H_
