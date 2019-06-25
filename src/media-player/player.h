#pragma once
#include <stdio.h>
#include <thread>
#include <atomic>
#include <tchar.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>

#include <SDL.h>
#include <SDL_thread.h>
}

#include "Queue.h"

//#define DECODE_MAX_SIZE 2
#define MAX_AUDIO_FRAME_SIZE 192000 //采样率：1 second of 48khz 32bit audio 

class PlayerContext {
public:
	PlayerContext();
	AVFormatContext* pFormateCtx; // AV文件的上下文
	std::atomic_bool quit;		  // 退出标志

	// --------------------------- 音频相关参数 ---------------------------- //
	AVCodecParameters* audioCodecParameter; // 音频解码器的参数
	AVCodecContext*	   audioCodecCtx;	    // 音频解码器的上下文
	AVCodec*		   audioCodec;			// 音频解码器
	AVStream*		   audio_stream;		// 音频流
	int				   au_stream_index;	    // 记录音频流的位置
	double			   audio_clk;			// 当前音频时间
	int64_t			   audio_pts;			// 记录当前已经播放音频的时间
	int64_t			   audio_pts_duration;	// 记录当前已经播放音频的时间
	Uint8*			   audio_pos;			// 用来控制每次
	Uint32			   audio_len;			// 用来控制

	Queue<AVPacket*>    audio_queue;		// 音频包队列
	SwrContext*		   au_convert_ctx;		// 音频转换器
	AVSampleFormat	   out_sample_fmt;		// 重采样格式
	int				   out_buffer_size;		// 重采样后的buffer大小
	uint8_t*		   out_buffer;			// 重采样后的buffer

	SDL_AudioSpec	   wanted_spec;			// sdl系统播放音频的各项参数信息
	// ------------------------------ end --------------------------------- //

	// ------------------------ 视频相关参数 --------------------------- //
	AVCodecParameters* videoCodecParameter; // 视频解码器的参数
	AVCodecContext*	   videoCodecCtx;		// 视频解码器的上下文
	AVCodec*		   pVideoCodec;			// 视频解码器
	AVStream*		   video_stream;		// 视频流
	Queue<AVPacket*>    video_queue;		// 视频包队列

	SwsContext*		   vi_convert_ctx;		// 视频转换器
	AVFrame*		   pFrameYUV;			// 存放转换后的视频
	int				   video_stream_index;	// 记录视频流的位置
	int64_t			   video_pts;			// 记录当前已经播放了的视频时间
	double			   video_clk;			// 当前视频帧的时间戳
	// ---------------------------- end ------------------------------ //

	// ---------------------------- sdl ----------------------------- //
	SDL_Window*		   screen;	 // 视频窗口
	SDL_Renderer*	   renderer; // 渲染器
	SDL_Texture*	   texture;  // 纹理
	SDL_Rect		   sdlRect; // sdl渲染的区域
	// --------------------------- end ------------------------------ //
};

// 解码视频包（packet）的线程
void decode_video_thread(PlayerContext* playerCtx);
// 解码音频包（packet）的线程
void decode_audio_thread(PlayerContext* playerCtx);