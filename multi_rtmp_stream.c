/*

 * multi_rtmp_stream.c

 *

 * Lightweight multi-destination RTMP relay using FFmpeg libraries.

 *

 * The application reads a single input stream and remuxes it to multiple

 * RTMP destinations simultaneously.

 *

 * No transcoding is performed.

 *

 * Typical use case:

 *

 *                         OBS Studio

 *                             |

 *                             | RTMP

 *                             v

 *                    multi_rtmp_stream

 *                     /       |       \

 *                    /        |        \

 *                   v         v         v

 *               YouTube    Twitch    Other RTMP

 *

 * Build requirements:

 *

 *   apt install \

 *       gcc \

 *       pkg-config \

 *       libavformat-dev \

 *       libavcodec-dev \

 *       libavutil-dev

 *

 * Build:

 *

 *   gcc multi_rtmp_stream.c -o multi_rtmp_stream \

 *       $(pkg-config --cflags --libs \

 *           libavformat \

 *           libavcodec \

 *           libavutil)

 *

 * Usage:

 *

 *   ./multi_rtmp_stream [-listen] \

 *       <input> \

 *       <rtmp_output_1> \

 *       <rtmp_output_2> \

 *       [more outputs...]

 *

 * Example using a file:

 *

 *   ./multi_rtmp_stream input.mp4 \

 *       rtmp://a.rtmp.youtube.com/live2/YOUTUBE_KEY \

 *       rtmp://live.twitch.tv/app/TWITCH_KEY

 *

 * Example acting as an RTMP server for OBS:

 *

 *   ./multi_rtmp_stream -listen \

 *       rtmp://0.0.0.0:1935/live/obs \

 *       rtmp://a.rtmp.youtube.com/live2/YOUTUBE_KEY \

 *       rtmp://live.twitch.tv/app/TWITCH_KEY

 *

 * OBS configuration:

 *

 *   Server:

 *       rtmp://<relay-ip>:1935/live

 *

 *   Stream Key:

 *       obs

 *

 * Notes:

 *

 * - The program performs remuxing only. It does not decode or re-encode

 *   audio or video.

 *

 * - All output destinations receive the exact same encoded stream.

 *

 * - H.264 video and AAC audio are recommended because they are widely

 *   supported by RTMP streaming platforms.

 *

 * - If an output connection fails, the program keeps the remaining

 *   destinations active and periodically attempts to reconnect the

 *   failed output.

 *

 * - In -listen mode, if OBS disconnects, the program returns to listening

 *   for a new incoming connection.

 *

 * - Press Ctrl+C for a clean shutdown.

 *

 * - This version is intentionally RTMP-only.

 */



#include <signal.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <time.h>

#include <unistd.h>



#include <libavformat/avformat.h>

#include <libavutil/timestamp.h>



#define MAX_OUTPUTS 16

#define OUTPUT_RETRY_INTERVAL_SECONDS 5



typedef struct

{

    AVFormatContext *fmt_ctx;

    const char *url;

    int *stream_map;

    int active;

    time_t next_retry;

} OutputTarget;



static volatile sig_atomic_t g_should_exit = 0;



static void handle_signal(int sig)

{

    (void)sig;

    g_should_exit = 1;

}



static int interrupt_cb(void *opaque)

{

    (void)opaque;

    return g_should_exit;

}



static void log_error(const char *message, int error_number)

{

    char error_buffer[AV_ERROR_MAX_STRING_SIZE] = {0};



    av_strerror(

        error_number,

        error_buffer,

        sizeof(error_buffer)

    );



    fprintf(

        stderr,

        "%s: %s\n",

        message,

        error_buffer

    );

}



static int open_input(

    const char *input_url,

    int listen_mode,

    AVFormatContext **output_context

)

