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
#include <array>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;
using namespace cv;

char pc_ip[] = "192.168.23.5";
int ras_recv_port = 9001;
int port_pc_cam1 = 8081;
int baudRate = 9600;
int fps = 20;
int msgNum = 2;

queue<array<char, 2>> uartQueue;
mutex uartMutex;
condition_variable uartCV;
bool running = true;

void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio);
void uart_thread(int serialHandle);

int main() {
    thread th1(thread_cv, port_pc_cam1, 1920 / 3, 1080 / 3, 0, 50);
    th1.detach();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        cerr << "[UDP]Socket creation failed" << endl;
        return -1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(ras_recv_port);

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "[UDP]Bind failed" << endl;
        close(sock);
        return -1;
    }

    if (gpioInitialise() < 0) {
        cerr << "[UART]pigpio initialization failed!" << endl;
        return 1;
    }

    int serialHandle = serOpen((char*)"/dev/serial0", baudRate, 0);
    if (serialHandle < 0) {
        cerr << "[UART]Failed to open serial port!" << endl;
        gpioTerminate();
        return 1;
    }

    thread uartTh(uart_thread, serialHandle);

    cout << "[UDP]Listening on port " << ras_recv_port << endl;
    cout << "[UART] initialized at baud rate " << baudRate << endl;

    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    while (true) {
        char buffer[16] = {0};
        ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                               (sockaddr*)&clientAddr, &addrLen);
        if (len > 0) {
            buffer[len] = '\0';
            if (len >= msgNum) {
                array<char, 2> msg = { buffer[0], buffer[1] };
                {
                    lock_guard<mutex> lock(uartMutex);
                    uartQueue.push(msg);
                }
                uartCV.notify_one();
            }
        }
        usleep(1000);
    }

    running = false;
    uartCV.notify_all();
    uartTh.join();
    serClose(serialHandle);
    gpioTerminate();
    close(sock);
    return 0;
}

void uart_thread(int serialHandle) {
    while (running) {
        unique_lock<mutex> lock(uartMutex);
        uartCV.wait(lock, [] { return !uartQueue.empty() || !running; });

        while (!uartQueue.empty()) {
            auto msg = uartQueue.front();
            uartQueue.pop();
            lock.unlock();

            int result = serWrite(serialHandle, msg.data(), msgNum);
            if (result < 0) {
                cerr << "[UART]Failed to send data!" << endl;
            } else {
                printf("UART sent: %c %c\n", msg[0], msg[1]);
            }

            lock.lock();
        }
    }
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
        cout << "Camera not Found!\n";
        return;
    }

    Mat frame;
    vector<unsigned char> ibuff;
    vector<int> param = { IMWRITE_JPEG_QUALITY, ratio };
    char buff[65500];

    while (waitKey(1) == -1) {
        cap >> frame;
        imencode(".jpg", frame, ibuff, param);

        if (ibuff.size() < 65500) {
            for (size_t i = 0; i < ibuff.size(); i++)
                buff[i] = ibuff[i];
            sendto(sock, buff, 65500, 0, (struct sockaddr*)&addr, sizeof(addr));
        }

        this_thread::sleep_for(chrono::milliseconds(1000 / fps));
    }

    close(sock);
}
