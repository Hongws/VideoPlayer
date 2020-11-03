

#include "pch.h"
#include "ffplaycore.h"
#include "VideoPlayer.h"
#include "VideoPlayerDlg.h"
#include "SyncLocker.h"


//指向MFC窗口的指针
CVideoPlayerDlg *g_pvideplayerdlg;

/* Since we only have one decoding thread, the Big Struct
   can be global in case we need it. */
VideoState *global_video_state;
int			g_nframe;
int			g_nindex;

SyncLocker *g_syncLocker;
//SDL_mutex *text_mutex;


char* W2M(LPWSTR wchar, char* mchar)
{
	DWORD dwFlags = 0;
	// Do the conversion.
	int len = WideCharToMultiByte(CP_OEMCP, dwFlags, wchar, -1, mchar, 0, NULL, NULL);
	if (len > 0)
	{
		int cchWide = wcslen(wchar) + 1;
		len = WideCharToMultiByte(CP_OEMCP, dwFlags, wchar, cchWide, mchar, len, NULL, NULL);
		if (len < 0)
		{
			memset(mchar, 0, 1);
			return NULL;
		}
		else {
			return mchar;
		}
	}
	else
		return NULL;
}

void g_logf(FILE *l_file, const char *fmt, ...)
{
	va_list ap;
	//time_t ti;
	//struct tm *tim;
	char tbuf[50];

	if (!l_file)
		return;

	if (g_syncLocker)	g_syncLocker->Lock();
	va_start(ap, fmt);
	//ti = time(0);
	//tim = localtime(&ti);
	//strftime(tbuf, 50, "%H:%M:%S-%d/%m", tim);
	SYSTEMTIME tm;
	GetLocalTime(&tm);
	sprintf(tbuf, "%02d:%02d:%02d.%03d-%02d/%02d", tm.wHour, tm.wMinute, tm.wSecond, tm.wMilliseconds, tm.wDay, tm.wMonth);
	
	fprintf(l_file, "%s: ", tbuf);
	vfprintf(l_file, fmt, ap);
	fprintf(l_file, "\n");
	fflush(l_file);
	if (g_syncLocker)	g_syncLocker->unLock();
}

long GetCurrentYMD()
{
	struct tm *cvtm;
	time_t	tip;
	long	nDate;

	time(&tip);
	cvtm = localtime(&tip);
	if (tip < 0)
		return -1;
	nDate = (cvtm->tm_year + 1900) * 10000 + (cvtm->tm_mon + 1) * 100 + cvtm->tm_mday;
	return nDate;
}


