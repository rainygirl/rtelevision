// See rtv_msvdx.h. The accelerator is R Chromium's
// (chromium114_port/files/media/gpu/haiku/haiku_msvdx_video_decoder.cc in
// haiku-rchromium-x86), with Chromium's VideoDecoder plumbing taken out.
#include "rtv_msvdx.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "hw/msvdx_decode.h"
#include "media/gpu/h264_decoder.h"
#include "media/gpu/h264_dpb.h"

using namespace media;

namespace {

const char* FirmwarePath() {
  static const char* const kCandidates[] = {
      "/boot/home/config/non-packaged/data/firmware/msvdx_fw.bin",
      "/boot/system/non-packaged/data/firmware/msvdx_fw.bin",
      "/boot/home/msvdx/msvdx_fw.bin",
  };
  const char* env = getenv("RTV_MSVDX_FIRMWARE");
  if (env && access(env, R_OK) == 0)
    return env;
  for (const char* path : kCandidates) {
    if (access(path, R_OK) == 0)
      return path;
  }
  return nullptr;
}

// H.264 scan order to raster, as VA-API wants the scaling lists.
constexpr uint8_t kZigzagScan4x4[16] = {0, 1,  4,  8,  5, 2,  3,  6,
                                        9, 12, 13, 10, 7, 11, 14, 15};
constexpr uint8_t kZigzagScan8x8[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// Which surfaces are free. A picture holds a surface number and gives it
// back when the DPB is done with it.
class SurfacePool : public base::RefCountedThreadSafe<SurfacePool> {
 public:
  void Reset(int count, const gfx::Size& size) {
    free_.clear();
    for (int i = count - 1; i >= 0; i--)
      free_.push_back(i);
    size_ = size;
  }
  int Take() {
    if (free_.empty())
      return -1;
    int id = free_.back();
    free_.pop_back();
    return id;
  }
  void Give(int id) { free_.push_back(id); }
  const gfx::Size& size() const { return size_; }

 private:
  friend class base::RefCountedThreadSafe<SurfacePool>;
  ~SurfacePool() = default;
  std::vector<int> free_;
  gfx::Size size_;
};

class MsvdxH264Picture : public H264Picture {
 public:
  MsvdxH264Picture(scoped_refptr<SurfacePool> pool, int surface)
      : pool_(std::move(pool)), surface_(surface) {}
  int surface() const { return surface_; }

 private:
  ~MsvdxH264Picture() override { pool_->Give(surface_); }
  scoped_refptr<SurfacePool> pool_;
  const int surface_;
};

int SurfaceOf(const H264Picture* pic) {
  return static_cast<const MsvdxH264Picture*>(pic)->surface();
}

void InitVAPicture(VAPictureH264* va_pic) {
  memset(va_pic, 0, sizeof(*va_pic));
  va_pic->picture_id = 0xffffffff;
  va_pic->flags = VA_PICTURE_H264_INVALID;
}

void FillVAPicture(VAPictureH264* va_pic, const H264Picture* pic) {
  va_pic->picture_id = SurfaceOf(pic);
  va_pic->frame_idx =
      pic->long_term ? pic->long_term_frame_idx : pic->frame_num;
  va_pic->flags = 0;
  if (pic->field == H264Picture::FIELD_BOTTOM)
    va_pic->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
  if (pic->ref) {
    va_pic->flags |= pic->long_term ? VA_PICTURE_H264_LONG_TERM_REFERENCE
                                    : VA_PICTURE_H264_SHORT_TERM_REFERENCE;
  }
  va_pic->TopFieldOrderCnt = pic->top_field_order_cnt;
  va_pic->BottomFieldOrderCnt = pic->bottom_field_order_cnt;
}

}  // namespace

struct rtv_msvdx {
  rtv_msvdx_output_fn output = nullptr;
  void* opaque = nullptr;
  std::unique_ptr<H264Decoder> decoder;
  scoped_refptr<SurfacePool> pool;
  int32_t next_id = 0;
  std::map<int32_t, int64_t> pts;

