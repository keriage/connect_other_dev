// g++ -Wall udp_uart_queue.cpp -std=c++11 -I/usr/local/include/opencv4 -L/usr/local/lib 
// -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc 
// -lpigpio -lpthread -g -O0 -o udp_uart_queue
// sudo ./udp_uart_queue

#include <iostream>
#include <cstring>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
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

using namespace std;
using namespace cv;

char pc_ip[] = "192.168.23.5";
int ras_recv_port = 9001;
int port_pc_cam1 = 8081;
int baudRate = 9600;
int fps = 20;
const int msgNum = 2;

std::atomic<bool> running(true);
std::queue<string> uartQueue;
std::mutex queueMutex;
std::condition_variable queueCV;

void udp_receiver_thread() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "[UDP] Socket creation failed" << endl;
        return;
    }

    sockaddr_in serverAddr{}, clientAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ras_recv_port);

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "[UDP] Bind failed" << endl;
        close(sock);
        return;
    }

    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
    socklen_t addrLen = sizeof(clientAddr);
    char buffer[16];

    while (running) {
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
        if (len >= msgNum) {
            std::string cmd(buffer, msgNum);
            {
                std::lock_guard<std::mutex> lock(queueMutex);
                uartQueue.push(cmd);
            }
            queueCV.notify_one();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    close(sock);
}

void uart_sender_thread(int serialHandle) {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, [] { return !uartQueue.empty() || !running; });

        if (!running && uartQueue.empty()) break;

        std::string data = uartQueue.front();
        uartQueue.pop();
        lock.unlock();

        if (!data.empty()) {
            serWrite(serialHandle, data.c_str(), msgNum);
            printf("[UART] Sent: %c, %u | %c, %u\n", data[0], data[0], data[1], data[1]);
        }
    }
}

void camera_thread(int port, int WIDTH, int HEIGHT, int num, int ratio) {
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
        cout << "Camera not Found!" << endl;
        return;
    }

    Mat frame, jpgimg;
    static const int sendSize = 65500;
    char buff[sendSize];
    vector<unsigned char> ibuff;
    vector<int> param = {IMWRITE_JPEG_QUALITY, ratio};

    while (running) {
        cap >> frame;
        if (!frame.empty()) {
            imencode(".jpg", frame, ibuff, param);
            memcpy(buff, ibuff.data(), ibuff.size());
            sendto(sock, buff, ibuff.size(), 0, (struct sockaddr*)&addr, sizeof(addr));
            jpgimg = imdecode(Mat(ibuff), IMREAD_COLOR);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
    }
    close(sock);
}

int main() {
    if (gpioInitialise() < 0) {
        cerr << "[UART] pigpio initialization failed!" << endl;
        return 1;
    }

    int serialHandle = serOpen("/dev/serial0", baudRate, 0);
    if (serialHandle < 0) {
        cerr << "[UART] Failed to open serial port!" << endl;
        gpioTerminate();
        return 1;
    }

    thread cam_thread(camera_thread, port_pc_cam1, 640, 360, 0, 50);
    thread recv_thread(udp_receiver_thread);
    thread send_thread(uart_sender_thread, serialHandle);

    while (1) {
        cout << "program is running\n";
        sleep(10);
    }

    cout << "Press Enter to stop...\n";
    cin.get();
    running = false;
    queueCV.notify_all();

    cam_thread.join();
    recv_thread.join();
    send_thread.join();

    serClose(serialHandle);
    gpioTerminate();
    return 0;
}