void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}
int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

	AVPacketList *pkt1;
	if (av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
	AVPacketList *pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {

		if (global_video_state->quit) {
			ret = -1;
			break;
		}

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

int audio_decode_frame(VideoState *is, uint8_t *audio_buf, int buf_size, double *pts_ptr) {
	int len1, data_size = 0;
	AVPacket *pkt = &is->audio_pkt;
	double pts;
	int n;

	for (;;) {
		while (is->audio_pkt_size > 0) {
			avcodec_send_packet(is->audio_ctx, pkt);
			while (avcodec_receive_frame(is->audio_ctx, &is->audio_frame) == 0) {
				len1 = is->audio_frame.pkt_size;

				if (len1 < 0) {
					/* if error, skip frame */
					is->audio_pkt_size = 0;
					break;
				}

				data_size = 2 * is->audio_frame.nb_samples * 2;
				assert(data_size <= buf_size);

				swr_convert(is->audio_swr_ctx,
					&audio_buf,
					MAX_AUDIO_FRAME_SIZE * 3 / 2,
					(const uint8_t **)is->audio_frame.data,
					is->audio_frame.nb_samples);

				g_logf(g_pvideplayerdlg->m_logsfile, "index:%d\t pts:%lld\t packet size:%d", g_nindex++, pkt->pts, pkt->size);

			}
			is->audio_pkt_data += len1;
			is->audio_pkt_size -= len1;
			if (data_size <= 0) {
				/* No data yet, get more frames */
				continue;
			}
			pts = is->audio_clock;
			*pts_ptr = pts;
			n = 2 * is->audio_ctx->channels;
			is->audio_clock += (double)data_size /
				(double)(n * is->audio_ctx->sample_rate);
			int nIndex = g_nindex - 1;
			g_logf(g_pvideplayerdlg->m_logsfile, "index:%d\t pts:%lld\t packet size:%d\t audio_clock:%50.23f", nIndex, pkt->pts, pkt->size, is->audio_clock);

			/* We have data, return it and come back for more later */
			return data_size;
		}
		if (pkt->data)
			av_packet_unref(pkt);

		if (is->quit) {
			return -1;
		}
		/* next packet */
		if (packet_queue_get(&is->audioq, pkt, 1) < 0) {
			return -1;
		}
		is->audio_pkt_data = pkt->data;
		is->audio_pkt_size = pkt->size;
		/* if update, update the audio clock w/pts */
		if (pkt->pts != AV_NOPTS_VALUE) {
			is->audio_clock = av_q2d(is->audio_st->time_base) * pkt->pts;
		}
	}	
}

// 音频设备回调
void audio_callback(void *userdata, Uint8 *stream, int len) {

	VideoState *is = (VideoState *)userdata;
	int len1, audio_size;
	double pts;
	
	SDL_memset(stream, 0, len);
	

	//向设备发送长度为len的数据
	while (len > 0) {
		//g_logf(g_pvideplayerdlg->m_logsfile, "audio_callback len:%d", len);
		//缓冲区中无数据
		if (is->audio_buf_index >= is->audio_buf_size) {
			/* We have already sent all our data; get more  从packet中解码数据*/
			audio_size = audio_decode_frame(is, is->audio_buf, sizeof(is->audio_buf), &pts);
			//g_logf(g_pvideplayerdlg->m_logsfile, "audio_callback audio_size:%d", audio_size);

			if (audio_size < 0)  //没有解码到数据或者出错，填充0
			{
				/* If error, output silence */
				is->audio_buf_size = 1024/**2*2*/;
				memset(is->audio_buf, 0, is->audio_buf_size);
			}
			else {
				is->audio_buf_size = audio_size;
			}
			is->audio_buf_index = 0;
		}
		len1 = is->audio_buf_size - is->audio_buf_index;
		if (len1 > len)
			len1 = len;
		//memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
		SDL_MixAudio(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1, SDL_MIX_MAXVOLUME);	//混音

		len -= len1;
		stream += len1;
		is->audio_buf_index += len1;
	}
}

// 定时器回调函数，发送FF_REFRESH_EVENT事件，更新显示视频帧
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque) {
	SDL_Event event;
	g_logf(g_pvideplayerdlg->m_logsfile, "sdl event.type:FF_REFRESH_EVENT");
	event.type = FF_REFRESH_EVENT;
	event.user.data1 = opaque;
	SDL_PushEvent(&event);
	return 0; /* 0 means stop timer */
}

/* schedule a video refresh in 'delay' ms  设置定时器*/ 
static void schedule_refresh(VideoState *is, int delay) {
	SDL_AddTimer(delay, sdl_refresh_timer_cb, is);
}

// 视频播放
void video_display(VideoState *is, SDL_Renderer* renderer, SDL_Texture **texture, bool &bCrtSdlText)
{
	SDL_Rect rect;
	VideoPicture *vp;
	//AVPicture pict;
	float aspect_ratio;
	//int w=0, h=0, x=0, y=0;
	//screen->w;
	//int i;

	//renderer->w;

	vp = &is->pictq[is->pictq_rindex];
	if (vp->frame) {
		//if (is->video_st->codec->sample_aspect_ratio.num == 0) {
		//	aspect_ratio = 0;
		//}
		//else {
		//	aspect_ratio = av_q2d(is->video_st->codec->sample_aspect_ratio) *
		//		is->video_st->codec->width / is->video_st->codec->height;
		//}
		//if (aspect_ratio <= 0.0) {
		//	aspect_ratio = (float)is->video_st->codec->width /
		//		(float)is->video_st->codec->height;
		//}

		if (bCrtSdlText) {
			//SDL_SetWindowSize(win, screen_width, screen_height);
			//SDL_SetWindowPosition(win, screen_left, screen_top);
			//SDL_ShowWindow(win);

			//Uint32 pixformat = SDL_PIXELFORMAT_IYUV;
			//SDL_Texture *texturgg = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, is->video_ctx->width, is->video_ctx->height);
			//create texture for render
			*texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, is->video_ctx->width, is->video_ctx->height);
			//texture = SDL_CreateTexture(renderer,
			//	pixformat,
			//	SDL_TEXTUREACCESS_STREAMING,
			//	screen_width,
			//	screen_height);
			bCrtSdlText = false;
		}




		rect.x = 0;
		rect.y = 0;
		rect.w = is->video_ctx->width;
		rect.h = is->video_ctx->height;

		SDL_UpdateYUVTexture(*texture, NULL,
			vp->frame->data[0], vp->frame->linesize[0],
			vp->frame->data[1], vp->frame->linesize[1],
			vp->frame->data[2], vp->frame->linesize[2]);
		//SDL_LockMutex(text_mutex);
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, *texture, NULL, NULL);
		SDL_RenderPresent(renderer);
		//SDL_UnlockMutex(text_mutex);

	}
}

