#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <stdbool.h>

typedef struct {
    uint32_t nBytes;
    uint32_t flags;
} rfbH264Header;

#define sz_rfbH264Header 8

AVCodecContext *avctx;
AVCodecParserContext *parser;
AVFrame* frame;
struct SwsContext* sws;
uint8_t* h264WorkBuffer;
uint32_t h264WorkBufferLength;

uint32_t fbLen = 0;
uint32_t pw, ph;

bool initialized = false;

bool H264Init(int w, int h)
{
  sws = NULL;
  h264WorkBuffer = NULL;
  h264WorkBufferLength = 0;
  pw = w;
  ph = h;

  const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec)
  {
    rfbClientLog("%s: H264 codec not found\n", __FUNCTION__);
    return false;
  }

  parser = av_parser_init(codec->id);
  if (!parser)
  {
    rfbClientLog("%s: could not create H264 parser\n", __FUNCTION__);
    return false;
  }

  avctx = avcodec_alloc_context3(codec);
  if (!avctx)
  {
    av_parser_close(parser);
    rfbClientLog("%s: could not allocate video codec context\n", __FUNCTION__);
    return false;
  }

  frame = av_frame_alloc();
  if (!frame)
  {
    av_parser_close(parser);
    avcodec_free_context(&avctx);
    rfbClientLog("%s: could not allocate video frame\n", __FUNCTION__);
    return false;
  }

  if (avcodec_open2(avctx, codec, NULL) < 0)
  {
    av_parser_close(parser);
    avcodec_free_context(&avctx);
    av_frame_free(&frame);
    rfbClientLog("%s: could not open H264 codec\n", __FUNCTION__);
    return false;
  }

  fbLen = av_image_get_buffer_size(AV_PIX_FMT_RGB32, w, h, 1);

  initialized = true;
  return true;

}


uint8_t* H264MakeWorkBuffer(uint8_t* buffer, uint32_t len)
{
  uint32_t reserve_len = len + len % AV_INPUT_BUFFER_PADDING_SIZE;

  if (!h264WorkBuffer || reserve_len > h264WorkBufferLength)
  {
    h264WorkBuffer = (uint8_t*)realloc(h264WorkBuffer, reserve_len);
    if (h264WorkBuffer == NULL) {
       rfbClientLog("%s: unable to allocate memory\n", __FUNCTION__);
       return NULL;
    }
    h264WorkBufferLength = reserve_len;
  }

  memcpy(h264WorkBuffer, buffer, len);
  memset(h264WorkBuffer + len, 0, h264WorkBufferLength - len);
  return h264WorkBuffer;
}


void H264Decode(rfbClient* client, uint8_t* h264_in_buffer, uint32_t len, uint32_t flags) {
  if (!initialized)
    return;

  uint8_t* h264_work_buffer = H264MakeWorkBuffer(h264_in_buffer, len);

  AVPacket *packet = av_packet_alloc();

  int ret;
  int frames_received = 0;
  while (len)
  {
    ret = av_parser_parse2(parser, avctx, &packet->data, &packet->size, h264_work_buffer, len, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (ret < 0)
    {
      rfbClientLog("%s: error while parsing\n", __FUNCTION__);
      break;
    }

    if (!packet->size && len == ret)
      ret = av_parser_parse2(parser, avctx, &packet->data, &packet->size, h264_work_buffer, len, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    if (ret < 0)
    {
      rfbClientLog("%s: error while parsing\n", __FUNCTION__);
      break;
    }
    h264_work_buffer += ret;
    len -= ret;

    if (!ret)
    {
      packet->size = len;
      packet->data = h264_work_buffer;
      len = 0;
    }

    if (!packet->size)
      continue;

    ret = avcodec_send_packet(avctx, packet);
    if (ret < 0)
    {
      rfbClientLog("%s: error sending a packet to decoding\n", __FUNCTION__);
      break;
    }

    ret = avcodec_receive_frame(avctx, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      break;
    else if (ret < 0)
    {
      rfbClientLog("%s: error during decoding\n", __FUNCTION__);
      break;
    }
    frames_received++;
  }

  packet->size = 0;
  packet->data = NULL;
  av_packet_free(&packet);

  if (!frames_received)
    return;

  if (!frame->height)
    return;

  sws = sws_getCachedContext(sws,
                             frame->width, frame->height, avctx->pix_fmt,
                             frame->width, frame->height, AV_PIX_FMT_RGB32,
                             0, NULL, NULL, NULL);

  int dst_linesize = 4 * avctx->width;
  sws_scale(sws, (const uint8_t * const *)frame->data, frame->linesize, 0, frame->height, &client->frameBuffer, &dst_linesize);
}


void H264Free() {
  if (!initialized)
    return;
  av_parser_close(parser);
  avcodec_free_context(&avctx);
  av_frame_free(&frame);
  free(h264WorkBuffer);
  initialized = false;
}


static rfbBool HandleH264(rfbClient* client, int rx, int ry, int rw, int rh)
{
    rfbH264Header hdr;
    char *framedata;

    if(initialized && (rw != pw || rh != ph)) {
#ifdef DEBUG
        rfbClientLog("%s: screen resize to: %dx%d\n", __FUNCTION__, rw, rh);
#endif
        H264Free();
    }

    if(!initialized) {
        if(!H264Init(rw,rh)) {
            rfbClientLog("%s: unable to initialize H264 codec\n", __FUNCTION__);
            return false;
        }
        client->screen.width = rw;
        client->screen.height = rh;
    }

    if (!ReadFromRFBServer(client, (char *)&hdr, sz_rfbH264Header))
        return false;

    hdr.nBytes = rfbClientSwap32IfLE(hdr.nBytes);
    hdr.flags = rfbClientSwap32IfLE(hdr.flags);

    framedata = (char*) malloc(hdr.nBytes);

    if (!ReadFromRFBServer(client, framedata, hdr.nBytes))
        return false;

    H264Decode(client, (uint8_t *)framedata, hdr.nBytes, 0);

    free(framedata);

    return true;
}