  bool Configure();
  void Output(const H264Picture* pic);
  int Run();
};

namespace {

class MsvdxH264Accelerator : public H264Decoder::H264Accelerator {
 public:
  explicit MsvdxH264Accelerator(rtv_msvdx* owner) : owner_(owner) {}

  scoped_refptr<H264Picture> CreateH264Picture() override {
    const int surface = owner_->pool->Take();
    if (surface < 0)
      return nullptr;
    return base::MakeRefCounted<MsvdxH264Picture>(owner_->pool, surface);
  }
  Status SubmitFrameMetadata(const H264SPS* sps,
                             const H264PPS* pps,
                             const H264DPB& dpb,
                             const H264Picture::Vector& ref_pic_listp0,
                             const H264Picture::Vector& ref_pic_listb0,
                             const H264Picture::Vector& ref_pic_listb1,
                             scoped_refptr<H264Picture> pic) override;
  Status SubmitSlice(const H264PPS* pps,
                     const H264SliceHeader* slice_hdr,
                     const H264Picture::Vector& ref_pic_list0,
                     const H264Picture::Vector& ref_pic_list1,
                     scoped_refptr<H264Picture> pic,
                     const uint8_t* data,
                     size_t size,
                     const std::vector<SubsampleEntry>& subsamples) override;
  Status SubmitDecode(scoped_refptr<H264Picture> pic) override {
    return msvdx_decode_end() == 0 ? Status::kOk : Status::kFail;
  }
  bool OutputPicture(scoped_refptr<H264Picture> pic) override {
    owner_->Output(pic.get());
    return true;
  }
  void Reset() override {}