double get_audio_clock(VideoState *is) {
	double pts;
	int hw_buf_size, bytes_per_sec, n;

	//上一步获取的PTS
	pts = is->audio_clock;
	// 音频缓冲区还没有播放的数据
	hw_buf_size = is->audio_buf_size - is->audio_buf_index;
	// 每秒钟音频播放的字节数
	bytes_per_sec = 0;
	n = is->audio_ctx->channels * 2;
	if (is->audio_st) {
		bytes_per_sec = is->audio_ctx->sample_rate * n;
	}
	if (bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}
	return pts;
}

void video_refresh_timer(void *userdata, SDL_Renderer* renderer, SDL_Texture **texture, bool &bCrtSdlText) 
{
	VideoState *is = (VideoState *)userdata;
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	if (is->video_st) 
	{
		if (is->pictq_size == 0) {
			schedule_refresh(is, 1);
		}
		else {
			// 从数组中取出一帧视频帧
			vp = &is->pictq[is->pictq_rindex];

			is->video_current_pts = vp->pts;
			is->video_current_pts_time = av_gettime();
			// 当前Frame时间减去上一帧的时间，获取两帧间的时差
			delay = vp->pts - is->frame_last_pts;
			if (delay <= 0 || delay >= 1.0) {
				// 延时小于0或大于1秒（太长）都是错误的，将延时时间设置为上一次的延时时间
				delay = is->frame_last_delay;
			}
			// 保存延时和PTS，等待下次使用
			is->frame_last_delay = delay;
			is->frame_last_pts = vp->pts;

			// 获取音频Audio_Clock
			ref_clock = get_audio_clock(is);
			// 得到当前PTS和Audio_Clock的差值
			diff = vp->pts - ref_clock;

			/* Skip or repeat the frame. Take delay into account
			   FFPlay still doesn't "know if this is the best guess." */
			sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
				if (diff <= -sync_threshold) {
					delay = 0;
				}
				else if (diff >= sync_threshold) {
					delay = 2 * delay;
				}
			}
			is->frame_timer += delay;
			// 最终真正要延时的时间
			actual_delay = is->frame_timer - (av_gettime() / 1000000.0);
			if (actual_delay < 0.010) {
				// 延时时间过小就设置最小值
				actual_delay = 0.010;
			}
			// 根据延时时间重新设置定时器，刷新视频
			schedule_refresh(is, (int)(actual_delay * 1000 + 0.5));

			// 视频帧显示
			video_display(is, renderer, texture, bCrtSdlText);

			// 更新视频帧数组下标
			if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
				is->pictq_rindex = 0;
			}
			SDL_LockMutex(is->pictq_mutex);
			// 视频帧数组减一
			is->pictq_size--;
			SDL_CondSignal(is->pictq_cond);
			SDL_UnlockMutex(is->pictq_mutex);
		}
	}
	else {
		schedule_refresh(is, 100);
	}
	
}

