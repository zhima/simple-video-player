#include <stdlib.h>
#include <stdio.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "videoutils.h"


#define SDL_AUDIO_BUFFER_SIZE 1024
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#define MAX_VIDEOQ_SIZE (5 * 256 * 1025)

static uint32_t FF_QUIT_EVENT = 0;
static uint32_t FF_REFRESH_EVENT = 0;
static uint32_t FF_ALLOC_EVENT = 0;

static AVFrame *audioFrame = NULL;
static int audio_out_linesize = -1;
static int audio_channels = 0;
static uint8_t *out_buffer = NULL;
static SDL_Window *sdlWindow = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static SDL_mutex *sdlWindow_alloc_mutex = NULL;
static SDL_cond *sdlWindow_alloc_cond = NULL;
static uint64_t global_video_pkt_pts = AV_NOPTS_VALUE;

VideoState *global_video_state;

int audio_decode_frame(VideoState *vs, uint8_t *audio_buf, int buf_size);
void audio_callback(void *userdata, Uint8 *stream, int len);
void clearAtExit(void);
int decode_thread(void *userdata);
void video_refresh_timer(void *userdata);
static void schedule_refresh(VideoState *vs, int delay);
static Uint32 sdl_refresh_timer_cb(Uint32 interval, void *opaque);
int decode_interrupt_cb(void *);
int stream_component_open(VideoState *vs, int stream_index);
void stream_component_close(VideoState *vs, int stream_index);
int allocate_sdlwindow(void *userdata);
int get_buffer(struct AVCodecContext *ctx, AVFrame *pic);
void release_buffer(struct AVCodecContext *ctx, AVFrame *pic);

int main (int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage:./a.out videoFileName\n");
        return -1;
    }
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
        fprintf(stderr, "SDL_Init Error:%s", SDL_GetError());
        return -1;
    }
    atexit(clearAtExit);

    /*sdlWindow = SDL_CreateWindow("sdl-ffmpeg player",
         SDL_WINDOWPOS_CENTERED,
         SDL_WINDOWPOS_CENTERED,
         640, 480,
         SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
     renderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
     if (NULL == renderer) {
         fprintf(stderr, "SDL_CreateRenderer error:%s\n",SDL_GetError());
         return -1;
     }
     texture = SDL_CreateTexture(renderer,
         SDL_PIXELFORMAT_YV12,
         SDL_TEXTUREACCESS_STREAMING,
         640, 480);
     if (NULL == texture) {
         fprintf(stderr, "SDL_CreateTexture error:%s\n",SDL_GetError());
         return -1;
    }*/
    sdlWindow_alloc_mutex = SDL_CreateMutex();
    sdlWindow_alloc_cond = SDL_CreateCond();


    FF_QUIT_EVENT = SDL_RegisterEvents(3);
    if (FF_QUIT_EVENT == ((Uint32) - 1))
    {
        //not enough user-defined events left, reset to zero
        FF_QUIT_EVENT = 0;
    }
    FF_REFRESH_EVENT = FF_QUIT_EVENT + 1;
    FF_ALLOC_EVENT = FF_QUIT_EVENT + 2;
    VideoState *vs;

    vs = av_mallocz(sizeof(VideoState));
    av_strlcpy(vs->filename, argv[1], sizeof(vs->filename));
    vs->pictq_mutex = SDL_CreateMutex();
    vs->pictq_cond = SDL_CreateCond();
    vs->quit = 0;
    int pictq_index = 0;
    for (pictq_index = 0; pictq_index < VIDEO_PICTURE_QUEUE_SIZE; pictq_index++) {
        vs->pict_q[pictq_index].pictYUV = NULL;
    }

    schedule_refresh(vs, 40);

    vs->parse_tid = SDL_CreateThread(decode_thread, "decode_thread", vs);
    if (!vs->parse_tid) {
        av_free(vs);
        return -1;
    }

    SDL_Event event;
    SDL_zero(event);
    for(;;) {
        SDL_WaitEvent(&event);
        if (FF_REFRESH_EVENT == event.type) {
                video_refresh_timer(event.user.data1);
        } else if (FF_QUIT_EVENT == event.type) {
                vs->quit = 1;
                break;
        } else if (SDL_QUIT == event.type) {
            fprintf(stderr, "receive SDL QUIT event");
            vs->quit = 1;
            break;
        } else if (FF_ALLOC_EVENT == event.type) {
            if (allocate_sdlwindow(event.user.data1) < 0) {
                vs->quit = 1;
                break;
            }
        }
    }

    exit:
        SDL_CondBroadcast(vs->pictq_cond);
        SDL_CondBroadcast(vs->audioq.cond);
        SDL_CondBroadcast(vs->videoq.cond);
        SDL_CondBroadcast(sdlWindow_alloc_cond);
        SDL_WaitThread(vs->parse_tid, NULL);
    
    if (texture) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(sdlWindow);
    }
    SDL_Quit();

	return 0;
}