 private:
  rtv_msvdx* const owner_;
  bool entropy_cabac_ = false;
};

H264Decoder::H264Accelerator::Status MsvdxH264Accelerator::SubmitFrameMetadata(
    const H264SPS* sps,
    const H264PPS* pps,
    const H264DPB& dpb,
    const H264Picture::Vector& ref_pic_listp0,
    const H264Picture::Vector& ref_pic_listb0,
    const H264Picture::Vector& ref_pic_listb1,
    scoped_refptr<H264Picture> pic) {
  // Field pictures and MBAFF were never verified on this hardware path, and
  // broadcast 1080i is exactly that; libavcodec takes those.
  if (!sps->frame_mbs_only_flag || sps->chroma_format_idc != 1)
    return Status::kNotSupported;
  VAPictureParameterBufferH264 pic_param;
  VAIQMatrixBufferH264 iq;
  memset(&pic_param, 0, sizeof(pic_param));
  memset(&iq, 0, sizeof(iq));

  pic_param.picture_width_in_mbs_minus1 = sps->pic_width_in_mbs_minus1;
  pic_param.picture_height_in_mbs_minus1 = sps->pic_height_in_map_units_minus1;
  pic_param.bit_depth_luma_minus8 = sps->bit_depth_luma_minus8;
  pic_param.bit_depth_chroma_minus8 = sps->bit_depth_chroma_minus8;
  pic_param.seq_fields.bits.chroma_format_idc = sps->chroma_format_idc;
  pic_param.seq_fields.bits.residual_colour_transform_flag =
      sps->separate_colour_plane_flag;
  pic_param.seq_fields.bits.gaps_in_frame_num_value_allowed_flag =
      sps->gaps_in_frame_num_value_allowed_flag;
  pic_param.seq_fields.bits.frame_mbs_only_flag = sps->frame_mbs_only_flag;
  pic_param.seq_fields.bits.mb_adaptive_frame_field_flag =
      sps->mb_adaptive_frame_field_flag;
  pic_param.seq_fields.bits.direct_8x8_inference_flag =
      sps->direct_8x8_inference_flag;
  pic_param.seq_fields.bits.MinLumaBiPredSize8x8 = sps->level_idc >= 31;
  pic_param.seq_fields.bits.log2_max_frame_num_minus4 =
      sps->log2_max_frame_num_minus4;
  pic_param.seq_fields.bits.pic_order_cnt_type = sps->pic_order_cnt_type;
  pic_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 =
      sps->log2_max_pic_order_cnt_lsb_minus4;
  pic_param.seq_fields.bits.delta_pic_order_always_zero_flag =
      sps->delta_pic_order_always_zero_flag;

  pic_param.num_slice_groups_minus1 = pps->num_slice_groups_minus1;
  pic_param.pic_init_qp_minus26 = pps->pic_init_qp_minus26;
  pic_param.pic_init_qs_minus26 = pps->pic_init_qs_minus26;
  pic_param.chroma_qp_index_offset = pps->chroma_qp_index_offset;
  pic_param.second_chroma_qp_index_offset = pps->second_chroma_qp_index_offset;
  pic_param.pic_fields.bits.entropy_coding_mode_flag =
      pps->entropy_coding_mode_flag;
  pic_param.pic_fields.bits.weighted_pred_flag = pps->weighted_pred_flag;
  pic_param.pic_fields.bits.weighted_bipred_idc = pps->weighted_bipred_idc;
  pic_param.pic_fields.bits.transform_8x8_mode_flag =
      pps->transform_8x8_mode_flag;
  pic_param.pic_fields.bits.field_pic_flag = 0;
  pic_param.pic_fields.bits.constrained_intra_pred_flag =
      pps->constrained_intra_pred_flag;
  pic_param.pic_fields.bits.pic_order_present_flag =
      pps->bottom_field_pic_order_in_frame_present_flag;
  pic_param.pic_fields.bits.deblocking_filter_control_present_flag =
      pps->deblocking_filter_control_present_flag;
  pic_param.pic_fields.bits.redundant_pic_cnt_present_flag =
      pps->redundant_pic_cnt_present_flag;
  pic_param.pic_fields.bits.reference_pic_flag = pic->ref;
  pic_param.frame_num = pic->frame_num;
  entropy_cabac_ = pps->entropy_coding_mode_flag;

  InitVAPicture(&pic_param.CurrPic);
  FillVAPicture(&pic_param.CurrPic, pic.get());
  for (int i = 0; i < 16; ++i)
    InitVAPicture(&pic_param.ReferenceFrames[i]);
  for (size_t i = 0; i < ref_pic_listp0.size() && i < 16; i++)
    FillVAPicture(&pic_param.ReferenceFrames[i], ref_pic_listp0[i].get());
  pic_param.num_ref_frames = sps->max_num_ref_frames;

  const bool from_pps = pps->pic_scaling_matrix_present_flag;
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 16; ++j) {
      iq.ScalingList4x4[i][kZigzagScan4x4[j]] =
          from_pps ? pps->scaling_list4x4[i][j] : sps->scaling_list4x4[i][j];
    }
  }
  for (int i = 0; i < 2; ++i) {
    for (int j = 0; j < 64; ++j) {
      iq.ScalingList8x8[i][kZigzagScan8x8[j]] =
          from_pps ? pps->scaling_list8x8[i][j] : sps->scaling_list8x8[i][j];
    }
  }

  return msvdx_decode_begin(SurfaceOf(pic.get()), &pic_param, &iq) == 0
             ? Status::kOk
             : Status::kFail;
}

