#include "videoutils.h"

void packet_queue_init(PacketQueue *queue) {
    memset(queue, 0, sizeof(PacketQueue));
    queue->mutex = SDL_CreateMutex();
    queue->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *queue, AVPacket *pkt) {
    if (NULL == pkt) {
        return -1;
    }

    if (av_dup_packet(pkt) < 0) {
        return -1;
    }

    AVPacketList *pktl = av_malloc(sizeof(AVPacketList));
    if (NULL == pktl) {
        return -1;
    }
    pktl->pkt = *pkt;
    pktl->next = NULL;

    SDL_LockMutex(queue->mutex);

    if (!queue->first_pkt) { // queue is empty
        queue->first_pkt = pktl;
    } else { // queue is not empty
        queue->last_pkt->next = pktl;
    }
    queue->last_pkt = pktl;
    queue->nb_packets++;
    queue->size += pktl->pkt.size;
    SDL_CondSignal(queue->cond);

    SDL_UnlockMutex(queue->mutex);
    return 0;
}


int packet_queue_get(PacketQueue *queue, AVPacket *pkt, int block, int *quit) {
    AVPacketList *pktl;
    int ret = 0;

    SDL_LockMutex(queue->mutex);

    for(;;) {
        if ((*quit) != 0) {
            ret = -1;
            break;
        }

        pktl = queue->first_pkt;
        if (pktl) {
            queue->first_pkt = pktl->next;
            if (!queue->first_pkt) {
                queue->last_pkt = NULL;
            }
            queue->nb_packets--;
            queue->size -= pktl->pkt.size;
            *pkt = pktl->pkt;
            av_free(pktl);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            SDL_CondWait(queue->cond, queue->mutex);
        }
    }
    SDL_UnlockMutex(queue->mutex);
    return ret;
}


void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame) {
    FILE *pFile;

    char szFileName[32];

    int y;

    sprintf(szFileName, "frame%d.ppm", iFrame);

    pFile = fopen(szFileName, "wb");
    if (NULL == pFile) {
        return;
    }

    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    for (y = 0; y < height; y++) {
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);
    }

    fclose(pFile);
}

