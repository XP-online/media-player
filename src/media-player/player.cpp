#include "player.h"
// decode_video_thread 解码视频packet的线程
void decode_video_thread(PlayerContext* playerCtx) {
	AVFrame* pFrame = nullptr;
	while (playerCtx && !playerCtx->quit) {
		AVPacket *pkt;
		if (!playerCtx->video_queue.pull(pkt)) { // 从视频队列中取出视频包
			SDL_Delay(1);
			continue;
		}
		// ------------------------ 根据视频包解码出一个视频帧 ------------------------ //
		if (avcodec_send_packet(playerCtx->videoCodecCtx, pkt)) {
			exit(1);
			return;
		}
		int ret = 0;
		while(ret >= 0){
			if (!pFrame) {
				if (!(pFrame = av_frame_alloc())) {
					fprintf(stderr, "Could not allocate audio frame\n");
					exit(1);
				}
			}
			// 从编码器接收一个frame
			ret = avcodec_receive_frame(playerCtx->videoCodecCtx, pFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			// --------------------------- 计算视频时间戳------------------------------ //
			double pts = 0; 
			int64_t f_pts = pFrame->pts;
			int64_t pkt_dts = pFrame->pkt_dts;
			if (pkt_dts != AV_NOPTS_VALUE) {
				pts = av_frame_get_best_effort_timestamp(pFrame);  // 尽力获取视频帧的时间戳
			}
			else {
				pts = pFrame->pts; // 如果获取时间戳失败，则将当前的pts作为视频的时间戳
			}
			pts *= av_q2d(playerCtx->video_stream->time_base);/* time_base 是一个分数 av_q2d是一个将分数转换成小数的函数。
																 在此处是用pts乘以time_base得到时间戳 */
			if (pts == 0.0) {
				pts = playerCtx->video_clk;
			}
			//计算视频流的时间基。av_q2d将一个分数转换成小数
			double frame_delay = av_q2d(playerCtx->video_stream->time_base); 
			frame_delay += pFrame->repeat_pict * (frame_delay * 0.5); /* 计算视频延迟。 根据 pFrame->repeat_pict的注释：
																	     extra_delay = repeat_pict / (2*fps) => extra_delay = repeat_pict * fps * 0.5 */
			pts += frame_delay; // 视频帧的时间戳+视频延迟等于视频的实际时间戳
			playerCtx->video_clk = pts; // 更新视频时间戳

			// ---------------------------- 音视频同步 -------------------------- //
			int64_t delay = (playerCtx->video_clk - playerCtx->audio_clk) * 1000; // 计算视频时间戳和音频的差距。因为这里的单位是微妙。所以*1000 转换成毫秒
			if (delay > 0) { // delay大于0说明视频时间戳提前了，等待一段时间再显示视频。
				SDL_Delay(delay);
			}

			// ---------------------------- SDL 显示视频 ----------------------------//
			sws_scale(playerCtx->vi_convert_ctx, pFrame->data, pFrame->linesize, 0 // 对视频数据进行缩放
				, playerCtx->videoCodecCtx->height, playerCtx->pFrameYUV->data, playerCtx->pFrameYUV->linesize);
			// 更细屏幕的纹理，参数：(texture纹理，rect区域nullptr为更新全局，plane像素数据，pitch数据大小)
			SDL_UpdateYUVTexture(playerCtx->texture, nullptr,
				playerCtx->pFrameYUV->data[0], playerCtx->pFrameYUV->linesize[0], //y
				playerCtx->pFrameYUV->data[1], playerCtx->pFrameYUV->linesize[1], //u
				playerCtx->pFrameYUV->data[2], playerCtx->pFrameYUV->linesize[2]); //v
			//重置渲染器
			SDL_RenderClear(playerCtx->renderer);
			//将纹理信息拷贝给渲染器
			SDL_RenderCopy(playerCtx->renderer, playerCtx->texture, nullptr, &playerCtx->sdlRect);
			// 显示
			SDL_RenderPresent(playerCtx->renderer);
			// --------------------------- end --------------------------------- //
			av_frame_unref(pFrame);
		}
		
		av_frame_free(&pFrame);
		av_packet_unref(pkt);
		av_packet_free(&pkt);
	}
	
}
// decode_audio_thread 解码音频packet的线程
void decode_audio_thread(PlayerContext* playerCtx) {
	AVFrame *pFrame = nullptr;
	while (playerCtx && !playerCtx->quit) {
		AVPacket *pkt;
		if (!playerCtx->audio_queue.pull(pkt)) { // 从音频队列中取出音频包
			SDL_Delay(1);
			continue;
		}
		
		// ------------------------ 根据音频包解码出一个音频帧 ------------------------ //
		if (avcodec_send_packet(playerCtx->audioCodecCtx, pkt)) {// 发送一个pkt给解码器,ffmpeg3之后的写法
			exit(1);
			return;
		}
		int ret = 0;
		while (ret >= 0) {
			if (!pFrame) {
				if (!(pFrame = av_frame_alloc())) {
					fprintf(stderr, "Could not allocate audio frame\n");
					exit(1);
				}
			}
			// 从编码器接收一个frame
			ret = avcodec_receive_frame(playerCtx->audioCodecCtx, pFrame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0)
			{
				fprintf(stderr, "Error during decoding\n");
				exit(1);
				return;
			}
		// ----------------------- 对音频帧进行重采样，并给系统缓冲区数据 -------------------------- //
			
			swr_convert(playerCtx->au_convert_ctx, &playerCtx->out_buffer, playerCtx->out_buffer_size, // 对音频的采样率进行转换
				(const uint8_t * *)pFrame->data, playerCtx->audioCodecCtx->frame_size);
			// 上一帧音频数据还没有播完
			while (playerCtx->audio_len > 0) {
				SDL_Delay(1);
			}
			// 重新设置音频缓存
			playerCtx->audio_pos = (Uint8*)playerCtx->out_buffer;
			playerCtx->audio_len = playerCtx->out_buffer_size;
		// -------------------------------- 更新音频时间戳 ----------------------------------- //
			if (pFrame->pts != AV_NOPTS_VALUE) {
				playerCtx->audio_clk = av_q2d(playerCtx->audio_stream->time_base) * pFrame->pts;
			}
			playerCtx->audio_pts_duration = pFrame->pkt_duration;
			av_frame_unref(pFrame);
		}
		// ------------------------------ end ------------------------------------- //
		av_frame_free(&pFrame);
		av_packet_unref(pkt);
		av_packet_free(&pkt);
	}
}
// sdl_audio_callback SDL 的系统回调函数
void sdl_audio_callback(void* userdata, Uint8* stream, int len) {
	PlayerContext* playerCtx = (PlayerContext*)userdata;
	if (!playerCtx || playerCtx->quit) {
		return;
	}
	// 清空stream中的内存 SDL 2.0  
	SDL_memset(stream, 0, len);
	if (playerCtx->audio_len == 0)
		return;
	// 尽可能的给系统传递音频buffer，但不会超出已经解析的大小(audio_len),也不会多给超出系统索要的大小(len)
	Uint32 streamlen = ((Uint32)len > playerCtx->audio_len ? playerCtx->audio_len : len);
	SDL_MixAudio(stream, playerCtx->audio_pos, streamlen, SDL_MIX_MAXVOLUME); // 给系统要分配的控件赋值
	playerCtx->audio_pos += streamlen; // 音频缓存的位置向前
	playerCtx->audio_len -= streamlen; // 音频缓存去掉已经分配掉的大小

	// 更新音频播放时间
	playerCtx->audio_clk += streamlen / (playerCtx->audioCodecCtx->channels * 2 * playerCtx->audioCodecCtx->sample_rate);
}

// init_audio_parameters 初始化音频参数，重采样器所需的各项参数
int init_audio_parameters(PlayerContext &playerCtx){
	// 获取音频解码器参数
	playerCtx.audioCodecParameter = playerCtx.pFormateCtx->streams[playerCtx.au_stream_index]->codecpar;
	// 获取音频解码器
	playerCtx.audioCodec = avcodec_find_decoder(playerCtx.audioCodecParameter->codec_id);
	if (nullptr == playerCtx.audioCodec) {
		printf_s("audio avcodec_find_decoder failed.\n");
		return -1;
	}
	// 获取解码器上下文
	playerCtx.audioCodecCtx = avcodec_alloc_context3(playerCtx.audioCodec);
	// 根据音频参数配置音频上下文
	if (avcodec_parameters_to_context(playerCtx.audioCodecCtx, playerCtx.audioCodecParameter) < 0) {
		printf_s("audio avcodec_parameters_to_context failed\n");
		return -1;
	}
	// 根据上下文配置音频解码器
	avcodec_open2(playerCtx.audioCodecCtx, playerCtx.audioCodec, nullptr);

	// ------------------------- 设置重采样相关参数 ------------------------- //
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO; // 双声道输出
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

	playerCtx.out_sample_fmt = AV_SAMPLE_FMT_S16; // 输出的音频格式
	int out_sample_rate = 44100; // 采样率
	int64_t in_channel_layout = av_get_default_channel_layout(playerCtx.audioCodecCtx->channels); //输入通道数
	playerCtx.audioCodecCtx->channel_layout = in_channel_layout;
	playerCtx.au_convert_ctx = swr_alloc(); // 初始化重采样结构体
	playerCtx.au_convert_ctx = swr_alloc_set_opts(playerCtx.au_convert_ctx, out_channel_layout, playerCtx.out_sample_fmt, out_sample_rate,
		in_channel_layout, playerCtx.audioCodecCtx->sample_fmt, playerCtx.audioCodecCtx->sample_rate, 0, nullptr); //配置重采样率
	swr_init(playerCtx.au_convert_ctx); // 初始化重采样率
	int out_nb_samples = playerCtx.audioCodecCtx->frame_size;

	// 计算出重采样后需要的buffer大小，后期储存转换后的音频数据时用
	playerCtx.out_buffer_size = av_samples_get_buffer_size(nullptr, playerCtx.audioCodecCtx->channels, out_nb_samples, playerCtx.out_sample_fmt, 1);
	playerCtx.out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

	// ----------------------- 设置 SDL播放音频时的参数 ------------------------ //
	playerCtx.wanted_spec.freq = out_sample_rate;//44100;
	playerCtx.wanted_spec.format = AUDIO_S16SYS;
	playerCtx.wanted_spec.channels = out_channels;//playerCtx.audioCodecCtx->channels;
	playerCtx.wanted_spec.silence = 0;
	playerCtx.wanted_spec.samples = out_nb_samples;//frame->nb_samples;
	playerCtx.wanted_spec.callback = sdl_audio_callback;//sdl系统回调。上面有说明
	playerCtx.wanted_spec.userdata = &playerCtx; // 回调时想带进去的参数

	// SDL打开音频播放设备
	if (SDL_OpenAudio(&playerCtx.wanted_spec, nullptr) < 0) {
		printf("can't open audio.\n");
		return -1;
	}
	// 暂停/播放音频，参数为0播放音频，非0暂停音频
	SDL_PauseAudio(0);
	return 0;
}
// init_video_paramerters 初始化视频参数，sws转换器所需的各项参数
int init_video_paramerters(PlayerContext& playerCtx) {
	// 获取视频解码器参数
	playerCtx.videoCodecParameter = playerCtx.pFormateCtx->streams[playerCtx.video_stream_index]->codecpar;
	// 获取视频解码器
	playerCtx.pVideoCodec = avcodec_find_decoder(playerCtx.videoCodecParameter->codec_id);
	if (nullptr == playerCtx.pVideoCodec) {
		printf_s("video avcodec_find_decoder failed.\n");
		return -1;
	}
	// 获取解码器上下文
	playerCtx.videoCodecCtx = avcodec_alloc_context3(playerCtx.pVideoCodec);
	// 根据视频参数配置视频编码器
	if (avcodec_parameters_to_context(playerCtx.videoCodecCtx, playerCtx.videoCodecParameter) < 0) {
		printf_s("video avcodec_parameters_to_context failed\n");
		return -1;
	}
	// 根据上下文配置视频解码器
	avcodec_open2(playerCtx.videoCodecCtx, playerCtx.pVideoCodec, nullptr);

	// 创建一个SDL窗口 SDL2.0之后的版本
	playerCtx.screen = SDL_CreateWindow("MediaPlayer",
		SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED,
		//playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height,
		1280, 720,
		SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
	if (!playerCtx.screen) {
		fprintf(stderr, "SDL: could not set video mode - exiting\n");
		exit(1);
	}
	// 创建一个SDL渲染器
	playerCtx.renderer = SDL_CreateRenderer(playerCtx.screen, -1, 0);
	// 创建一个SDL纹理
	playerCtx.texture = SDL_CreateTexture(playerCtx.renderer,
		SDL_PIXELFORMAT_YV12,
		SDL_TEXTUREACCESS_STREAMING,
		playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height);
	// 设置SDL渲染的区域
	playerCtx.sdlRect.x = 0;
	playerCtx.sdlRect.y = 0;
	playerCtx.sdlRect.w = 1280;// playerCtx.videoCodecCtx->width;
	playerCtx.sdlRect.h = 720;// playerCtx.videoCodecCtx->height;
	// 设置视频缩放转换器
	playerCtx.vi_convert_ctx = sws_getContext(playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height, playerCtx.videoCodecCtx->pix_fmt
		, playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
	// 配置视频帧和视频像素空间
	unsigned char* out_buffer = (unsigned char*)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P
		, playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height, 1));
	playerCtx.pFrameYUV = av_frame_alloc(); // 配置视频帧
	// 配置视频帧的像素数据空间
	av_image_fill_arrays(playerCtx.pFrameYUV->data, playerCtx.pFrameYUV->linesize
		, out_buffer, AV_PIX_FMT_YUV420P, playerCtx.videoCodecCtx->width, playerCtx.videoCodecCtx->height, 1);
	return 0;
}

int _tmain()
{
	// 获取项目中音频文件的地址，这部分可以将filePath换成本地的其他音频路径。
	char* base_path = SDL_GetBasePath();
	char filePath[256];
	strcpy_s(filePath, base_path);
	strcat_s(filePath, "J.Fla - Billie Jean (cover by J.Fla).mp4");

	// 注册所有编码器
	av_register_all();
	
	// 音视频的环境
	PlayerContext playerCtx;

	//读取文件头的格式信息储存到pFormateCtx中
	if (avformat_open_input(&playerCtx.pFormateCtx, filePath, nullptr, 0) != 0) {
		printf_s("avformat_open_input failed.\n");
		return -1;
	}
	//读取文件中的流信息储存到pFormateCtx中
	if (avformat_find_stream_info(playerCtx.pFormateCtx, nullptr) < 0) {
		printf_s("avformat_find_stream_info failed.\n");
		return -1;
	}
	// 将文件信息储存到标准错误上
	av_dump_format(playerCtx.pFormateCtx, 0, filePath, 0);

	// 查找音频流和视频流的位置
	for (unsigned i = 0; i < playerCtx.pFormateCtx->nb_streams; ++i)
	{
		if (AVMEDIA_TYPE_VIDEO == playerCtx.pFormateCtx->streams[i]->codecpar->codec_type
			&& playerCtx.video_stream_index < 0) { // 获取视频流位置
			playerCtx.video_stream_index = i;
			playerCtx.video_stream = playerCtx.pFormateCtx->streams[i];
		}
		if (AVMEDIA_TYPE_AUDIO == playerCtx.pFormateCtx->streams[i]->codecpar->codec_type
			&& playerCtx.au_stream_index < 0) { // 获取音频流位置
			playerCtx.au_stream_index = i;
			playerCtx.audio_stream = playerCtx.pFormateCtx->streams[i];
			continue;
		}
	}
	// 异常处理
	if (playerCtx.video_stream_index == -1)
		return -1;
	if (playerCtx.au_stream_index == -1)
		return -1;
	
	// 初始化 SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	// 初始化音频参数
	if (init_audio_parameters(playerCtx) < 0) {
		return -1;
	}
	// 初始化视频参数
	if (init_video_paramerters(playerCtx) < 0) {
		return -1;
	}
	
	// 解码音频pkt线程
	std::thread decodeAudioThread(decode_audio_thread, &playerCtx);
	decodeAudioThread.detach();
	// 解码视频pkt线程
	std::thread decodeVideoThread(decode_video_thread, &playerCtx);
	decodeVideoThread.detach();

	// 读取AVFrame并根据pkt的类型放入音频队列或视频队列中
	AVPacket* packet = nullptr;
	while (!playerCtx.quit) {
		// 判断缓存是否填满,填满则等待消耗后再继续填缓存
		if (playerCtx.audio_queue.size() > 50 ||
			playerCtx.video_queue.size() > 100) {
			SDL_Delay(10);
			continue;
		}
		packet = av_packet_alloc();
		av_init_packet(packet);
		if (av_read_frame(playerCtx.pFormateCtx, packet) < 0) { // 从AV文件中读取Frame
			break;
		}
		// 将音频帧存入到音频缓存队列中，在音频的解码线程中解码
		if (packet->stream_index == playerCtx.au_stream_index) { 
			playerCtx.audio_queue.push(packet);
		}
		//将视频存入到视频缓存队列中，在视频的解码线程中解码
		else if (packet->stream_index == playerCtx.video_stream_index) { 
			playerCtx.video_queue.push(packet);
		}
		else {
			av_packet_unref(packet);
			av_packet_free(&packet);
		}
	}

	// 关闭SDL音频播放设备
	SDL_CloseAudio();
	// 关闭SDL
	SDL_Quit();
	// 释放内存
	avcodec_parameters_free(&playerCtx.audioCodecParameter);
	avcodec_free_context(&playerCtx.audioCodecCtx);
	swr_free(&playerCtx.au_convert_ctx);
	av_free(playerCtx.audioCodecCtx);
	av_free(playerCtx.out_buffer);

	avcodec_free_context(&playerCtx.videoCodecCtx);
	sws_freeContext(playerCtx.vi_convert_ctx);
	av_frame_free(&playerCtx.pFrameYUV);

	return 0;
}

PlayerContext::PlayerContext() {
	pFormateCtx = nullptr; // AV文件的上下文
	quit = false;		   // 结束的标志

	// --------------------------- 音频相关参数 ---------------------------- //
	audioCodecParameter = nullptr; // 音频解码器的参数
	audioCodecCtx		= nullptr; // 音频解码器的上下文
	audioCodec			= nullptr; // 音频解码器
	audio_stream		= nullptr; // 音频流
	au_stream_index		= -1;	   // 记录音频流的位置
	audio_clk			= 0.0;	   // 当前音频时间
	audio_pts			= 0;	   // 记录当前已经播放音频的时间
	audio_pts_duration	= 0;	   // 记录当前已经播放音频的时间
	audio_pos			= nullptr; // 用来控制每次
	audio_len			= 0;	   // 用来控制

	au_convert_ctx		= nullptr;			 // 音频转换器
	out_sample_fmt		= AV_SAMPLE_FMT_S16; // 重采样格式
	out_buffer_size		= 0;				 // 重采样后的buffer大小
	out_buffer			= nullptr;			 // 重采样后的buffer

	wanted_spec			= {};				 // sdl 系统播放音频的各项参数信息
	// ------------------------------ end --------------------------------- //

	// ------------------------ 视频相关参数 --------------------------- //
	videoCodecParameter = nullptr; // 视频解码器的参数
	videoCodecCtx		= nullptr; // 视频解码器的上下文
	pVideoCodec			= nullptr; // 视频解码器
	video_stream		= nullptr; // 视频流

	vi_convert_ctx		= nullptr; // 视频转换器
	pFrameYUV			= nullptr; // 存放转换后的视频
	video_stream_index	= -1;	   // 记录视频流的位置
	video_pts			= 0;	   // 记录当前已经播放了的视频时间
	video_clk			= 0.0;	   // 当前视频帧的时间戳
	// --------------------------- end ------------------------------ //

	// ---------------------------- sdl ----------------------------- //
	screen   = nullptr; // 视频窗口
	renderer = nullptr; // 渲染器
	texture  = nullptr; // 纹理
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = 1280;
	sdlRect.h = 720;
	// --------------------------- end ------------------------------ //
}
