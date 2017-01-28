#include "Encoder.h"
#include "ExporterCommon.h"

static void avlog_cb(void *, int level, const char * szFmt, va_list varg)
{
	char logbuf[2000];
	vsnprintf(logbuf, sizeof(logbuf), szFmt, varg);
	logbuf[sizeof(logbuf) - 1] = '\0';

	OutputDebugStringA(logbuf);
}

Encoder::Encoder(const char *filename)
{
	this->filename = filename;
	
	/* Create the container format */
	AVOutputFormat *format = av_guess_format(NULL, filename, NULL);
	avformat_alloc_output_context2(&formatContext, format, NULL, NULL);

	av_log_set_level(AV_LOG_DEBUG);
	av_log_set_callback(avlog_cb);
}

Encoder::~Encoder()
{
	/* Free the muxer */
	avformat_free_context(formatContext);
}

void Encoder::setVideoCodec(AVCodecID codecId, csSDK_int32 width, csSDK_int32 height, AVPixelFormat pixelFormat, AVRational pixelAspectRation, AVRational timebase, AVFieldOrder fieldOrder)
{
	/* Add streams */
	if (codecId != AV_CODEC_ID_NONE)
	{
		hasVideo = true;
		videoContext = new AVContext;

		/* Find codec */
		videoContext->codec = avcodec_find_encoder(codecId);
		if (videoContext->codec == NULL)
		{
			return;
		}

		/* Create the stream */
		videoContext->stream = avformat_new_stream(formatContext, NULL);
		videoContext->stream->id = formatContext->nb_streams - 1;
		videoContext->stream->time_base = timebase;

		/* Allocate the codec context */
		videoContext->codecContext = avcodec_alloc_context3(videoContext->codec);
		videoContext->codecContext->codec_id = codecId;
		videoContext->codecContext->width = width;
		videoContext->codecContext->height = height;
		videoContext->codecContext->bit_rate = 400000; // dummy
		videoContext->codecContext->time_base = timebase;
		videoContext->codecContext->pix_fmt = pixelFormat;

		if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			videoContext->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		/* Set up RGB -> YUV converter */
		FrameFilterOptions options;
		options.media_type = AVMEDIA_TYPE_VIDEO;
		options.width = width;
		options.height = height;
		options.pix_fmt = PLUGIN_VIDEO_PIX_FORMAT;
		options.time_base = timebase;
		options.sar = { 1, 1 };
		videoContext->frameFilter = new FrameFilter();
		videoContext->frameFilter->configure(options, "format=pix_fmts=yuv420p");

		/* Set colorspace and -range */
		videoContext->codecContext->colorspace = AVColorSpace::AVCOL_SPC_BT709;
		videoContext->codecContext->color_range = AVColorRange::AVCOL_RANGE_MPEG;
		videoContext->codecContext->color_primaries = AVColorPrimaries::AVCOL_PRI_BT709;
		videoContext->codecContext->color_trc = AVColorTransferCharacteristic::AVCOL_TRC_BT709;
	}
}

void Encoder::setAudioCodec(AVCodecID codecId, csSDK_int64 channelLayout, csSDK_int64 bitrate, AVRational timebase)
{
	if (codecId != AV_CODEC_ID_NONE)
	{
		hasAudio = true;
		audioContext = new AVContext;

		/* Find codec */
		audioContext->codec = avcodec_find_encoder(codecId);
		if (audioContext->codec == NULL)
		{
			return;
		}

		/* Create the stream */
		audioContext->stream = avformat_new_stream(formatContext, NULL);
		audioContext->stream->id = formatContext->nb_streams - 1;
		audioContext->stream->time_base = timebase;

		/* Configure the audio encoder */
		audioContext->codecContext = avcodec_alloc_context3(audioContext->codec);
		audioContext->codecContext->bit_rate = bitrate;
		audioContext->codecContext->channel_layout = channelLayout;
		audioContext->codecContext->channels = av_get_channel_layout_nb_channels(channelLayout);
		audioContext->codecContext->sample_fmt = audioContext->codec->sample_fmts ? audioContext->codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
		audioContext->codecContext->sample_rate = timebase.den;
		audioContext->codecContext->time_base = timebase;
		
		if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
		{
			audioContext->codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}

		/* Set up audio filters */
		FrameFilterOptions options;
		options.media_type = AVMEDIA_TYPE_AUDIO;
		options.channel_layout = channelLayout;
		options.sample_fmt = PLUGIN_AUDIO_SAMPLE_FORMAT;
		options.time_base = { 1, PLUGIN_AUDIO_SAMPLE_RATE };
		audioContext->frameFilter = new FrameFilter();
		audioContext->frameFilter->configure(options, "aformat=channel_layouts=stereo:sample_fmts=fltp:sample_rates=96000");
	}
}

void Encoder::open(const char *videoOptions, const char *audioOptions)
{
	if (hasVideo)
	{
		openStream(videoContext, videoOptions);
	}
	if (hasAudio)
	{
		openStream(audioContext, audioOptions);
	}

	/* Open the target file */
	avio_open(&formatContext->pb, filename, AVIO_FLAG_WRITE);
	avformat_write_header(formatContext, NULL);
}

void Encoder::close()
{
	if (hasVideo)
	{
		videoContext->frameFilter->~FrameFilter();
		avcodec_close(videoContext->codecContext);
		av_packet_free(&videoContext->packet);
		av_frame_free(&videoContext->frame);
		av_frame_free(&videoContext->frame_dst);
		//avcodec_free_context(&videoContext->codecContext);
	}

	if (hasAudio)
	{
		audioContext->frameFilter->~FrameFilter();
		av_packet_free(&audioContext->packet);
		av_frame_free(&audioContext->frame);
		avcodec_free_context(&audioContext->codecContext);
	}

	/* Write trailer */
	av_write_trailer(formatContext);

	/* Close the file */
	avio_closep(&formatContext->pb);

	sws_freeContext(swsContext);
}