int decode_thread(void *userdata) {
    VideoState *vs = (VideoState *)userdata;
    global_video_state = vs;
    int ret = 0;
	av_register_all();

    if (avformat_open_input(&vs->formatCtx, vs->filename, NULL, NULL) < 0) {
        ret = -1;
        goto fail;
    }
    vs->formatCtx->interrupt_callback.callback = decode_interrupt_cb;
    vs->formatCtx->interrupt_callback.opaque = vs;

    if (avformat_find_stream_info(vs->formatCtx, NULL) < 0) {
        ret = -1;
        goto fail;
    }

    av_dump_format(vs->formatCtx, 0, vs->filename, 0);
    
    int i;
    int videoStream = -1;
    int audioStream = -1;
    for(i = 0; i < vs->formatCtx->nb_streams;i++) {
        if (vs->formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0) {
            videoStream = i;
        }

        if (vs->formatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO && audioStream < 0) {
            audioStream = i;

        }
    }
    
    if (videoStream == -1 || audioStream == -1) {
        ret = -1;
        goto fail; // Didn't find a video/audio stream
    }

    if (stream_component_open(vs, videoStream) < 0) {
        ret = -1;
        goto fail;
    }
    if (stream_component_open(vs, audioStream) < 0) {
        ret = -1;
        goto fail;
    }

    SDL_Event alloc_event;
    alloc_event.type = FF_ALLOC_EVENT;
    alloc_event.user.data1 = vs;
    SDL_PushEvent(&alloc_event);
    SDL_LockMutex(sdlWindow_alloc_mutex);
    SDL_CondWait(sdlWindow_alloc_cond, sdlWindow_alloc_mutex);
    SDL_UnlockMutex(sdlWindow_alloc_mutex);

    AVPacket packet;

    for(;;) {
        if (vs->quit) {
            break;
        }

        if (vs->audioq.size > MAX_AUDIOQ_SIZE ||
                vs->videoq.size > MAX_VIDEOQ_SIZE) {
            SDL_Delay(10);
            continue;
        }
        

        if (av_read_frame(vs->formatCtx, &packet) < 0) {
            if (vs->formatCtx->pb->error == 0) {
                SDL_Delay(100);
                continue;
            } else {
                break;
            }
            
        }

        if (packet.stream_index == vs->videoStreamIndex) {
            packet_queue_put(&vs->videoq, &packet);
        } else if (packet.stream_index == vs->audioStreamIndex) {
            packet_queue_put(&vs->audioq, &packet);
        } else {
            av_free_packet(&packet);
        }

       
    }

    while(!vs->quit) {
        SDL_Delay(100);
    }

    ret = 0;

    fail:
        if (vs->audioStreamIndex >= 0) {
            stream_component_close(vs, vs->audioStreamIndex);
        }

        if (vs->videoStreamIndex >= 0) {
            stream_component_close(vs, vs->videoStreamIndex);
        }

        if (vs->formatCtx) {
            avformat_close_input(&vs->formatCtx);
        }

        if (ret != 0) {
            SDL_Event event;
            event.type = FF_QUIT_EVENT;
            event.user.data1 = vs;
            SDL_PushEvent(&event);
        }
        return ret;

    
}

