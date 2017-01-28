#pragma once

#include "ExporterCommon.h"
#include "FrameFilter.h"

typedef struct AVContext
{
	AVCodec *codec;
	AVCodecContext *codecContext;
	AVStream *stream;
	AVDictionary *options = NULL;
	AVFrame *frame;
	AVFrame *frame_dst;
	int64_t sampleCount = 0;
	int64_t next_pts = 0;
	FrameFilter *frameFilter;
	AVPacket *packet;
	uint8_t* videoBuffer;
	int videoBuffer_size;
} AVContext;

class Encoder
{
public:
	Encoder(const char *filename);
	~Encoder();
	void Encoder::setVideoCodec(AVCodecID codecId, csSDK_int32 width, csSDK_int32 height, AVPixelFormat pixelFormat, AVRational pixelAspectRation, AVRational timebase, AVFieldOrder fieldOrder);
	void Encoder::setAudioCodec(AVCodecID codecId, csSDK_int64 channellayout, csSDK_int64 bitrate, AVRational timebase);
	void Encoder::open(const char *videoOptions, const char *audioOptions);
	void Encoder::close();
	int Encoder::writeVideoFrame(char *data);
	int Encoder::writeAudioFrame(const uint8_t **data, int32_t sampleCount);
	void Encoder::getVideoOptions(AVDictionary *dict);
private:
	void Encoder::openStream(AVContext *context, const char* options);
	AVFormatContext *formatContext;
	AVContext *videoContext;
	AVContext *audioContext;
	SwsContext *swsContext;
	bool hasVideo = false, hasAudio = false;
	const char *filename;
};