{

    AVFormatContext *context = avformat_alloc_context();



    if (!context)

        return AVERROR(ENOMEM);



    context->interrupt_callback.callback = interrupt_cb;

    context->interrupt_callback.opaque = NULL;



    int result;



    if (listen_mode)

    {

        AVDictionary *input_options = NULL;



        av_dict_set(&input_options, "listen", "1", 0);



        printf(

            "Waiting for incoming RTMP connection on %s ...\n",

            input_url

        );



        result = avformat_open_input(

            &context,

            input_url,

            NULL,

            &input_options

        );



        av_dict_free(&input_options);

    }

    else

    {

        result = avformat_open_input(

            &context,

            input_url,

            NULL,

            NULL

        );

    }



    if (result < 0)

    {

        avformat_free_context(context);

        return result;

    }



    result = avformat_find_stream_info(context, NULL);



    if (result < 0)

    {

        avformat_close_input(&context);

        return result;

    }



    *output_context = context;

    return 0;

}



static int build_stream_map(

    AVFormatContext *input_context,

    OutputTarget *output

)

{

    av_freep(&output->stream_map);



    output->stream_map = av_calloc(

        input_context->nb_streams,

        sizeof(int)

    );



    if (!output->stream_map)

        return AVERROR(ENOMEM);



    unsigned int output_index = 0;



    for (

        unsigned int input_index = 0;

        input_index < input_context->nb_streams;

        input_index++

    )

    {

        enum AVMediaType media_type =

            input_context->streams[input_index]->codecpar->codec_type;



        if (

            media_type != AVMEDIA_TYPE_VIDEO &&

            media_type != AVMEDIA_TYPE_AUDIO

        )

        {

            output->stream_map[input_index] = -1;

            continue;

        }



        if (

            output_index >=

            (unsigned int)output->fmt_ctx->nb_streams

        )

        {

            output->stream_map[input_index] = -1;

            continue;

        }



        output->stream_map[input_index] = (int)output_index;

        output_index++;

    }



    return 0;

}



static void teardown_output(

    OutputTarget *output

)

{

    if (output->fmt_ctx)

    {

        if (

            !(output->fmt_ctx->oformat->flags & AVFMT_NOFILE) &&

            output->fmt_ctx->pb

        )

        {

            avio_closep(&output->fmt_ctx->pb);

        }



        avformat_free_context(output->fmt_ctx);

        output->fmt_ctx = NULL;

    }



    output->active = 0;

}



static int open_one_output(

    AVFormatContext *input_context,

    OutputTarget *output

)

{

    const char *output_url = output->url;

    int result;



    teardown_output(output);



    result = avformat_alloc_output_context2(

        &output->fmt_ctx,

        NULL,

        "flv",

        output_url

    );



    if (result < 0 || !output->fmt_ctx)

    {

        log_error(

            "Could not create output context",

            result

        );



        fprintf(

            stderr,

            "Skipping endpoint: %s\n",

            output_url

        );



        return result < 0 ? result : AVERROR_UNKNOWN;

    }



    output->fmt_ctx->interrupt_callback.callback = interrupt_cb;

    output->fmt_ctx->interrupt_callback.opaque = NULL;



    for (

        unsigned int input_index = 0;

        input_index < input_context->nb_streams;

        input_index++

    )

    {

        AVStream *input_stream =

            input_context->streams[input_index];



        enum AVMediaType media_type =

            input_stream->codecpar->codec_type;



        if (

            media_type != AVMEDIA_TYPE_VIDEO &&

            media_type != AVMEDIA_TYPE_AUDIO

        )

        {

            continue;

        }



        AVStream *output_stream =

            avformat_new_stream(

                output->fmt_ctx,

                NULL

            );



        if (!output_stream)

        {

            fprintf(

                stderr,

                "Could not allocate output stream.\n"

            );



            fprintf(

                stderr,

                "Skipping endpoint: %s\n",

                output_url

            );



            teardown_output(output);

            return AVERROR(ENOMEM);

        }



        result = avcodec_parameters_copy(

            output_stream->codecpar,

            input_stream->codecpar

        );



        if (result < 0)

        {

            log_error(

                "Could not copy codec parameters",

                result

            );



            fprintf(

                stderr,

                "Skipping endpoint: %s\n",

                output_url

            );



            teardown_output(output);

            return result;

        }



        output_stream->codecpar->codec_tag = 0;

    }



    if (

        !(output->fmt_ctx->oformat->flags & AVFMT_NOFILE)

    )

    {

        result = avio_open2(

            &output->fmt_ctx->pb,

            output_url,

            AVIO_FLAG_WRITE,

            &output->fmt_ctx->interrupt_callback,

            NULL

        );



        if (result < 0)

        {

            log_error(

                "Could not open output URL",

                result

            );



            fprintf(

                stderr,

                "Skipping endpoint: %s\n",

                output_url

            );



            teardown_output(output);

            return result;

        }

    }



    result = avformat_write_header(

        output->fmt_ctx,

        NULL

    );



    if (result < 0)

    {

        log_error(

            "Could not write output header",

            result

        );



        fprintf(

            stderr,

            "Skipping endpoint: %s\n",

            output_url

        );



        teardown_output(output);

        return result;

    }



    result = build_stream_map(

        input_context,

        output

    );



    if (result < 0)

    {

        fprintf(

            stderr,

            "Could not allocate stream map for %s\n",

            output_url

        );



        teardown_output(output);

        return result;

    }



    output->active = 1;



    printf(

        "Output connected: %s\n",

        output_url

    );



    return 0;

}