//解码后视频帧保存
int queue_picture(VideoState *is, AVFrame *pFrame, double pts) {

	VideoPicture *vp;
	//AVCodecContext *pCodecCtx;
	   	 
	/* wait until we have space for a new pic */
	SDL_LockMutex(is->pictq_mutex);
	while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
		!is->quit) {
		SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
	SDL_UnlockMutex(is->pictq_mutex);

	if (is->quit)
		return -1;

	// windex is set to 0 initially
	vp = &is->pictq[is->pictq_windex];

	//    /* allocate or resize the buffer! */
	if (!vp->frame ||
		vp->width != is->video_ctx->width ||
		vp->height != is->video_ctx->height) {

		vp->frame = av_frame_alloc();
		if (is->quit) {
			return -1;
		}
	}

	/* We have a place to put our picture on the queue */
	if (vp->frame) {

		vp->pts = pts;

		vp->frame = pFrame;
		/* now we inform our display thread that we have a pic ready */
		if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
			is->pictq_windex = 0;
		}

		SDL_LockMutex(is->pictq_mutex);
		is->pictq_size++;
		SDL_UnlockMutex(is->pictq_mutex);
	}
	return 0;
}

	

////  视频同步，获取正确的视频PTS
double synchronize_video(VideoState *is, AVFrame *src_frame, double pts) {

	double frame_delay;

	if (pts != 0) {
		is->video_clock = pts;
	}
	else {
		pts = is->video_clock;
	}
	/* update the video clock */
	frame_delay = av_q2d(is->video_ctx->time_base);
	/* if we are repeating a frame, adjust clock accordingly */
	frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
	is->video_clock += frame_delay;
	return pts;
}

// 视频解码
int video_thread(void *arg) 
{
	VideoState *is = (VideoState *)arg;
	AVPacket pkt1, *packet = &pkt1;
	//int frameFinished;
	AVFrame *pFrame;
	double pts;

	//为frame 申请内存
	pFrame = av_frame_alloc();

	for (;;) {
		if (packet_queue_get(&is->videoq, packet, 1) < 0) {
			// means we quit getting packets
			break;
		}
		

		// Decode video frame
		avcodec_send_packet(is->video_ctx, packet);
		while (avcodec_receive_frame(is->video_ctx, pFrame) == 0)
		{
			
			if ((pts = pFrame->best_effort_timestamp) != AV_NOPTS_VALUE) {
			}
			else {
				pts = 0;
			}
			pts *= av_q2d(is->video_st->time_base);
			//printf("%m.nf",a);m是总宽度，你可以设置大一些，n是小数位数，设置个3或者6
			//g_logf(g_pvideplayerdlg->m_logsfile, "decode %d frame\t pts:%lf", g_nframe++, pts);
			g_logf(g_pvideplayerdlg->m_logsfile, "decode %d frame\t pts:%50.23f", g_nframe++, pts);

			pts = synchronize_video(is, pFrame, pts);
			if (queue_picture(is, pFrame, pts) < 0) {
				break;
			}
			av_packet_unref(packet);
		}
	}
	//av_free(pFrame);
	av_frame_free(&pFrame);
	return 0;
}