H264Decoder::H264Accelerator::Status MsvdxH264Accelerator::SubmitSlice(
    const H264PPS* pps,
    const H264SliceHeader* slice_hdr,
    const H264Picture::Vector& ref_pic_list0,
    const H264Picture::Vector& ref_pic_list1,
    scoped_refptr<H264Picture> pic,
    const uint8_t* data,
    size_t size,
    const std::vector<SubsampleEntry>& subsamples) {
  VASliceParameterBufferH264 slice_param;
  memset(&slice_param, 0, sizeof(slice_param));

  // header_bit_size counts RBSP bits from the NAL header. The hardware wants
  // the offset in the bytes as stored, emulation-prevention bytes included,
  // and for CABAC rounded up to the byte. Add back the EPBs in the header.
  uint32_t bits = slice_hdr->header_bit_size;
  {
    const uint32_t rbsp_bytes_needed = (bits + 7) / 8;
    uint32_t rbsp = 0;
    int zeros = 0;
    for (size_t i = 0; i < size && rbsp < rbsp_bytes_needed; i++) {
      if (zeros >= 2 && data[i] == 3) {
        bits += 8;
        zeros = 0;
        continue;
      }
      zeros = data[i] == 0 ? zeros + 1 : 0;
      rbsp++;
    }
  }
  if (entropy_cabac_)
    bits = (bits + 7) & ~7u;
  slice_param.slice_data_bit_offset = bits;

  slice_param.first_mb_in_slice = slice_hdr->first_mb_in_slice;
  slice_param.slice_type = slice_hdr->slice_type % 5;
  slice_param.direct_spatial_mv_pred_flag =
      slice_hdr->direct_spatial_mv_pred_flag;
  slice_param.num_ref_idx_l0_active_minus1 =
      slice_hdr->num_ref_idx_l0_active_minus1;
  slice_param.num_ref_idx_l1_active_minus1 =
      slice_hdr->num_ref_idx_l1_active_minus1;
  slice_param.cabac_init_idc = slice_hdr->cabac_init_idc;
  slice_param.slice_qp_delta = slice_hdr->slice_qp_delta;
  slice_param.disable_deblocking_filter_idc =
      slice_hdr->disable_deblocking_filter_idc;
  slice_param.slice_alpha_c0_offset_div2 =
      slice_hdr->slice_alpha_c0_offset_div2;
  slice_param.slice_beta_offset_div2 = slice_hdr->slice_beta_offset_div2;

  if (((slice_hdr->IsPSlice() || slice_hdr->IsSPSlice()) &&
       pps->weighted_pred_flag) ||
      (slice_hdr->IsBSlice() && pps->weighted_bipred_idc == 1)) {
    slice_param.luma_log2_weight_denom = slice_hdr->luma_log2_weight_denom;
    slice_param.chroma_log2_weight_denom = slice_hdr->chroma_log2_weight_denom;
    slice_param.luma_weight_l0_flag = slice_hdr->luma_weight_l0_flag;
    slice_param.luma_weight_l1_flag = slice_hdr->luma_weight_l1_flag;
    slice_param.chroma_weight_l0_flag = slice_hdr->chroma_weight_l0_flag;
    slice_param.chroma_weight_l1_flag = slice_hdr->chroma_weight_l1_flag;
    for (int i = 0; i <= slice_param.num_ref_idx_l0_active_minus1 && i < 32;
         ++i) {
      slice_param.luma_weight_l0[i] =
          slice_hdr->pred_weight_table_l0.luma_weight[i];
      slice_param.luma_offset_l0[i] =
          slice_hdr->pred_weight_table_l0.luma_offset[i];
      for (int j = 0; j < 2; ++j) {
        slice_param.chroma_weight_l0[i][j] =
            slice_hdr->pred_weight_table_l0.chroma_weight[i][j];
        slice_param.chroma_offset_l0[i][j] =
            slice_hdr->pred_weight_table_l0.chroma_offset[i][j];
      }
    }
    if (slice_hdr->IsBSlice()) {
      for (int i = 0; i <= slice_param.num_ref_idx_l1_active_minus1 && i < 32;
           ++i) {
        slice_param.luma_weight_l1[i] =
            slice_hdr->pred_weight_table_l1.luma_weight[i];
        slice_param.luma_offset_l1[i] =
            slice_hdr->pred_weight_table_l1.luma_offset[i];
        for (int j = 0; j < 2; ++j) {
          slice_param.chroma_weight_l1[i][j] =
              slice_hdr->pred_weight_table_l1.chroma_weight[i][j];
          slice_param.chroma_offset_l1[i][j] =
              slice_hdr->pred_weight_table_l1.chroma_offset[i][j];
        }
      }
    }
  }

  for (int i = 0; i < 32; ++i) {
    InitVAPicture(&slice_param.RefPicList0[i]);
    InitVAPicture(&slice_param.RefPicList1[i]);
  }
  for (size_t i = 0; i < ref_pic_list0.size() && i < 32; ++i) {
    if (ref_pic_list0[i])
      FillVAPicture(&slice_param.RefPicList0[i], ref_pic_list0[i].get());
  }
  for (size_t i = 0; i < ref_pic_list1.size() && i < 32; ++i) {
    if (ref_pic_list1[i])
      FillVAPicture(&slice_param.RefPicList1[i], ref_pic_list1[i].get());
  }

  return msvdx_decode_slice(&slice_param, data, size) == 0 ? Status::kOk
                                                           : Status::kFail;
}

}  // namespace

