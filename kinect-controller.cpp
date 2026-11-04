#include <libfreenect/libfreenect.h>
#include <opencv2/opencv.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


// ============================================================
// CONFIG
// ============================================================

constexpr int WIDTH  = 640;
constexpr int HEIGHT = 480;
constexpr int FPS    = 30;

// MUDE AQUI PARA O IP DO MAC
const std::string SRT_URL =
    "srt://192.168.1.4:9494?mode=caller&pkt_size=1316";


// ============================================================
// KINECT
// ============================================================

freenect_context* f_ctx = nullptr;
freenect_device*  f_dev = nullptr;

std::atomic<bool> running{true};

// 0 RGB
// 1 IR
// 2 Depth
std::atomic<int> requested_view{0};

std::atomic<int> requested_ir_brightness{25};
std::atomic<int> requested_tilt{0};

// stream de vídeo atual:
// 0 = RGB
// 1 = IR
std::atomic<int> active_video_format{0};

std::mutex frame_mutex;

cv::Mat latest_rgb;
cv::Mat latest_ir;
cv::Mat latest_depth;

// RGB precisa de 3 bytes por pixel.
// IR usa menos, mas pode reutilizar este buffer.
std::vector<uint8_t> video_buffer(WIDTH * HEIGHT * 3);


// ============================================================
// FFMPEG STREAMER
//
// Roda FFmpeg como processo filho.
// O OpenCV entrega frames BGR crus pelo stdin.
// ============================================================

class FFmpegStreamer
{
public:

    bool start(const std::string& url)
    {
        stop();

        int pipefd[2];

        if (pipe(pipefd) == -1)
        {
            std::cerr << "Erro criando pipe do FFmpeg: "
                      << strerror(errno) << "\n";
            return false;
        }

        pid_ = fork();

        if (pid_ == -1)
        {
            std::cerr << "Erro fazendo fork do FFmpeg\n";

            close(pipefd[0]);
            close(pipefd[1]);

            return false;
        }


        // ----------------------------------------------------
        // CHILD
        // ----------------------------------------------------

        if (pid_ == 0)
        {
            close(pipefd[1]);

            // stdin do ffmpeg recebe nosso pipe
            dup2(pipefd[0], STDIN_FILENO);

            close(pipefd[0]);


            execlp(
                "ffmpeg",
                "ffmpeg",

                "-hide_banner",
                "-loglevel", "warning",

                // entrada raw do OpenCV
                "-f", "rawvideo",
                "-pixel_format", "bgr24",
                "-video_size", "640x480",
                "-framerate", "30",
                "-i", "pipe:0",

                "-an",

                // baixa latência
                "-c:v", "libx264",
                "-preset", "ultrafast",
                "-tune", "zerolatency",

                // sem B frames
                "-bf", "0",

                // keyframe a cada ~1s
                "-g", "30",

                "-pix_fmt", "yuv420p",

                // SRT normalmente leva MPEG-TS
                "-f", "mpegts",

                url.c_str(),

                (char*) nullptr
            );


            // Só chega aqui se execlp falhar
            std::cerr << "Nao consegui iniciar ffmpeg\n";

            _exit(1);
        }


        // ----------------------------------------------------
        // PARENT
        // ----------------------------------------------------

        close(pipefd[0]);

        write_fd_ = pipefd[1];

        active_ = true;

        worker_ = std::thread(
            &FFmpegStreamer::workerLoop,
            this
        );

        std::cout
            << "\nSTREAM START\n"
            << url
            << "\n\n";

        return true;
    }


    void pushFrame(const cv::Mat& frame)
    {
        if (!active_ || frame.empty())
            return;

        cv::Mat bgr;

        if (frame.channels() == 1)
        {
            cv::cvtColor(
                frame,
                bgr,
                cv::COLOR_GRAY2BGR
            );
        }
        else
        {
            bgr = frame;
        }


        if (
            bgr.cols != WIDTH ||
            bgr.rows != HEIGHT
        )
        {
            cv::resize(
                bgr,
                bgr,
                cv::Size(WIDTH, HEIGHT)
            );
        }


        if (!bgr.isContinuous())
            bgr = bgr.clone();


        {
            std::lock_guard<std::mutex> lock(frame_mutex_);

            // Mantemos apenas o frame mais recente.
            // Não deixa criar uma fila infinita.
            latest_frame_ = bgr.clone();

            frame_available_ = true;
        }

        condition_.notify_one();
    }