int stream_component_open(VideoState *is, int stream_index) 
{

	AVFormatContext *pFormatCtx = is->pFormatCtx;
	AVCodecContext *codecCtx = NULL;
	AVCodec *codec = NULL;
	//AVDictionary *optionsDict = NULL;
	SDL_AudioSpec wanted_spec;// , spec;

	if (stream_index < 0 || stream_index >= pFormatCtx->nb_streams) 
	{
		g_logf(g_pvideplayerdlg->m_logsfile, "Fail stream_index:%d", stream_index);
		return -1;
	}

	// Get a pointer to the codec context for the video stream 从 vedio stream 中获取对应的解码器上下文的指针
	//codecCtx = pFormatCtx->streams[stream_index]->codec;
	codecCtx = avcodec_alloc_context3(NULL);
	int ret = avcodec_parameters_to_context(codecCtx, pFormatCtx->streams[stream_index]->codecpar);
	if (ret < 0)
	{
		g_logf(g_pvideplayerdlg->m_logsfile, "Fail copy codec!");
		return -1;
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "Copy codec successful");

	codec = avcodec_find_decoder(codecCtx->codec_id);
	if (!codec) {
		g_logf(g_pvideplayerdlg->m_logsfile, "Fail find codec!");
		return -1;
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "Find codec successful");

	// 打开解码器
	if (avcodec_open2(codecCtx, codec, NULL) < 0) 
	{
		g_logf(g_pvideplayerdlg->m_logsfile, "Fail supported codec!");
		return -1;
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "supported codec successful");


	// 音频流参数配置，打开音频设备，播放音频
	if (codecCtx->codec_type == AVMEDIA_TYPE_AUDIO) 
	{
		// Set audio settings from codec info
		wanted_spec.freq = codecCtx->sample_rate;
		wanted_spec.format = AUDIO_S16SYS;	//音频数据格式
		wanted_spec.channels = 2;//codecCtx->channels;
		wanted_spec.silence = 0;
		wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
		wanted_spec.callback = audio_callback;
		wanted_spec.userdata = is;

		g_logf(g_pvideplayerdlg->m_logsfile, "wanted spec: channels(声道):%d, sample_fmt(音频数据格式):%d, sample_rate(采样率):%d", 2, AUDIO_S16SYS, codecCtx->sample_rate);

		if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
			//fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
			g_logf(g_pvideplayerdlg->m_logsfile, "Fail SDL_OpenAudio: %s !", SDL_GetError());
			return -1;
		}
		else
			g_logf(g_pvideplayerdlg->m_logsfile, "SDL_OpenAudio  successful");

		is->audioStream = stream_index;
		is->audio_st = pFormatCtx->streams[stream_index];
		is->audio_ctx = codecCtx;
		is->audio_buf_size = 0;

		is->audio_buf_index = 0;
		memset(&is->audio_pkt, 0, sizeof(is->audio_pkt));
		packet_queue_init(&is->audioq);

		//Out Audio Param
		uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;

		int out_nb_samples = is->audio_ctx->frame_size;

		int out_sample_rate = is->audio_ctx->sample_rate;
		int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);


		int64_t in_channel_layout = av_get_default_channel_layout(is->audio_ctx->channels);

		// 音频重采样
		struct SwrContext *audio_convert_ctx;
		audio_convert_ctx = swr_alloc();
		swr_alloc_set_opts(audio_convert_ctx,
			out_channel_layout,
			AV_SAMPLE_FMT_S16,
			out_sample_rate,
			in_channel_layout,
			is->audio_ctx->sample_fmt,
			is->audio_ctx->sample_rate,
			0,
			NULL);

		g_logf(g_pvideplayerdlg->m_logsfile, "Create sample rate converter for conversion of %dHz %s %d channels to %dHz %s %d channels!",
			is->audio_ctx->sample_rate, av_get_sample_fmt_name(is->audio_ctx->sample_fmt), is->audio_ctx->channels,
			out_sample_rate, av_get_sample_fmt_name(AV_SAMPLE_FMT_S16), out_channel_layout);

		swr_init(audio_convert_ctx);
		is->audio_swr_ctx = audio_convert_ctx;

		// 开始播放音频，audio_callback回调
		g_logf(g_pvideplayerdlg->m_logsfile, "SDL_PauseAudio(0) audio_callback");
		SDL_PauseAudio(0);

	}

	if (codecCtx->codec_type == AVMEDIA_TYPE_VIDEO)
	{
		is->videoStream = stream_index;
		is->video_st = pFormatCtx->streams[stream_index];
		is->video_ctx = codecCtx;

		is->frame_timer = (double)av_gettime() / 1000000.0;
		is->frame_last_delay = 40e-3;
		is->video_current_pts_time = av_gettime();

		packet_queue_init(&is->videoq);

		// 创建视频解码线程
		g_logf(g_pvideplayerdlg->m_logsfile, "------------start video_thread------------");
		is->video_tid = SDL_CreateThread(video_thread, "video_thread", is);
	}

	return 0;
}

int decode_interrupt_cb(void *opaque) {
	return (global_video_state && global_video_state->quit);
}