static int setup_outputs(

    AVFormatContext *input_context,

    OutputTarget *outputs,

    int output_count

)

{

    int active_outputs = 0;



    for (int i = 0; i < output_count; i++)

    {

        if (

            open_one_output(

                input_context,

                &outputs[i]

            ) == 0

        )

        {

            active_outputs++;

        }

    }



    return active_outputs;

}



static void relay_loop(

    AVFormatContext *input_context,

    OutputTarget *outputs,

    int output_count

)

{

    AVPacket *packet = av_packet_alloc();



    if (!packet)

    {

        fprintf(

            stderr,

            "Could not allocate packet.\n"

        );

        return;

    }



    while (!g_should_exit)

    {

        time_t current_time = time(NULL);



        for (int i = 0; i < output_count; i++)

        {

            if (

                !outputs[i].active &&

                current_time >= outputs[i].next_retry

            )

            {

                printf(

                    "Attempting to reconnect endpoint: %s\n",

                    outputs[i].url

                );



                if (

                    open_one_output(

                        input_context,

                        &outputs[i]

                    ) < 0

                )

                {

                    outputs[i].next_retry =

                        current_time +

                        OUTPUT_RETRY_INTERVAL_SECONDS;

                }

            }

        }



        int result = av_read_frame(

            input_context,

            packet

        );



        if (result < 0)

            break;



        unsigned int input_index =

            (unsigned int)packet->stream_index;



        AVStream *input_stream =

            input_context->streams[input_index];



        for (int i = 0; i < output_count; i++)

        {

            if (!outputs[i].active)

                continue;



            int mapped_index =

                outputs[i].stream_map[input_index];



            if (mapped_index < 0)

                continue;



            AVPacket *output_packet =

                av_packet_clone(packet);



            if (!output_packet)

            {

                fprintf(

                    stderr,

                    "Could not clone packet.\n"

                );

                continue;

            }



            AVStream *output_stream =

                outputs[i].fmt_ctx->streams[mapped_index];



            output_packet->stream_index = mapped_index;



            av_packet_rescale_ts(

                output_packet,

                input_stream->time_base,

                output_stream->time_base

            );



            output_packet->pos = -1;



            int write_result =

                av_interleaved_write_frame(

                    outputs[i].fmt_ctx,

                    output_packet

                );



            av_packet_free(&output_packet);



            if (write_result < 0)

            {

                log_error(

                    "Error writing packet to output",

                    write_result

                );



                fprintf(

                    stderr,

                    "Endpoint disconnected: %s\n",

                    outputs[i].url

                );



                fprintf(

                    stderr,

                    "Scheduling reconnect attempt.\n"

                );



                teardown_output(&outputs[i]);



                outputs[i].next_retry =

                    time(NULL) +

                    OUTPUT_RETRY_INTERVAL_SECONDS;

            }

        }



        av_packet_unref(packet);

    }



    av_packet_free(&packet);

}