void Encoder::openStream(AVContext *context, const char *options)
{
	int ret;

	AVDictionary *dict = NULL;
	if (options != NULL)
	{
		av_dict_set(&dict, "x264opts", options, 0);
	}

	/* Open the codec */
	ret = avcodec_open2(context->codecContext, context->codec, &dict);
	if (ret < 0)
	{
		return;
	}

	/* Create a reusable frame */
	context->frame = av_frame_alloc();
	context->frame_dst = av_frame_alloc();

	/* Create a reusable packet */
	context->packet = av_packet_alloc();

	switch (context->codec->type)
	{
	case AVMEDIA_TYPE_VIDEO:
		context->frame->width = context->frame_dst->width = context->codecContext->width;
		context->frame->height = context->frame_dst->height = context->codecContext->height;
		context->frame->format = PLUGIN_VIDEO_PIX_FORMAT;
		context->frame_dst->format = context->codecContext->pix_fmt;
		ret = av_frame_get_buffer(context->frame, 32);
		ret = av_frame_get_buffer(context->frame_dst, 32);
		break;
	}

	/* Copy the stream parameters to the context */
	ret = avcodec_parameters_from_context(context->stream->codecpar, context->codecContext);
}

void Encoder::getVideoOptions(AVDictionary *dict)
{
	dict = videoContext->options;
}

int Encoder::writeVideoFrame(char *data)
{
	/* Just return if no video encoding is configured */
	if (!hasVideo)
	{
		return 0;
	}

	int ret, ret2;

	if (data == NULL)
	{
		/* Flush the encoder */
		ret = avcodec_send_frame(videoContext->codecContext, NULL);
	}
	else
	{
		/* Fill the source frame */
		int ls = videoContext->frame->width * 4;
		for (int y = 0; y < videoContext->frame->height; y++)
		{
			for (int x = 0; x < ls; x += 4)
			{
				int sp = y * ls + x;
				int dp = (videoContext->frame->height - y - 1) * ls + x;
				videoContext->frame->data[0][sp] = data[dp - 4];
				videoContext->frame->data[0][sp + 1] = data[dp - 3];
				videoContext->frame->data[0][sp + 2] = data[dp - 2];
				videoContext->frame->data[0][sp + 3] = data[dp - 1];
			}
		}

		/* Presentation timestamp */
		videoContext->frame->pts = videoContext->next_pts++;

		/* Filter the frame */
		ret = videoContext->frameFilter->sendFrame(videoContext->frame);
		while ((ret = videoContext->frameFilter->receiveFrame(videoContext->frame_dst)) >= 0)
		{
			if ((ret2 = avcodec_send_frame(videoContext->codecContext, videoContext->frame_dst)) < 0)
			{
				fprintf(stderr, "error sending a frame for encoding\n");
				exit(1);
			}
		}
	}

	//av_frame_unref(videoContext->frame);
	//av_frame_unref(videoContext->frame_dst);

	while ((ret = avcodec_receive_packet(videoContext->codecContext, videoContext->packet)) >= 0)
	{
		av_packet_rescale_ts(videoContext->packet, videoContext->codecContext->time_base, videoContext->stream->time_base);
		videoContext->packet->stream_index = videoContext->stream->index;
		av_interleaved_write_frame(formatContext, videoContext->packet);
		av_packet_unref(videoContext->packet);
	}

	return 0;
}

int Encoder::writeAudioFrame(const uint8_t **data, int32_t sampleCount)
{
	/* Just return if no audio encoding is configured */
	if (!hasAudio)
	{
		return 0;
	}

	int ret = 0, err =0;
	if (data == NULL)
	{
		/* Flush the encoder */
		ret = avcodec_send_frame(audioContext->codecContext, NULL);
	}
	else
	{
		/* Create source frame */
		audioContext->frame->format = audioContext->codecContext->sample_fmt;
		audioContext->frame->channel_layout = audioContext->codecContext->channel_layout;
		audioContext->frame->channels = audioContext->codecContext->channels;
		audioContext->frame->sample_rate = audioContext->codecContext->sample_rate;
		audioContext->frame->nb_samples = sampleCount;
		audioContext->frame->pts = audioContext->sampleCount;
		audioContext->sampleCount += sampleCount;

		/* Fill buffer */
		ret = av_frame_get_buffer(audioContext->frame, 0);
		for (int i = 0; i < audioContext->frame->channels; i++)
		{
			memcpy(audioContext->frame->data[i], data[i], sampleCount * 4);
		}

		/* Resample the frame */
		err = audioContext->frameFilter->sendFrame(audioContext->frame);
		while ((err = audioContext->frameFilter->receiveFrame(audioContext->frame)) >= 0)
		{
			ret = avcodec_send_frame(audioContext->codecContext, audioContext->frame);
			if (ret < 0)
			{
				fprintf(stderr, "error sending a frame for encoding\n");
				exit(1);
			}
			av_frame_unref(audioContext->frame);
		}
	}

	while (ret >= 0)
	{
		ret = avcodec_receive_packet(audioContext->codecContext, audioContext->packet);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
		{
			return 1;
		}
		else if (ret < 0)
		{
			return -1;
		}

		av_packet_rescale_ts(audioContext->packet, audioContext->codecContext->time_base, audioContext->stream->time_base);
		audioContext->packet->stream_index = audioContext->stream->index;
		av_interleaved_write_frame(formatContext, audioContext->packet);
		av_packet_unref(audioContext->packet);
	}

	return 0;
}