    void stop()
    {
        bool had_process =
            pid_ > 0 ||
            write_fd_ >= 0 ||
            worker_.joinable();

        active_ = false;

        condition_.notify_all();


        // mata o ffmpeg caso ele esteja parado
        // esperando conexão SRT
        if (pid_ > 0)
        {
            kill(pid_, SIGTERM);
        }


        if (write_fd_ >= 0)
        {
            close(write_fd_);
            write_fd_ = -1;
        }


        if (worker_.joinable())
        {
            worker_.join();
        }


        if (pid_ > 0)
        {
            int status = 0;

            waitpid(
                pid_,
                &status,
                0
            );

            pid_ = -1;
        }


        {
            std::lock_guard<std::mutex> lock(frame_mutex_);

            latest_frame_.release();
            frame_available_ = false;
        }


        if (had_process)
            std::cout << "\nSTREAM STOP\n\n";
    }


    bool isActive() const
    {
        return active_;
    }


    ~FFmpegStreamer()
    {
        stop();
    }


private:

    void workerLoop()
    {
        while (active_)
        {
            cv::Mat frame;


            {
                std::unique_lock<std::mutex> lock(frame_mutex_);

                condition_.wait(
                    lock,
                    [&]()
                    {
                        return
                            frame_available_ ||
                            !active_;
                    }
                );


                if (!active_)
                    break;


                frame = latest_frame_.clone();

                frame_available_ = false;
            }


            if (frame.empty())
                continue;


            const uint8_t* data = frame.ptr<uint8_t>();

            size_t bytes =
                frame.total() *
                frame.elemSize();

            size_t written = 0;


            while (
                written < bytes &&
                active_
            )
            {
                ssize_t result =
                    write(
                        write_fd_,
                        data + written,
                        bytes - written
                    );


                if (result < 0)
                {
                    if (errno == EINTR)
                        continue;

                    std::cerr
                        << "FFmpeg stream desconectado: "
                        << strerror(errno)
                        << "\n";

                    active_ = false;

                    break;
                }


                written += result;
            }
        }
    }


    std::atomic<bool> active_{false};

    pid_t pid_ = -1;

    int write_fd_ = -1;

    std::thread worker_;

    std::mutex frame_mutex_;

    std::condition_variable condition_;

    cv::Mat latest_frame_;

    bool frame_available_ = false;
};


FFmpegStreamer streamer;


// ============================================================
// KINECT CALLBACKS
// ============================================================

void video_cb(
    freenect_device*,
    void* data,
    uint32_t
)
{
    std::lock_guard<std::mutex> lock(frame_mutex);


    if (active_video_format == 0)
    {
        // Kinect entrega RGB
        // OpenCV trabalha em BGR

        cv::Mat rgb(
            HEIGHT,
            WIDTH,
            CV_8UC3,
            data
        );

        cv::cvtColor(
            rgb,
            latest_rgb,
            cv::COLOR_RGB2BGR
        );
    }
    else
    {
        cv::Mat ir(
            HEIGHT,
            WIDTH,
            CV_8UC1,
            data
        );

        latest_ir = ir.clone();
    }
}


void depth_cb(
    freenect_device*,
    void* data,
    uint32_t
)
{
    std::lock_guard<std::mutex> lock(frame_mutex);

    cv::Mat depth(
        HEIGHT,
        WIDTH,
        CV_16UC1,
        data
    );

    latest_depth = depth.clone();
}


// ============================================================
// LIBFREENECT THREAD
// ============================================================