bool rtv_msvdx::Configure() {
  // Pictures leave through a copy, so a surface is held only by the DPB and
  // by the picture being decoded.
  const int count =
      std::min<int>(decoder->GetRequiredNumOfPictures() + 2, 24);
  const gfx::Size size = decoder->GetPicSize();
  if (size.width() > 1920 || size.height() > 1088)
    return false;
  int profile = MSVDX_PROFILE_HIGH;
  if (decoder->GetProfile() == H264PROFILE_BASELINE)
    profile = MSVDX_PROFILE_BASELINE;
  else if (decoder->GetProfile() == H264PROFILE_MAIN)
    profile = MSVDX_PROFILE_MAIN;
  if (msvdx_decode_configure(size.width(), size.height(), count, profile) != 0)
    return false;
  pool->Reset(count, size);
  return true;
}

void rtv_msvdx::Output(const H264Picture* pic) {
  rtv_msvdx_picture out;
  if (msvdx_decode_read(SurfaceOf(pic), &out.luma, &out.chroma, &out.stride) !=
      0)
    return;
  const gfx::Size coded = pool->size();
  gfx::Rect visible = pic->visible_rect();
  if (visible.IsEmpty())
    visible = gfx::Rect(coded);
  out.coded_width = coded.width();
  out.coded_height = coded.height();
  out.visible_x = visible.x();
  out.visible_y = visible.y();
  out.visible_width = visible.width();
  out.visible_height = visible.height();
  auto it = pts.find(pic->bitstream_id());
  out.pts = it != pts.end() ? it->second : -1;
  output(opaque, &out);
}

int rtv_msvdx::Run() {
  for (;;) {
    switch (decoder->Decode()) {
      case AcceleratedVideoDecoder::kConfigChange:
        if (!Configure())
          return -1;
        continue;
      case AcceleratedVideoDecoder::kRanOutOfStreamData:
        while (pts.size() > 64)
          pts.erase(pts.begin());
        return 0;
      case AcceleratedVideoDecoder::kRanOutOfSurfaces:
      case AcceleratedVideoDecoder::kDecodeError:
      case AcceleratedVideoDecoder::kNeedContextUpdate:
      case AcceleratedVideoDecoder::kTryAgain:
        return -1;
    }
  }
}

extern "C" {

const char* rtv_msvdx_firmware(void) {
  return FirmwarePath();
}

rtv_msvdx* rtv_msvdx_create(rtv_msvdx_output_fn output, void* opaque) {
  const char* firmware = FirmwarePath();
  if (!firmware || msvdx_decode_open(firmware) != 0)
    return nullptr;
  auto* d = new rtv_msvdx;
  d->output = output;
  d->opaque = opaque;
  d->pool = base::MakeRefCounted<SurfacePool>();
  d->decoder = std::make_unique<H264Decoder>(
      std::make_unique<MsvdxH264Accelerator>(d), H264PROFILE_HIGH);
  return d;
}

void rtv_msvdx_destroy(rtv_msvdx* d) {
  if (!d)
    return;
  d->decoder.reset();
  d->pool.reset();
  msvdx_decode_close();
  delete d;
}

int rtv_msvdx_decode(rtv_msvdx* d, const uint8_t* data, size_t size,
                     int64_t pts) {
  if (!size)
    return 0;
  const int32_t id = d->next_id;
  d->next_id = (d->next_id + 1) & 0x3fffffff;
  d->pts[id] = pts;
  DecoderBuffer buffer(data, size);
  d->decoder->SetStream(id, buffer);
  return d->Run();
}

int rtv_msvdx_drain(rtv_msvdx* d) {
  return d->decoder->Flush() ? 0 : -1;
}

void rtv_msvdx_reset(rtv_msvdx* d) {
  d->decoder->Reset();
  d->pts.clear();
}

}  // extern "C"
