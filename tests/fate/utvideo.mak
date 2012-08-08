FATE_UTVIDEO += fate-utvideo_rgba_left
fate-utvideo_rgba_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_left.avi

FATE_UTVIDEO += fate-utvideo_rgba_median
fate-utvideo_rgba_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgba_median.avi

FATE_UTVIDEO += fate-utvideo_rgb_left
fate-utvideo_rgb_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_left.avi

FATE_UTVIDEO += fate-utvideo_rgb_median
fate-utvideo_rgb_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_rgb_median.avi

FATE_UTVIDEO += fate-utvideo_yuv420_left
fate-utvideo_yuv420_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_left.avi

FATE_UTVIDEO += fate-utvideo_yuv420_median
fate-utvideo_yuv420_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv420_median.avi

FATE_UTVIDEO += fate-utvideo_yuv422_left
fate-utvideo_yuv422_left: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_left.avi

FATE_UTVIDEO += fate-utvideo_yuv422_median
fate-utvideo_yuv422_median: CMD = framecrc -i $(SAMPLES)/utvideo/utvideo_yuv422_median.avi

FATE_UTVIDEO += fate-utvideoenc_rgba_none
fate-utvideoenc_rgba_none: tests/vsynth1/00.pgm
fate-utvideoenc_rgba_none: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgba -pred 3 -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_rgba_left
fate-utvideoenc_rgba_left: tests/vsynth1/00.pgm
fate-utvideoenc_rgba_left: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgba -pred left -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_rgba_median
fate-utvideoenc_rgba_median: tests/vsynth1/00.pgm
fate-utvideoenc_rgba_median: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgba -pred median -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_rgb_none
fate-utvideoenc_rgb_none: tests/vsynth1/00.pgm
fate-utvideoenc_rgb_none: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgb24 -pred 3 -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_rgb_left
fate-utvideoenc_rgb_left: tests/vsynth1/00.pgm
fate-utvideoenc_rgb_left: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgb24 -pred left -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_rgb_median
fate-utvideoenc_rgb_median: tests/vsynth1/00.pgm
fate-utvideoenc_rgb_median: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt rgb24 -pred median -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv420_none
fate-utvideoenc_yuv420_none: tests/vsynth1/00.pgm
fate-utvideoenc_yuv420_none: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv420p -pred 3 -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv420_left
fate-utvideoenc_yuv420_left: tests/vsynth1/00.pgm
fate-utvideoenc_yuv420_left: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv420p -pred left -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv420_median
fate-utvideoenc_yuv420_median: tests/vsynth1/00.pgm
fate-utvideoenc_yuv420_median: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv420p -pred median -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv422_none
fate-utvideoenc_yuv422_none: tests/vsynth1/00.pgm
fate-utvideoenc_yuv422_none: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv422p -pred 3 -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv422_left
fate-utvideoenc_yuv422_left: tests/vsynth1/00.pgm
fate-utvideoenc_yuv422_left: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv422p -pred left -vcodec utvideo -f avi

FATE_UTVIDEO += fate-utvideoenc_yuv422_median
fate-utvideoenc_yuv422_median: tests/vsynth1/00.pgm
fate-utvideoenc_yuv422_median: CMD = md5 -f image2 -vcodec pgmyuv -i $(TARGET_PATH)/tests/vsynth1/%02d.pgm -flags +bitexact -pix_fmt yuv422p -pred median -vcodec utvideo -f avi

FATE_SAMPLES_AVCONV += $(FATE_UTVIDEO)
fate-utvideo: $(FATE_UTVIDEO)
