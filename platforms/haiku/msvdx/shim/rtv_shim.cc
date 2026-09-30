// Definitions the shimmed Chromium files link against. See rtv_shim.h.
#include "rtv_shim.h"

#include "media/base/subsample_entry.h"
#include "media/base/video_codecs.h"

namespace media {

std::string GetProfileName(VideoCodecProfile profile) {
  return "profile " + std::to_string(static_cast<int>(profile));
}

const std::vector<SubsampleEntry>& DecryptConfig::subsamples() const {
  static const std::vector<SubsampleEntry> none;
  return none;
}

}  // namespace media