int queue_picture(VideoState *vs, AVFrame *pFrame) {
    VideoPicture *vp;
    int dst_pix_fmt;
    AVPicture pict;

    SDL_LockMutex(vs->pictq_mutex);
    while(vs->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !vs->quit) {
        SDL_CondWait(vs->pictq_cond, vs->pictq_mutex);
    }

    SDL_UnlockMutex(vs->pictq_mutex);

    if (vs->quit) {
        return -1;
    }

    vp = &vs->pict_q[vs->pictq_windex];
    if (NULL == vp->pictYUV ||
        vp->width != vs->video_stm->codec->width ||
        vp->height != vs->video_stm->codec->height) {
        if (NULL != vp->pictYUV) {
            av_free(vp->pictYUV);
            vp->pictYUV = NULL;
        }
        vp->pictYUV = av_frame_alloc(); // or av_frame_alloc()
        if (NULL == vp->pictYUV) {
            return -1;
        }
        uint8_t *buffer = NULL;
        int numBytes = 0;
        AVCodecContext *videoCodecCtx = vs->video_stm->codec;
        numBytes = avpicture_get_size(PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height);
        buffer = (uint8_t *)av_mallocz(numBytes * sizeof(uint8_t));
        avpicture_fill((AVPicture *)vp->pictYUV, buffer, PIX_FMT_YUV420P, videoCodecCtx->width, videoCodecCtx->height);

    }
    if (vp->pictYUV) {
        sws_scale(
                    vs->sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    vs->video_stm->codec->height,
                    vp->pictYUV->data,
                    vp->pictYUV->linesize
                );
        //av_frame_copy ? copy meta data?
    }
    vp->width = vs->video_stm->codec->width;
    vp->height = vs->video_stm->codec->height;
    if (++vs->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE) {
        vs->pictq_windex = 0;
    }

    SDL_LockMutex(vs->pictq_mutex);
    vs->pictq_size++;
    SDL_UnlockMutex(vs->pictq_mutex);
    return 0;

}

int video_thread(void *userdata) {
    VideoState *vs = (VideoState *)userdata;
    AVCodecContext *videoCodecCtx = vs->video_stm->codec;
    AVPacket pkt1, *packet = &pkt1;
    int got_frame = 0;
    
    double pts = 0;
    AVFrame *frame;
    frame = av_frame_alloc();
    for (;;) {
        if (packet_queue_get(&vs->videoq, packet, 1, &vs->quit) < 0) {
            //means we need to quit getting packets
            break;
        }
        pts = 0;
        // global_video_pkt_pts = packet->pts;
        avcodec_decode_video2(videoCodecCtx, frame, &got_frame, packet);
        // if (packet->dts == AV_NOPTS_VALUE &&
        //         frame->opaque && *(uint64_t *)frame->opaque != AV_NOPTS_VALUE) {
        //     pts = *(uint64_t *)frame->opaque;
        // } else if (packet->dts != AV_NOPTS_VALUE) {
        //     pts = pkt->dts;
        // } else {
        //     pts = 0;
        // }
        // pts *= av_q2d(vs->video_stm->time_base);

        if (got_frame) {
            if (queue_picture(vs, frame) < 0) {
                break;
            }
           
        }
        av_free_packet(packet);

    }
    av_free(frame);
    SDL_Event event;
    event.type = FF_QUIT_EVENT;
    event.user.data1 = vs;
    SDL_PushEvent(&event);
    return 0;
}

void stream_component_close(VideoState *vs, int stream_index) {
    AVFormatContext *formatCtx = vs->formatCtx;
    AVCodecContext *codecCtx = NULL;

    if (stream_index < 0 || stream_index >= formatCtx->nb_streams) {
        return;
    }
    codecCtx = formatCtx->streams[stream_index]->codec;
    avcodec_close(codecCtx);
    switch(codecCtx->codec_type) {
        case AVMEDIA_TYPE_AUDIO:
            vs->audioStreamIndex = -1;
            vs->audio_stm = NULL;
            av_free_packet(&vs->audio_pkt);
            swr_close(vs->swr_ctx);
            vs->audio_buf_size = 0;
            vs->audio_buf_index = 0;
            if (NULL != out_buffer) {
                free(out_buffer);
            }
            
            SDL_CloseAudio();
            break;
        case AVMEDIA_TYPE_VIDEO:
            vs->videoStreamIndex = -1;
            vs->video_stm = NULL;
            //sws_close(vs->sws_ctx);
            SDL_WaitThread(vs->video_tid, NULL);
            break;
        default:
            break;
    }
}

