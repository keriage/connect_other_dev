// コマンド
// g++ -Wall rescon_yaramaika.cpp -std=c++11 -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc -lpigpio -lpthread -g -O0 -o test
// g++ -Wall new_udp_uart.cpp -std=c++11 -I/usr/local/include/opencv4 -L/usr/local/lib -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc -lpigpio -lpthread -g -O0 -o test
// sudo ./test
//
//-------------------------------------------------------------------------

// UDP通信
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

// UART
#include <pigpio.h>

int ras_recv_ip = 9001;
int baudRate = 9600; // BPS

int main() {
    // ソケット生成
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[UDP]Socket creation failed" << std::endl;
        return -1;
    }

    // ソケットアドレスの設定
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // すべてのIPアドレスで受信
    serverAddr.sin_port = htons(ras_recv_ip);       // ポート番号

    // ソケットのバインド
    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[UDP]Bind failed" << std::endl;
        close(sock);
        return -1;
    }

    // 非ブロッキングモードに設定
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    fd_set readfds;
    char buffer[1024];
    sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);

    std::cout << "[UDP]Listening for UDP packets..." << std::endl;


    // pigpioの初期化
    if (gpioInitialise() < 0) {
        std::cerr << "[UART]pigpio initialization failed!" << std::endl;
        return 1;
    }

    // シリアルポートの設定
    int serialHandle = serOpen(const_cast<char*>("/dev/serial0"), baudRate, 0); // UARTポートのオープン
    if (serialHandle < 0) {
        std::cerr << "[UART]Failed to open serial port!" << std::endl;
        gpioTerminate();
        return 1;
    }

    std::cout << "[UART] initialized at baud rate " << baudRate << std::endl;


    while (true) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        // タイムアウトの設定（例：2秒）
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        // ソケットの監視
        int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);

        if (activity < 0) {
            std::cerr << "select() error" << std::endl;
            break;
        }

        if (activity == 0) {
            // タイムアウト時の処理（必要に応じて）
            std::cout << "Waiting for data..." << std::endl;

                // データ送信
                char dami_buffer[] = "k";
                int result = serWrite(serialHandle, dami_buffer, strlen(buffer));
                if (result < 0) {
                    std::cerr << "[UART]Failed to send data!" << std::endl;
                } else {
                    std::cout << "[UART]Data sent: " << dami_buffer << std::endl;                   
                }

            continue;
        }

        if (FD_ISSET(sock, &readfds)) {
            // データを受信
            ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
            if (len > 0) {
                buffer[len] = '\0'; // 文字列の終端を追加
                std::cout << "[UDP]Received: " << buffer << std::endl;

                // データ送信
                /*int result = serWrite(serialHandle, buffer, strlen(buffer));*/
                  int result = serWrite(serialHandle, buffer, len);
                if (result < 0) {
                    std::cerr << "[UART]Failed to send data!" << std::endl;
                } else {
                    std::cout << "[UART]Data sent: " << buffer << std::endl;                   
                }

            } else {
                std::cerr << "[UDP]recvfrom() error" << std::endl;
            }
        }
    }

    // UARTの終了
    serClose(serialHandle); // UARTポートのクローズ
    gpioTerminate(); // pigpioの終了

    // UDPの終了
    close(sock);
    return 0;
}
                                                                 