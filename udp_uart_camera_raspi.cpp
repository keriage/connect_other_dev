// g++ -Wall udp_uart_camera.cpp -std=c++11 -I/usr/local/include/opencv4 -L/usr/local/lib \
// -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc \
// -lpigpio -lpthread -g -O0 -o udp_uart_camera
// sudo ./udp_uart_camera

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <pigpio.h>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <vector>
#include <thread>
#include <atomic>

using namespace std;
using namespace cv;

char pc_ip[] = "192.168.23.5";   // PCのIPアドレス
int ras_recv_port = 9001;
int port_pc_cam1 = 8081;
int baudRate = 9600;
int fps = 20;
int msgNum = 2;

std::atomic<bool> running(true);

void udp_uart_thread(int ras_recv_port, int serialHandle, int msgNum) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[UDP]Socket creation failed" << std::endl;
        return;
    }

    sockaddr_in serverAddr{}, clientAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ras_recv_port);

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[UDP]Bind failed" << std::endl;
        close(sock);
        return;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);

    fd_set readfds;
    socklen_t addrLen = sizeof(clientAddr);
    char buffer[16];

    while (running) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout{2, 0};

        int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            std::cerr << "[UDP]select() error" << std::endl;
            break;
        } else if (activity == 0) {
            char dami_buffer[2] = {'k', 0};
            serWrite(serialHandle, dami_buffer, msgNum);
            printf("[Timeout] Sent: %c, %u | %c, %u\n", dami_buffer[0], dami_buffer[0], dami_buffer[1], dami_buffer[1]);
            continue;
        }

        if (FD_ISSET(sock, &readfds)) {
            ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
            if (len >= 2) {
                buffer[2] = '\0';
                std::cout << "[UDP]Received: " << buffer << std::endl;
                int result = serWrite(serialHandle, buffer, msgNum);
                if (result < 0)
                    std::cerr << "[UART]Failed to send data!" << std::endl;
                else
                    printf("[UART] Sent: %c, %u | %c, %u\n", buffer[0], buffer[0], buffer[1], buffer[1]);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    close(sock);
}

void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio) {
    int sock;
    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(pc_ip);

    VideoCapture cap(num);
    cap.set(CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(CAP_PROP_FRAME_HEIGHT, HEIGHT);
    cap.set(CAP_PROP_FPS, fps);

    if (!cap.isOpened()) {
        cout << "Camera not Found\n!" << endl;
        return;
    }

    Mat frame;
    Mat jpgimg;
    static const int sendSize = 65500;
    char buff[sendSize];
    vector<unsigned char> ibuff;
    vector<int> param = {IMWRITE_JPEG_QUALITY, ratio};

    while (running) {
        cap >> frame;
        if (!frame.empty()) {
            imencode(".jpg", frame, ibuff, param);
            for (size_t i = 0; i < ibuff.size(); i++)
                buff[i] = ibuff[i];
            sendto(sock, buff, ibuff.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
            jpgimg = imdecode(Mat(ibuff), IMREAD_COLOR);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
    }
    close(sock);
}

int main() {
    if (gpioInitialise() < 0) {
        std::cerr << "[UART]pigpio init failed\n";
        return 1;
    }

    int serialHandle = serOpen("/dev/serial0", baudRate, 0);
    if (serialHandle < 0) {
        std::cerr << "[UART]Failed to open serial port!" << std::endl;
        gpioTerminate();
        return 1;
    }

    std::thread th_cv(thread_cv, port_pc_cam1, 640, 360, 0, 50);
    std::thread th_comm(udp_uart_thread, ras_recv_port, serialHandle, msgNum);

    std::cout << "Press Enter to stop...\n";
    std::cin.get();
    running = false;

    th_cv.join();
    th_comm.join();

    serClose(serialHandle);
    gpioTerminate();
    return 0;
}