//获取音频、视频流，并将packet放入队列中
int decode_thread(void *arg) {

	VideoState *is = (VideoState *)arg;
	AVFormatContext *pFormatCtx = NULL;
	AVPacket pkt1, *packet = &pkt1;

	int video_index = -1;
	int audio_index = -1;
	int i;

	//AVDictionary *io_dict = NULL;
	//AVIOInterruptCB callback;

	is->videoStream = -1;
	is->audioStream = -1;

	global_video_state = is;
	//// will interrupt blocking functions if we quit!
	//callback.callback = decode_interrupt_cb;
	//callback.opaque = is;
	//if (avio_open2(&is->io_context, is->filename, 0, &callback, &io_dict))
	//{
	//	g_logf(g_pvideplayerdlg->m_logsfile, "Unable to open I/O for %s !", is->filename);
	//	return -1;
	//}
	//else
	//	g_logf(g_pvideplayerdlg->m_logsfile, "open I/O for %s success", is->filename);

	int err_code;
	char errors[1024] = { 0 };
	// Open video file
	if ((err_code = avformat_open_input(&pFormatCtx, is->filename, NULL, NULL)) < 0)
	//if (avformat_open_input(&pFormatCtx, is->filename, NULL, NULL) != 0)
	{
		av_strerror(err_code, errors, 1024);
		g_logf(g_pvideplayerdlg->m_logsfile, "Could not open source file %s, %d(%s)", is->filename, err_code, errors);
		return -1; // Couldn't open file
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "%s open successful", is->filename);

	is->pFormatCtx = pFormatCtx;

	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		g_logf(g_pvideplayerdlg->m_logsfile, "Couldn't find stream information!");
		return -1; // Couldn't find stream information
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "Stream information find successful");

	// Dump information about file onto standard error
	av_dump_format(pFormatCtx, 0, is->filename, 0);

	// Find the first video stream

	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
			video_index < 0) {
			video_index = i;
		}
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
			audio_index < 0) {
			audio_index = i;
		}
	}

	g_logf(g_pvideplayerdlg->m_logsfile, "audio_index:%d, video_index:%d", audio_index, video_index);

	if (audio_index >= 0) {
		g_logf(g_pvideplayerdlg->m_logsfile, "------------start audio_index------------");
		stream_component_open(is, audio_index);
	}
	if (video_index >= 0) {
		g_logf(g_pvideplayerdlg->m_logsfile, "------------start video_index------------");
		stream_component_open(is, video_index);
	}

	if (is->videoStream < 0 || is->audioStream < 0) {
		goto fail;
	}

	// main decode loop

	for (;;) {
		if (is->quit) {
			break;
		}
		// seek stuff goes here
		if (is->audioq.size > MAX_AUDIOQ_SIZE ||
			is->videoq.size > MAX_VIDEOQ_SIZE) {
			SDL_Delay(10);
			continue;
		}
		if (av_read_frame(is->pFormatCtx, packet) < 0) {
			if (is->pFormatCtx->pb->error == 0) {
				SDL_Delay(100); /* no error; wait for user input */
				continue;
			}
			else {
				break;
			}
		}
		// Is this a packet from the video stream?
		if (packet->stream_index == is->videoStream) {
			packet_queue_put(&is->videoq, packet);
		}
		else if (packet->stream_index == is->audioStream) {
			packet_queue_put(&is->audioq, packet);
		}
		else {
			//av_free_packet(packet);
			av_packet_unref(packet);
		}
	}
	/* all done - wait for it */
	while (!is->quit) {
		SDL_Delay(100);
	}

fail:
	if (1) {
		SDL_Event event;
		event.type = FF_QUIT_EVENT;
		event.user.data1 = is;
		SDL_PushEvent(&event);
	}
	return 0;
}


void FFmpegLogFunc(void *ptr, int level, const char *fmt, va_list vl) 
{
	//if (NULL == g_pvideplayerdlg->m_logsfile)
	//	g_pvideplayerdlg->m_logsfile = fopen("1.txt", "a");

	if (level > AV_LOG_INFO)   return;

	if (g_pvideplayerdlg->m_logsfile)
	{
		//if(level == AV_LOG_INFO)
		//	fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_INFO: ");
		switch (level)
		{
		case AV_LOG_QUIET:
			fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_QUIET: ");
			break;
		case AV_LOG_PANIC:
			fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_PANIC: ");
			break;
		case AV_LOG_FATAL:
			fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_FATAL: ");
			break;
		case AV_LOG_ERROR:
			fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_ERROR: ");
			break;
		case AV_LOG_WARNING:
			fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_WARNING: ");
			break;
		//case AV_LOG_INFO:
		//	fprintf(g_pvideplayerdlg->m_logsfile, "AV_LOG_INFO: ");
		//	break;
		default:
			break;
		}
		vfprintf(g_pvideplayerdlg->m_logsfile, fmt, vl);
		fflush(g_pvideplayerdlg->m_logsfile);
	}

	//FILE *fp = fopen("FFmpegLog.txt", "a+");
	//if (fp) {
		//vfprintf(fp, fmt, vl);
		//fflush(fp);
		//fclose(fp);
	//}
}