void freenect_loop()
{
    int current_video = 0;

    int last_ir_brightness = -1;
    int last_tilt = 999;


    freenect_set_depth_callback(
        f_dev,
        depth_cb
    );

    freenect_set_video_callback(
        f_dev,
        video_cb
    );


    // Depth em milímetros
    freenect_set_depth_mode(
        f_dev,
        freenect_find_depth_mode(
            FREENECT_RESOLUTION_MEDIUM,
            FREENECT_DEPTH_MM
        )
    );


    // começa em RGB
    freenect_set_video_mode(
        f_dev,
        freenect_find_video_mode(
            FREENECT_RESOLUTION_MEDIUM,
            FREENECT_VIDEO_RGB
        )
    );

    active_video_format = 0;


    freenect_set_video_buffer(
        f_dev,
        video_buffer.data()
    );


    freenect_start_depth(f_dev);
    freenect_start_video(f_dev);


    while (running)
    {
        if (
            freenect_process_events(f_ctx)
            < 0
        )
        {
            break;
        }


        int view =
            requested_view.load();


        // ====================================================
        // RGB
        // ====================================================

        if (
            view == 0 &&
            current_video != 0
        )
        {
            freenect_stop_video(f_dev);


            freenect_set_video_mode(
                f_dev,
                freenect_find_video_mode(
                    FREENECT_RESOLUTION_MEDIUM,
                    FREENECT_VIDEO_RGB
                )
            );


            active_video_format = 0;


            freenect_set_video_buffer(
                f_dev,
                video_buffer.data()
            );


            freenect_start_video(f_dev);

            current_video = 0;
        }


        // ====================================================
        // IR
        // ====================================================

        if (
            view == 1 &&
            current_video != 1
        )
        {
            freenect_stop_video(f_dev);


            freenect_set_video_mode(
                f_dev,
                freenect_find_video_mode(
                    FREENECT_RESOLUTION_MEDIUM,
                    FREENECT_VIDEO_IR_8BIT
                )
            );


            active_video_format = 1;


            freenect_set_video_buffer(
                f_dev,
                video_buffer.data()
            );


            freenect_start_video(f_dev);

            current_video = 1;
        }


        // ====================================================
        // IR sensor brightness
        // ====================================================

        int ir =
            requested_ir_brightness.load();


        if (ir != last_ir_brightness)
        {
            ir =
                std::clamp(
                    ir,
                    1,
                    50
                );


            freenect_set_ir_brightness(
                f_dev,
                ir
            );


            last_ir_brightness = ir;
        }


        // ====================================================
        // TILT
        // ====================================================

        int tilt =
            requested_tilt.load();


        if (tilt != last_tilt)
        {
            tilt =
                std::clamp(
                    tilt,
                    -30,
                    30
                );


            freenect_set_tilt_degs(
                f_dev,
                tilt
            );


            last_tilt = tilt;
        }
    }


    freenect_stop_depth(f_dev);
    freenect_stop_video(f_dev);
}


// ============================================================
// DEPTH -> 8 BIT
// ============================================================

cv::Mat depth_to_image(
    const cv::Mat& depth
)
{
    cv::Mat f;

    depth.convertTo(
        f,
        CV_32F
    );


    // faixa inicial apenas para visualização
    constexpr float near_mm = 500.0f;
    constexpr float far_mm  = 4500.0f;


    // perto = branco
    // longe = preto

    f =
        (far_mm - f) *
        (
            255.0f /
            (far_mm - near_mm)
        );


    cv::threshold(
        f,
        f,
        255,
        255,
        cv::THRESH_TRUNC
    );


    cv::threshold(
        f,
        f,
        0,
        0,
        cv::THRESH_TOZERO
    );


    cv::Mat output;

    f.convertTo(
        output,
        CV_8U
    );


    // Kinect usa zero para depth inválido
    output.setTo(
        0,
        depth == 0
    );


    return output;
}


// ============================================================
// PROCESSAMENTO
// ============================================================