int stream_component_open(VideoState *vs, int stream_index) {
    AVFormatContext *formatCtx = vs->formatCtx;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    

    if (stream_index < 0 || stream_index >= formatCtx->nb_streams) {
        return -1;
    }

    codecCtx = formatCtx->streams[stream_index]->codec;

    AVDictionary *optionsDict = NULL;
    codec = avcodec_find_decoder(codecCtx->codec_id);
    if (!codec || (avcodec_open2(codecCtx, codec, &optionsDict) < 0)) {
        fprintf(stderr, "Unsupported codec!\n");
        return -1;
    }

    switch (codecCtx->codec_type) {
        case  AVMEDIA_TYPE_AUDIO:
            vs->audioStreamIndex = stream_index;
            vs->audio_stm = formatCtx->streams[stream_index];
            vs->audio_buf_size = 0;
            vs->audio_buf_index = 0;
            memset(&vs->audio_pkt, 0, sizeof(vs->audio_pkt));
            if (audio_out_linesize == -1) {
                int out_buffer_size = av_samples_get_buffer_size(&audio_out_linesize, codecCtx->channels,codecCtx->frame_size,codecCtx->sample_fmt, 1);
                out_buffer = malloc(out_buffer_size);
                fprintf(stdout, "get samples buffer size:%d outlinesize:%d\n", out_buffer_size, audio_out_linesize);
            }
            vs->swr_ctx = swr_alloc();
            vs->swr_ctx = swr_alloc_set_opts(vs->swr_ctx, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, codecCtx->sample_rate, codecCtx->channel_layout, codecCtx->sample_fmt, codecCtx->sample_rate, 0, NULL);
            swr_init(vs->swr_ctx);
            SDL_AudioSpec wanted_spec, haved_spec;
            SDL_zero(wanted_spec);
            SDL_zero(haved_spec);
            audio_channels = codecCtx->channels;
            wanted_spec.freq = codecCtx->sample_rate;
            wanted_spec.format = AUDIO_S16SYS;
            wanted_spec.channels = codecCtx->channels;
            wanted_spec.silence = 0;
            wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
            wanted_spec.callback = audio_callback;
            wanted_spec.userdata = vs;

            if (SDL_OpenAudio(&wanted_spec, &haved_spec) < 0) {
                fprintf(stderr, "Failed to open audio: %s\n", SDL_GetError());
                return -1;
            } else {
                if (haved_spec.format != wanted_spec.format) {
                    fprintf(stderr, "Can't get Signed16 SYS audio format.\n");
                }
            }

            packet_queue_init(&vs->audioq);

            SDL_PauseAudio(0);

            break;
        case AVMEDIA_TYPE_VIDEO:
            // codecCtx->get_buffer = get_buffer;
            // codecCtx->release_buffer = release_buffer;
            vs->videoStreamIndex = stream_index;
            vs->video_stm = formatCtx->streams[stream_index];
            vs->sws_ctx = sws_getContext(
                codecCtx->width,
                codecCtx->height,
                codecCtx->pix_fmt,
                codecCtx->width,
                codecCtx->height,
                PIX_FMT_YUV420P,
                SWS_BILINEAR,
                NULL,
                NULL,
                NULL
            );
            packet_queue_init(&vs->videoq);
            vs->video_tid = SDL_CreateThread(video_thread, "video_thread", vs);
            break;
        default:
            break;
    }
    return 0;
}


void audio_callback(void *userdata, Uint8 *stream, int len) {
    VideoState *vs = (VideoState *)userdata;
    int len1, audio_decoded_size;

    SDL_memset(stream, 0, len);

    while (len > 0) {
        if (vs->audio_buf_index >= vs->audio_buf_size) {
            audio_decoded_size = audio_decode_frame(vs, vs->audio_buf, sizeof(vs->audio_buf));
            if (audio_decoded_size < 0) {
                vs->audio_buf_size = 1024;
                memset(vs->audio_buf,0,vs->audio_buf_size);
            } else {
                vs->audio_buf_size = audio_decoded_size;
            }
            vs->audio_buf_index = 0;
        }

        len1 = vs->audio_buf_size - vs->audio_buf_index;
        if (len1 > len) {
            len1 = len;
        }
        SDL_MixAudio(stream, (uint8_t *)vs->audio_buf + vs->audio_buf_index, len1, SDL_MIX_MAXVOLUME / 2);
        //memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        vs->audio_buf_index += len1;
    }
}

int audio_decode_frame(VideoState *vs, uint8_t *audio_buf, int buf_size) {
    static AVPacket audio_pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;


    int len1, data_size;

    int decodeFinished = 0;

    if (audioFrame == NULL) {
        audioFrame = av_frame_alloc();
    }

    for(;;) {
        while (audio_pkt_size > 0) {
            len1 = avcodec_decode_audio4(vs->audio_stm->codec, audioFrame, &decodeFinished, &audio_pkt);
            if (len1 < 0) {
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            if (decodeFinished) {
                int out_nb_samples = swr_convert(vs->swr_ctx, &out_buffer, audio_out_linesize, (const uint8_t **)audioFrame->data, audioFrame->nb_samples);
                int out_len = out_nb_samples * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16)*audio_channels;
                memcpy(audio_buf, out_buffer, out_len);
                return out_len;
            }
        }

        if (audio_pkt.data) {
            av_free_packet(&audio_pkt);
        }

        if (vs->quit) {
            return -1;
        }

        if (packet_queue_get(&vs->audioq, &audio_pkt, 1, &vs->quit) < 0) {
            return -1;
        }

        audio_pkt_data = audio_pkt.data;
        audio_pkt_size = audio_pkt.size;
    }
}

