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
#include <queue>
#include <mutex>
#include <condition_variable>
#include <array>

using namespace std;
using namespace cv;

char pc_ip[] = "192.168.23.5";
int ras_recv_port = 9001;
int port_pc_cam1 = 8081;
int baudRate = 9600;
int fps = 20;
int msgNum = 2;

void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio);

queue<array<char, 16>> uart_queue;
mutex uart_mutex;
condition_variable uart_cv;
bool uart_running = true;

void uart_sender_thread(int serialHandle) {
    while (uart_running) {
        unique_lock<mutex> lock(uart_mutex);
        uart_cv.wait(lock, [] { return !uart_queue.empty() || !uart_running; });

        while (!uart_queue.empty()) {
            auto msg = uart_queue.front();
            uart_queue.pop();
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

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    fd_set readfds;
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

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

    thread uart_thread(uart_sender_thread, serialHandle);

    cout << "[UDP]Listening on port " << ras_recv_port << endl;

    while (true) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 5000;

        int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            cerr << "select() error" << endl;
            break;
        }

        if (activity == 0) continue;

        if (FD_ISSET(sock, &readfds)) {
            char buffer[16] = {0};
            ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0,
                                   (sockaddr*)&clientAddr, &addrLen);
            if (len > 0) {
                buffer[len] = '\0';
                array<char, 16> msg{};
                strncpy(msg.data(), buffer, 16);

                {
                    lock_guard<mutex> lock(uart_mutex);
                    uart_queue.push(msg);
                }
                uart_cv.notify_one();
            }
        }
    }

    uart_running = false;
    uart_cv.notify_one();
    uart_thread.join();

    serClose(serialHandle);
    gpioTerminate();
    close(sock);
    return 0;
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
