#pragma once

#pragma warning(disable:4996)

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavformat/avio.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "libavutil/avstring.h"
#include "libswresample/swresample.h"
#include "libavutil/samplefmt.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "sdl2/SDL.h"
	//#include "SDL/SDL.h"
};

#ifdef __MINGW32__
#undef main /* Prevents SDL from overriding main() */
#endif


#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIO_FRAME_SIZE 192000

#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)

#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 10.0

#define FF_REFRESH_EVENT (SDL_USEREVENT)
#define FF_QUIT_EVENT (SDL_USEREVENT + 1)

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct PacketQueue {
	AVPacketList *first_pkt, *last_pkt;
	int nb_packets;
	int size;
	SDL_mutex *mutex;
	SDL_cond *cond;
} PacketQueue;


typedef struct VideoPicture {
	//SDL_Texture *texture;
	AVFrame *frame;
	//SDL_Overlay *bmp;
	int width, height; /* source height & width */
	double pts;
} VideoPicture;

typedef struct VideoState {

	//multi-media file
	char filename[1024];
	AVFormatContext *pFormatCtx;
	int videoStream, audioStream;


	double audio_clock;
	double frame_timer;
	double frame_last_pts;
	double frame_last_delay;

	double video_clock; ///<pts of last decoded frame / predicted pts of next decoded frame
	double video_current_pts; ///<current displayed pts (different from video_clock if frame fifos are used)
	int64_t video_current_pts_time;  ///<time (av_gettime) at which we updated video_current_pts - used to have running video pts

	//audio
	AVStream *audio_st;
	AVCodecContext *audio_ctx;
	PacketQueue audioq;
	uint8_t audio_buf[(MAX_AUDIO_FRAME_SIZE * 3) / 2];
	unsigned int audio_buf_size;
	unsigned int audio_buf_index;
	AVFrame audio_frame;

	AVPacket audio_pkt;
	uint8_t *audio_pkt_data;
	int audio_pkt_size;
	int audio_hw_buf_size;
	struct SwrContext *audio_swr_ctx;


	//video
	AVStream *video_st;
	AVCodecContext *video_ctx;
	PacketQueue videoq;


	VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int pictq_size, pictq_rindex
		, pictq_windex;
	SDL_mutex *pictq_mutex;
	SDL_cond *pictq_cond;

	SDL_Thread *parse_tid;
	SDL_Thread *video_tid;

	int quit;
} VideoState;



/**
*	Description:
*		宽字节转多字节
*/
char* W2M(LPWSTR wchar, char* mchar);

/**
*	Description:
*		获取当前日期	格式:20201020
*/
long GetCurrentYMD();

/**
*	Description:
*		日志
*/
void g_logf(FILE *l_file, const char *fmt, ...);



/**
*	Description:
*		开始播放
*/
unsigned long __stdcall thread_start_play(void* lpParam);

void ReleaseFfplaycore();