int main(

    int argc,

    char **argv

)

{

    int listen_mode = 0;

    int argument_index = 1;



    while (argument_index < argc)

    {

        if (

            strcmp(

                argv[argument_index],

                "-listen"

            ) == 0

        )

        {

            listen_mode = 1;

            argument_index++;

        }

        else

        {

            break;

        }

    }



    if (argc - argument_index < 3)

    {

        fprintf(

            stderr,

            "Usage:\n"

            "  %s [-listen] <input> "

            "<rtmp_output_1> <rtmp_output_2> "

            "[more outputs...]\n\n"

            "Options:\n"

            "  -listen    Act as an RTMP server.\n"

            "             The input becomes the local address that\n"

            "             OBS or another encoder connects to.\n\n"

            "Example:\n"

            "  %s -listen "

            "rtmp://0.0.0.0:1935/live/obs "

            "rtmp://server1/app/key "

            "rtmp://server2/app/key\n",

            argv[0],

            argv[0]

        );



        return 1;

    }



    const char *input_url =

        argv[argument_index];



    int output_count =

        argc -

        argument_index -

        1;



    if (output_count > MAX_OUTPUTS)

    {

        fprintf(

            stderr,

            "Too many outputs. Maximum supported: %d\n",

            MAX_OUTPUTS

        );



        return 1;

    }



    avformat_network_init();



    signal(SIGINT, handle_signal);

    signal(SIGTERM, handle_signal);



    OutputTarget outputs[MAX_OUTPUTS] = {0};



    for (int i = 0; i < output_count; i++)

    {

        outputs[i].url =

            argv[

                argument_index +

                1 +

                i

            ];

    }



    int outputs_initialized = 0;

    int exit_code = 0;



    while (!g_should_exit)

    {

        AVFormatContext *input_context = NULL;



        int result = open_input(

            input_url,

            listen_mode,

            &input_context

        );



        if (result < 0)

        {

            if (g_should_exit)

                break;



            log_error(

                "Could not open input",

                result

            );



            if (!listen_mode)

            {

                exit_code = 1;

                break;

            }



            sleep(1);

            continue;

        }



        av_dump_format(

            input_context,

            0,

            input_url,

            0

        );



        if (!outputs_initialized)

        {

            int active_outputs =

                setup_outputs(

                    input_context,

                    outputs,

                    output_count

                );



            if (active_outputs == 0)

            {

                fprintf(

                    stderr,

                    "No output endpoints are available. Aborting.\n"

                );



                avformat_close_input(

                    &input_context

                );



                exit_code = 1;

                break;

            }



            outputs_initialized = 1;

        }

        else

        {

            for (int i = 0; i < output_count; i++)

            {

                if (outputs[i].active)

                {

                    build_stream_map(

                        input_context,

                        &outputs[i]

                    );

                }

            }



            printf(

                "Input source reconnected. Resuming relay.\n"

            );

        }



        relay_loop(

            input_context,

            outputs,

            output_count

        );



        avformat_close_input(

            &input_context

        );



        if (g_should_exit)

            break;



        if (!listen_mode)

            break;



        printf(

            "Input connection lost. Waiting for a new connection...\n"

        );

    }



    printf(

        "Closing outputs...\n"

    );



    for (int i = 0; i < output_count; i++)

    {

        if (

            outputs[i].active &&

            outputs[i].fmt_ctx

        )

        {

            av_write_trailer(

                outputs[i].fmt_ctx

            );

        }



        if (outputs[i].fmt_ctx)

        {

            if (

                !(outputs[i].fmt_ctx->oformat->flags & AVFMT_NOFILE) &&

                outputs[i].fmt_ctx->pb

            )

            {

                avio_closep(

                    &outputs[i].fmt_ctx->pb

                );

            }



            avformat_free_context(

                outputs[i].fmt_ctx

            );



            outputs[i].fmt_ctx = NULL;

        }



        av_free(

            outputs[i].stream_map

        );



        outputs[i].stream_map = NULL;

    }



    avformat_network_deinit();



    printf(

        "Shutdown complete.\n"

    );



    return exit_code;

}
