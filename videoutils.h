#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>

#define VIDEO_PICTURE_QUEUE_SIZE 2
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
}PacketQueue;

typedef struct VideoPicture {
    AVFrame *pictYUV;
    // uint8_t *data[AV_NUM_DATA_POINTERS];
    // int linesize[AV_NUM_DATA_POINTERS];
    uint64_t pts;
    int width, height;
}VideoPicture;

typedef struct VideoState {
    AVFormatContext *formatCtx;
    int videoStreamIndex, audioStreamIndex;
    double video_clock;

    AVStream *audio_stm;
    PacketQueue audioq;
    struct SwrContext *swr_ctx;
    uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    AVPacket audio_pkt;
    uint8_t *audio_pke_data;
    int audio_pkt_size;

    AVStream *video_stm;
    PacketQueue videoq;
    struct SwsContext *sws_ctx;
    VideoPicture pict_q[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    SDL_mutex *pictq_mutex;
    SDL_cond *pictq_cond;
    
    SDL_Thread *parse_tid;
    SDL_Thread *video_tid;

    char filename[1024];
    int quit;
}VideoState;




void packet_queue_init(PacketQueue *queue);

int packet_queue_put(PacketQueue *queue, AVPacket *pkt);

int packet_queue_get(PacketQueue *queue, AVPacket *pkt, int block, int *quit);