void video_display (VideoState *vs) {
    SDL_Rect rect;
    VideoPicture *vp;
    AVPicture pict;
    float aspect_ratio;
    int w, h, x, y;
    int screenW, screenH;
    int i;

    vp = &vs->pict_q[vs->pictq_rindex];
    if (vp->pictYUV) {
        if (vs->video_stm->codec->sample_aspect_ratio.num == 0) {
            aspect_ratio = 0;
        } else {
            aspect_ratio = av_q2d(vs->video_stm->codec->sample_aspect_ratio) *
                vs->video_stm->codec->width / vs->video_stm->codec->height;
        }

        if (aspect_ratio <= 0.0) {
            aspect_ratio = (float)vs->video_stm->codec->width / (float)vs->video_stm->codec->height;
        }

        SDL_GetWindowSize(sdlWindow, &screenW, &screenH);
        h = screenH;
        w = ((int)rint(h * aspect_ratio)) & -3;
        if (w > screenW) {
            w = screenW;
            h = ((int)rint(w / aspect_ratio)) & -3;
        }

        x = (screenW - w) / 2;
        y = (screenH - h) / 2;

        rect.x = x;
        rect.y = y;
        rect.w = w;
        rect.h = h;
        SDL_UpdateYUVTexture(texture, &rect, 
                vp->pictYUV->data[0], vp->pictYUV->linesize[0],
                vp->pictYUV->data[1], vp->pictYUV->linesize[1],
                vp->pictYUV->data[2], vp->pictYUV->linesize[2]);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
}

void video_refresh_timer(void *userdata) {
    VideoState *vs = (VideoState *)userdata;
    VideoPicture *vp = NULL;

    if (vs->video_stm) {
        if(vs->pictq_size == 0) {
            schedule_refresh(vs, 10);
        } else {
            vp = &vs->pict_q[vs->pictq_rindex];
            schedule_refresh(vs, 40);

            video_display(vs);

            if (++vs->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE) {
                vs->pictq_rindex = 0;
            }

            SDL_LockMutex(vs->pictq_mutex);
            vs->pictq_size--;
            SDL_CondSignal(vs->pictq_cond);
            SDL_UnlockMutex(vs->pictq_mutex);
        }
    }
}

static Uint32 sdl_refresh_timer_cb (Uint32 interval, void *opaque) {
    SDL_Event event;
    SDL_zero(event);

    event.type = FF_REFRESH_EVENT;
    event.user.data1 = opaque;
    SDL_PushEvent(&event);
    return 0;
}

static void schedule_refresh(VideoState *vs, int delay) {
    SDL_AddTimer(delay, sdl_refresh_timer_cb, vs);
}

void clearAtExit(void) {
    if (audioFrame) {
        av_free(audioFrame);
    }
}

int decode_interrupt_cb(void *data) {
    return (global_video_state && global_video_state->quit);
}

int allocate_sdlwindow(void *userdata) {
    VideoState *vs = (VideoState *)userdata;

    int ret = -1;
    if (vs->video_stm) {
        sdlWindow = SDL_CreateWindow("sdl-ffmpeg player",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        vs->video_stm->codec->width, vs->video_stm->codec->height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    renderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (NULL == renderer) {
        fprintf(stderr, "SDL_CreateRenderer error:%s\n",SDL_GetError());
        return -1;
    }
    texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_STREAMING,
        vs->video_stm->codec->width, vs->video_stm->codec->height);
    if (NULL == texture) {
        fprintf(stderr, "SDL_CreateTexture error:%s\n",SDL_GetError());
        return -1;
    }
        SDL_CondSignal(sdlWindow_alloc_cond);
        ret = 0;
    }

    return ret;
}

// double synchronize_video(VideoState *vs, AVFrame *src_frame, double pts) {
//     double frame_delay;

//     if (pts != 0) {
//         vs->video_clock = pts;
//     } else {
//         pts = vs->video_clock;
//     }

//     frame_delay = av_q2d(vs->video_stm->codec->time_base);

    
// }