//SDL2源代码分析1：
//初始化（SDL_Init()）
//窗口（SDL_Window）
//渲染器（SDL_Renderer）
//纹理（SDL_Texture）
//更新纹理（SDL_UpdateTexture()）
//复制到渲染器（SDL_RenderCopy()）
//显示（SDL_RenderPresent()）

unsigned long __stdcall thread_start_play(void* lpParam)
{
	g_pvideplayerdlg = (CVideoPlayerDlg *)lpParam;
	g_nframe = 0;
	g_nindex = 0;
	g_syncLocker = SyncLocker::createNew();

	av_log_set_callback(FFmpegLogFunc);
	//av_log_set_level(AV_LOG_DEBUG);
	//av_log(NULL, AV_LOG_WARNING, "start to sdfafsa\n");

	SDL_Event       event;

	VideoState      *is;

	struct SDL_Window     *pScreen;
	struct SDL_Renderer   *pRenderer;
	struct SDL_Texture    *pTexture = NULL;

	bool			bCrtSdlText = true;


	is = (VideoState*)av_mallocz(sizeof(VideoState));

	//if (argc < 2) {
	//	fprintf(stderr, "Usage: test <file>\n");
	//	exit(1);
	//}
	// Register all formats and codecs
	av_register_all();

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		g_logf(g_pvideplayerdlg->m_logsfile, "Could not initialize SDL - %s!", SDL_GetError());
		return 0L;
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "Initialize SDL successful");



	//pScreen = SDL_CreateWindow("audio & video", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 900, 500, SDL_WINDOW_OPENGL);
	pScreen = SDL_CreateWindowFrom(g_pvideplayerdlg->GetDlgItem(IDC_PICTURE)->GetSafeHwnd());


	if (!pScreen) {
		g_logf(g_pvideplayerdlg->m_logsfile, "SDL: could not set video mode - exiting!");
		return -1L;
	}
	else
		g_logf(g_pvideplayerdlg->m_logsfile, "SDL: set video mode - successful");
	//SDL_Window *windows = pScreen;

	//pScreen->windowed;
	pRenderer = SDL_CreateRenderer(pScreen, -1, 0);
	//text_mutex = SDL_CreateMutex();

	//SDL_SetWindowSize(pScreen, 848, 480);
	//SDL_SetWindowPosition(pScreen, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	//SDL_ShowWindow(pScreen);

	//SDL_Texture* texture = SDL_CreateTexture(pRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, 848, 480);

	
	//av_strlcpy(is->filename, "2.mp4", 1024);
	memcpy(is->filename, "1.mp4", strlen("1.mp4"));

	is->pictq_mutex = SDL_CreateMutex();
	is->pictq_cond = SDL_CreateCond();
	// 定时刷新器
	g_logf(g_pvideplayerdlg->m_logsfile, "sdl schedule_refresh 40ms");
	schedule_refresh(is, 40);

	g_logf(g_pvideplayerdlg->m_logsfile, "------------start decode_thread------------");
	is->parse_tid = SDL_CreateThread(decode_thread, "decode_thread", is);
	if (!is->parse_tid) {
		av_free(is);
		return -1;
	}
	for (;;) {
		// 等待SDL事件，否则阻塞
		SDL_WaitEvent(&event);
		switch (event.type) {
		case FF_QUIT_EVENT:
		case SDL_QUIT:
			is->quit = 1;
			/*
			 * If the video has finished playing, then both the picture and
			 * audio queues are waiting for more data.  Make them stop
			 * waiting and terminate normally.
			 */
			SDL_CondSignal(is->audioq.cond);
			SDL_CondSignal(is->videoq.cond);
			SDL_Quit();
			return 0;
			break;
		//case FF_ALLOC_EVENT:
		//	alloc_picture(event.user.data1, pRenderer);
		//	break;
		case FF_REFRESH_EVENT: // 定时器刷新事件
			video_refresh_timer(event.user.data1, pRenderer, &pTexture, bCrtSdlText);
			break;
		default:
			break;
		}
	}

	//SDL_DestroyTexture(texture);

	return 0L;
}

void ReleaseFfplaycore()
{
	if (g_syncLocker)
	{
		g_syncLocker->reclaim();
		g_syncLocker = nullptr;
	}
}





