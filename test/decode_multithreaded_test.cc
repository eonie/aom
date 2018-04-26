/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include <cstdio>
#include <cstdlib>
#include <string>

#include "aom_mem/aom_mem.h"
#include "test/codec_factory.h"
#include "test/encode_test_driver.h"
#include "test/i420_video_source.h"
#include "test/md5_helper.h"
#include "test/util.h"
#include "third_party/googletest/src/googletest/include/gtest/gtest.h"

namespace {

static const int kMaxNumThreadsMinus1 = 7;

class AV1DecodeMultiThreadedTest
    : public ::libaom_test::CodecTestWith4Params<int, int, int, int>,
      public ::libaom_test::EncoderTest {
 protected:
  AV1DecodeMultiThreadedTest()
      : EncoderTest(GET_PARAM(0)), md5_single_thread_(), md5_multi_thread_(),
        n_tile_cols_(GET_PARAM(1)), n_tile_rows_(GET_PARAM(2)),
        n_tile_groups_(GET_PARAM(3)), set_cpu_used_(GET_PARAM(4)) {
    init_flags_ = AOM_CODEC_USE_PSNR;
    aom_codec_dec_cfg_t cfg = aom_codec_dec_cfg_t();
    cfg.w = 704;
    cfg.h = 576;
    cfg.threads = 1;
    cfg.allow_lowbitdepth = 1;
    single_thread_dec_ = codec_->CreateDecoder(cfg, 0);

    for (int t = 0; t < kMaxNumThreadsMinus1; ++t) {
      cfg.threads = t + 2;
      multi_thread_dec_[t] = codec_->CreateDecoder(cfg, 0);
    }

#if CONFIG_AV1
    if (single_thread_dec_->IsAV1()) {
      single_thread_dec_->Control(AV1_SET_DECODE_TILE_ROW, -1);
      single_thread_dec_->Control(AV1_SET_DECODE_TILE_COL, -1);
    }
    for (int t = 0; t < kMaxNumThreadsMinus1; ++t) {
      if (multi_thread_dec_[t]->IsAV1()) {
        multi_thread_dec_[t]->Control(AV1_SET_DECODE_TILE_ROW, -1);
        multi_thread_dec_[t]->Control(AV1_SET_DECODE_TILE_COL, -1);
      }
    }
#endif
  }

  virtual ~AV1DecodeMultiThreadedTest() {
    delete single_thread_dec_;
    for (int t = 0; t < kMaxNumThreadsMinus1; ++t) delete multi_thread_dec_[t];
  }

  virtual void SetUp() {
    InitializeConfig();
    SetMode(libaom_test::kTwoPassGood);
  }

  virtual void PreEncodeFrameHook(libaom_test::VideoSource *video,
                                  libaom_test::Encoder *encoder) {
    if (video->frame() == 1) {
      encoder->Control(AV1E_SET_TILE_COLUMNS, n_tile_cols_);
      encoder->Control(AV1E_SET_TILE_ROWS, n_tile_rows_);
      encoder->Control(AV1E_SET_NUM_TG, n_tile_groups_);
      encoder->Control(AOME_SET_CPUUSED, set_cpu_used_);
    }
  }

  void UpdateMD5(::libaom_test::Decoder *dec, const aom_codec_cx_pkt_t *pkt,
                 ::libaom_test::MD5 *md5) {
    const aom_codec_err_t res = dec->DecodeFrame(
        reinterpret_cast<uint8_t *>(pkt->data.frame.buf), pkt->data.frame.sz);
    if (res != AOM_CODEC_OK) {
      abort_ = true;
      ASSERT_EQ(AOM_CODEC_OK, res);
    }
    const aom_image_t *img = dec->GetDxData().Next();
    md5->Add(img);
  }

  virtual void FramePktHook(const aom_codec_cx_pkt_t *pkt) {
    UpdateMD5(single_thread_dec_, pkt, &md5_single_thread_);

    for (int t = 0; t < kMaxNumThreadsMinus1; ++t)
      UpdateMD5(multi_thread_dec_[t], pkt, &md5_multi_thread_[t]);
  }

  void DoTest() {
    const aom_rational timebase = { 33333333, 1000000000 };
    cfg_.g_timebase = timebase;
    cfg_.rc_target_bitrate = 500;
    cfg_.g_lag_in_frames = 12;
    cfg_.rc_end_usage = AOM_VBR;

    libaom_test::I420VideoSource video("hantro_collage_w352h288.yuv", 704, 576,
                                       timebase.den, timebase.num, 0, 5);
    ASSERT_NO_FATAL_FAILURE(RunLoop(&video));

    const char *md5_single_thread_str = md5_single_thread_.Get();

    for (int t = 0; t < kMaxNumThreadsMinus1; ++t) {
      const char *md5_multi_thread_str = md5_multi_thread_[t].Get();
      ASSERT_STREQ(md5_single_thread_str, md5_multi_thread_str);
    }
  }

  ::libaom_test::MD5 md5_single_thread_;
  ::libaom_test::MD5 md5_multi_thread_[kMaxNumThreadsMinus1];
  ::libaom_test::Decoder *single_thread_dec_;
  ::libaom_test::Decoder *multi_thread_dec_[kMaxNumThreadsMinus1];

 private:
  int n_tile_cols_;
  int n_tile_rows_;
  int n_tile_groups_;
  int set_cpu_used_;
};

// run an encode and do the decode both in single thread
// and multi thread. Ensure that the MD5 of the output in both cases
// is identical. If so, the test passes.
TEST_P(AV1DecodeMultiThreadedTest, MD5Match) {
  cfg_.large_scale_tile = 0;
  single_thread_dec_->Control(AV1_SET_TILE_MODE, 0);
  for (int t = 0; t < kMaxNumThreadsMinus1; ++t)
    multi_thread_dec_[t]->Control(AV1_SET_TILE_MODE, 0);
  DoTest();
}

class AV1DecodeMultiThreadedTestLarge : public AV1DecodeMultiThreadedTest {};

TEST_P(AV1DecodeMultiThreadedTestLarge, MD5Match) {
  cfg_.large_scale_tile = 0;
  single_thread_dec_->Control(AV1_SET_TILE_MODE, 0);
  for (int t = 0; t < kMaxNumThreadsMinus1; ++t)
    multi_thread_dec_[t]->Control(AV1_SET_TILE_MODE, 0);
  DoTest();
}

// TODO(ranjit): More tests have to be added using pre-generated MD5.
AV1_INSTANTIATE_TEST_CASE(AV1DecodeMultiThreadedTest, ::testing::Values(1, 2),
                          ::testing::Values(1, 2), ::testing::Values(1),
                          ::testing::Values(3));
AV1_INSTANTIATE_TEST_CASE(AV1DecodeMultiThreadedTestLarge,
                          ::testing::Values(0, 1, 2, 6),
                          ::testing::Values(0, 1, 2, 6),
                          ::testing::Values(1, 4), ::testing::Values(0));

class AV1DecodeMultiThreadedLSTestLarge
    : public AV1DecodeMultiThreadedTestLarge {};

TEST_P(AV1DecodeMultiThreadedLSTestLarge, MD5Match) {
  cfg_.large_scale_tile = 1;
  single_thread_dec_->Control(AV1_SET_TILE_MODE, 1);
  for (int t = 0; t < kMaxNumThreadsMinus1; ++t)
    multi_thread_dec_[t]->Control(AV1_SET_TILE_MODE, 1);
  DoTest();
}

AV1_INSTANTIATE_TEST_CASE(AV1DecodeMultiThreadedLSTestLarge,
                          ::testing::Values(1, 2, 32),
                          ::testing::Values(1, 2, 32), ::testing::Values(1),
                          ::testing::Values(0, 3));

}  // namespace