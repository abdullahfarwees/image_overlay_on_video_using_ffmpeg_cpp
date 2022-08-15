#define _XOPEN_SOURCE 600 /* for usleep */
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "ffmpeg_common.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>

extern "C"
{
  #include <SDL/SDL.h>
}

const char *filter_descr = "movie=//home/afarwees/Desktop/git/lshape/lshape-with_text-removebg.png[wm];[in][wm]overlay=2:2[out]";

static AVFormatContext *fmt_ctx;
static AVCodecContext *dec_ctx;
AVFilterContext *buffersink_ctx;
AVFilterContext *buffersrc_ctx;
AVFilterGraph *filter_graph;
static int video_stream_index = -1;
static int64_t last_pts = AV_NOPTS_VALUE;

static int open_input_file(const char *filename)
{

    std::cout << "open input file\n";
    const AVCodec *dec;
    int ret;

    if ((ret = avformat_open_input(&fmt_ctx, filename, NULL, NULL)) < 0) {
        std::cout << "Cannot open input file\n";
        return ret;
    }
    else
    {
    std::cout << "avformat open input\n";
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
        std::cout << "Cannot find stream information\n";
        return ret;
    }
    else
    {
    std::cout << "avformat find stream info\n";
    }

    /* select the video stream */
    ret = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (ret < 0) {
        std::cout << "Cannot find a video stream in the input file\n";
        return ret;
    }
    else
    {
    std::cout << "avfind best stream\n";
    }
    
    video_stream_index = ret;

    /* create decoding context */
    dec_ctx = avcodec_alloc_context3(dec);
    if (!dec_ctx)
        return AVERROR(ENOMEM);
    avcodec_parameters_to_context(dec_ctx, fmt_ctx->streams[video_stream_index]->codecpar);

    /* init the video decoder */
    if ((ret = avcodec_open2(dec_ctx, dec, NULL)) < 0) {
        std::cout << "Cannot open video decoder\n";
        return ret;
    }
    else
    {
    std::cout << "avcodec_open2\n";
    }

    return 0;
}

static int init_filters(const char *filters_descr)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };

    filter_graph = avfilter_graph_alloc();
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    /* buffer video source: the decoded frames from the decoder will be inserted here. */
    snprintf(args, sizeof(args),
            "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
            time_base.num, time_base.den,
            dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                       args, NULL, filter_graph);
    if (ret < 0) {
        std::cout <<  "Cannot create buffer source\n";
        goto end;
    }
    else
    {
    std::cout << "avfilter_graph_create_filter\n";
    }

    /* buffer video sink: to terminate the filter chain. */
    ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                       NULL, NULL, filter_graph);
    if (ret < 0) {
        std::cout << "Cannot create buffer sink\n";
        goto end;
    }
    else
    {
    std::cout << "avfilter2\n";
    }

    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        std::cout << "Cannot set output pixel format\n";
        goto end;
    }
    else
    {
    std::cout << "av_opt_set_list\n";
    }

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

        std::cout << "point1 \n";
    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
    
    std::cout << "point2 \n";

    if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                    &inputs, &outputs, NULL)) < 0)
        goto end;

    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

        std::cout << " avfilter_inout_free\n";

    return ret;
}


int main(int argc, char **argv)
{
    int ret;
    AVPacket *packet;
    AVFrame *frame;
    AVFrame *filt_frame;
    
    /**/
    
    std::cout << "init ffmpeg AV\n";

    if (argc != 2) {
        fprintf(stderr, "Usage: %s file\n", argv[0]);
        exit(1);
    }
    
    //std::cout <<"reading all packets\n";
    int icount = 0;

    if ((ret = open_input_file(argv[1])) < 0)
    {
    std::cout << "failed to open input file\n";
        return 0;//goto end;
    }
        
    if ((ret = init_filters(filter_descr)) < 0)
    {
    std::cout << "failed to init_filters\n";
        return 0;//goto end;
    }
    
            // SDL init
            
            	SDL_Surface *screen; 
	        SDL_Overlay *bmp; 
	        SDL_Rect rect;
	        if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) 
	        {  
		        printf( "Could not initialize SDL - %s\n", SDL_GetError()); 
		        return -1;
	        } 
	        else
	        {
	        std::cout <<" initialized SDL\n";
	        }
	        
	        int sdl_height = 360;
	        int sdl_width = 640;
	        
	        screen = SDL_SetVideoMode(sdl_width, sdl_height, 0, 0);
	        if(!screen) {  
		        printf("SDL: could not set video mode - exiting\n");  
		        return -1;
	        }
                else
	        {
	        std::cout <<" SDL video mode set\n";
	        }
	        bmp = SDL_CreateYUVOverlay(sdl_width, sdl_height,SDL_YV12_OVERLAY, screen); 

	       // SDL_WM_SetCaption("Simplest FFmpeg Video Filter",NULL);
            
            // endfun SDL

            frame = av_frame_alloc();
            filt_frame = av_frame_alloc();
            packet = av_packet_alloc();
            
            if (!frame || !filt_frame || !packet) 
            {
                fprintf(stderr, "Could not allocate frame or packet\n");
                exit(1);
            }
            else
                {
                std::cout <<" allocated frame and packets\n";
                }
                


    /* read all packets */
    while (1) {
    
    		AVFormatContext *ofmt_ctx;
		AVStream *in_stream, *out_stream;
    
        if ((ret = av_read_frame(fmt_ctx, packet)) < 0)
            break;
            
         
        // write to file 
           
        if(packet->stream_index == video_stream_index)
        {
               // out_stream = of
        }

        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(dec_ctx, packet);
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }
            else
            {
                std::cout <<++icount<<" sent packets to decoder\n" ;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder\n");
                    goto end;
                }
                else
                {
                        //std::cout << "avcodec_receive_frame\n";
                }

                frame->pts = frame->best_effort_timestamp;

                /* push the decoded frame into the filtergraph */
                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
                    break;
                }
                else
                {
                        //std::cout << "av_buffersrc_add_frame_flags\n";
                }

                /* pull filtered frames from the filtergraph */
                while (1) {
                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
                    //////std::cout << "av_buffersin_get_Frame\n";
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                        break;
                    if (ret < 0)
                        goto end;
                   
                   /*
                   
                   Display current frames
                   
                   */
                   if (filt_frame->format==AV_PIX_FMT_YUV420P) 
                   {
                       SDL_LockYUVOverlay(bmp);
                       std::cout << "SDL lockYUV\n";
		        int y_size=sdl_width*sdl_height;
		        memcpy(bmp->pixels[0],filt_frame->data[0],y_size);   //Y
		        memcpy(bmp->pixels[2],filt_frame->data[1],y_size/4); //U
		        memcpy(bmp->pixels[1],filt_frame->data[2],y_size/4); //V 
		        bmp->pitches[0]=filt_frame->linesize[0];
		        bmp->pitches[2]=filt_frame->linesize[1];   
		        bmp->pitches[1]=filt_frame->linesize[2];
		        SDL_UnlockYUVOverlay(bmp); 
		        rect.x = 0;    
		        rect.y = 0;    
		        rect.w = sdl_width;    
		        rect.h = sdl_height;    
		        SDL_DisplayYUVOverlay(bmp, &rect); 
		        //Delay 40ms
		        SDL_Delay(40);
		    }
                   
                   // enddisplay frames
                   
                    av_frame_unref(filt_frame);
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }
    
end:

    avfilter_graph_free(&filter_graph);
    avcodec_free_context(&dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
    av_frame_free(&filt_frame);
    av_packet_free(&packet);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: \n");
        exit(1);
    }

    exit(0);
}