cv::Mat process_image(
    const cv::Mat& input,
    int brightness_slider,
    int contrast_slider,
    int gamma_slider,
    int threshold_slider
)
{
    if (input.empty())
        return {};


    cv::Mat output;


    double brightness =
        brightness_slider - 100;


    double contrast =
        contrast_slider / 100.0;


    double gamma =
        std::max(
            0.1,
            gamma_slider / 100.0
        );


    // ========================================================
    // BRIGHTNESS + CONTRAST
    // ========================================================

    input.convertTo(
        output,
        -1,
        contrast,
        brightness
    );


    // ========================================================
    // GAMMA LUT
    // ========================================================

    cv::Mat lut(
        1,
        256,
        CV_8U
    );


    for (int i = 0; i < 256; i++)
    {
        double x =
            i / 255.0;


        lut.at<uint8_t>(i) =
            cv::saturate_cast<uint8_t>(
                std::pow(
                    x,
                    gamma
                )
                * 255.0
            );
    }


    cv::LUT(
        output,
        lut,
        output
    );


    // ========================================================
    // THRESHOLD
    // ========================================================

    if (threshold_slider > 0)
    {
        if (output.channels() == 3)
        {
            cv::cvtColor(
                output,
                output,
                cv::COLOR_BGR2GRAY
            );
        }


        cv::threshold(
            output,
            output,
            threshold_slider,
            255,
            cv::THRESH_BINARY
        );
    }


    return output;
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    // evita o programa inteiro morrer se FFmpeg fechar o pipe
    signal(
        SIGPIPE,
        SIG_IGN
    );


    // ========================================================
    // INIT KINECT
    // ========================================================

    if (
        freenect_init(
            &f_ctx,
            nullptr
        )
        < 0
    )
    {
        std::cerr
            << "Erro iniciando libfreenect\n";

        return 1;
    }


    freenect_select_subdevices(
        f_ctx,
        (freenect_device_flags)(
            FREENECT_DEVICE_CAMERA |
            FREENECT_DEVICE_MOTOR
        )
    );


    if (
        freenect_num_devices(f_ctx)
        < 1
    )
    {
        std::cerr
            << "Nenhum Kinect encontrado\n";

        freenect_shutdown(f_ctx);

        return 1;
    }


    if (
        freenect_open_device(
            f_ctx,
            &f_dev,
            0
        )
        < 0
    )
    {
        std::cerr
            << "Nao consegui abrir o Kinect\n";

        freenect_shutdown(f_ctx);

        return 1;
    }


    int initial_ir =
        freenect_get_ir_brightness(
            f_dev
        );


    if (
        initial_ir < 1 ||
        initial_ir > 50
    )
    {
        initial_ir = 25;
    }


    requested_ir_brightness =
        initial_ir;


    // ========================================================
    // INTERFACE
    // ========================================================

    const std::string window =
        "Kinect Control";


    cv::namedWindow(
        window,
        cv::WINDOW_NORMAL
    );


    cv::resizeWindow(
        window,
        900,
        760
    );


    // ========================================================
    // CAMERA
    // ========================================================

    cv::createTrackbar(
        "Camera 0 RGB | 1 IR | 2 Depth",
        window,
        nullptr,
        2
    );


    // ========================================================
    // IMAGE
    // ========================================================

    cv::createTrackbar(
        "Brightness",
        window,
        nullptr,
        200
    );

    cv::setTrackbarPos(
        "Brightness",
        window,
        100
    );


    cv::createTrackbar(
        "Contrast x100",
        window,
        nullptr,
        300
    );

    cv::setTrackbarPos(
        "Contrast x100",
        window,
        100
    );


    cv::createTrackbar(
        "Gamma x100",
        window,
        nullptr,
        300
    );

    cv::setTrackbarPos(
        "Gamma x100",
        window,
        100
    );


    cv::createTrackbar(
        "Threshold",
        window,
        nullptr,
        255
    );


    // ========================================================
    // HARDWARE
    // ========================================================

    // slider 0..49
    // valor real 1..50

    cv::createTrackbar(
        "IR brightness 1-50",
        window,
        nullptr,
        49
    );

    cv::setTrackbarPos(
        "IR brightness 1-50",
        window,
        initial_ir - 1
    );


    // slider 0..60
    // valor real -30..+30

    cv::createTrackbar(
        "Tilt -30 to +30",
        window,
        nullptr,
        60
    );

    cv::setTrackbarPos(
        "Tilt -30 to +30",
        window,
        30
    );


    // ========================================================
    // STREAM
    // ========================================================

    cv::createTrackbar(
        "STREAM 0 OFF | 1 ON",
        window,
        nullptr,
        1
    );


    // ========================================================
    // KINECT THREAD
    // ========================================================

    std::thread kinect_thread(
        freenect_loop
    );


    int previous_stream_toggle = 0;


    auto next_stream_frame =
        std::chrono::steady_clock::now();


    // ========================================================
    // MAIN LOOP
    // ========================================================

    while (running)
    {
        // ----------------------------------------------------
        // Interface
        // ----------------------------------------------------

        int view =
            cv::getTrackbarPos(
                "Camera 0 RGB | 1 IR | 2 Depth",
                window
            );


        int brightness =
            cv::getTrackbarPos(
                "Brightness",
                window
            );


        int contrast =
            cv::getTrackbarPos(
                "Contrast x100",
                window
            );


        int gamma =
            cv::getTrackbarPos(
                "Gamma x100",
                window
            );


        int threshold =
            cv::getTrackbarPos(
                "Threshold",
                window
            );


        int ir_slider =
            cv::getTrackbarPos(
                "IR brightness 1-50",
                window
            );


        int tilt_slider =
            cv::getTrackbarPos(
                "Tilt -30 to +30",
                window
            );


        int stream_toggle =
            cv::getTrackbarPos(
                "STREAM 0 OFF | 1 ON",
                window
            );


        // hardware values reais

        int ir =
            ir_slider + 1;


        int tilt =
            tilt_slider - 30;


        requested_view =
            view;


        requested_ir_brightness =
            ir;


        requested_tilt =
            tilt;


        // ----------------------------------------------------
        // Stream ON / OFF
        // ----------------------------------------------------

        if (
            stream_toggle == 1 &&
            previous_stream_toggle == 0
        )
        {
            streamer.start(
                SRT_URL
            );


            next_stream_frame =
                std::chrono::steady_clock::now();
        }


        if (
            stream_toggle == 0 &&
            previous_stream_toggle == 1
        )
        {
            streamer.stop();
        }


        previous_stream_toggle =
            stream_toggle;


        // Se FFmpeg morreu sozinho,
        // devolve slider para OFF.

        if (
            stream_toggle == 1 &&
            !streamer.isActive()
        )
        {
            cv::setTrackbarPos(
                "STREAM 0 OFF | 1 ON",
                window,
                0
            );

            previous_stream_toggle = 0;
        }


        // ----------------------------------------------------
        // Escolhe source
        // ----------------------------------------------------

        cv::Mat source;


        {
            std::lock_guard<std::mutex> lock(
                frame_mutex
            );


            if (
                view == 0 &&
                !latest_rgb.empty()
            )
            {
                source =
                    latest_rgb.clone();
            }


            else if (
                view == 1 &&
                !latest_ir.empty()
            )
            {
                source =
                    latest_ir.clone();
            }


            else if (
                view == 2 &&
                !latest_depth.empty()
            )
            {
                source =
                    depth_to_image(
                        latest_depth
                    );
            }
        }


        // ----------------------------------------------------
        // Processa
        // ----------------------------------------------------

        if (!source.empty())
        {
            cv::Mat processed =
                process_image(
                    source,
                    brightness,
                    contrast,
                    gamma,
                    threshold
                );


            // =================================================
            // STREAM
            // =================================================

            auto now =
                std::chrono::steady_clock::now();


            if (
                streamer.isActive() &&
                now >= next_stream_frame
            )
            {
                streamer.pushFrame(
                    processed
                );


                next_stream_frame =
                    now +
                    std::chrono::milliseconds(33);
            }


            // =================================================
            // DISPLAY
            //
            // Clonamos porque estes overlays NÃO vão pro SRT.
            // =================================================

            cv::Mat display =
                processed.clone();


            std::string camera_name;


            if (view == 0)
                camera_name = "RGB";

            else if (view == 1)
                camera_name = "IR";

            else
                camera_name = "DEPTH";


            std::string status =
                camera_name;


            if (streamer.isActive())
                status += "  |  SRT ON";
            else
                status += "  |  SRT OFF";


            cv::putText(
                display,
                status,
                cv::Point(15, 30),
                cv::FONT_HERSHEY_SIMPLEX,
                0.7,
                cv::Scalar(255, 255, 255),
                2
            );


            cv::putText(
                display,
                "IR " +
                    std::to_string(ir) +
                    "  Tilt " +
                    std::to_string(tilt),
                cv::Point(15, 60),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55,
                cv::Scalar(255, 255, 255),
                1
            );


            cv::imshow(
                window,
                display
            );
        }


        // ----------------------------------------------------
        // Keyboard
        // ----------------------------------------------------

        int key =
            cv::waitKey(1);


        if (
            key == 27 ||
            key == 'q'
        )
        {
            running = false;
        }


        // janela fechada

        if (
            cv::getWindowProperty(
                window,
                cv::WND_PROP_VISIBLE
            )
            < 1
        )
        {
            running = false;
        }
    }


    // ========================================================
    // SHUTDOWN
    // ========================================================

    streamer.stop();


    running = false;


    if (kinect_thread.joinable())
        kinect_thread.join();


    freenect_close_device(
        f_dev
    );


    freenect_shutdown(
        f_ctx
    );


    cv::destroyAllWindows();


    return 0;
}
