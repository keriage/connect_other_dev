// コマンド
// g++ -Wall new_udp_uart.cpp -std=c++11 -I/usr/local/include/opencv4 -L/usr/local/lib -lopencv_core -lopencv_highgui -lopencv_imgcodecs -lopencv_videoio -lopencv_imgproc -lpigpio -lpthread -g -O0 -o test
// sudo ./test
// opencvのファイルlocalに入っていますので注意してください
// シリアルの初期化でエラーが出たばあい、sudo nano /etc/rc.localのファイルで、オートスタートを有効にしているかもしれません。確認してください。
//-------------------------------------------------------------------------

// UDP通信
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

// UART
#include <pigpio.h>

// cv
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>

// その他
#include <vector>
#include <thread>


using namespace std;
using namespace cv;

char pc_ip[] = "192.168.23.5";          //通信先PC
int ras_recv_port = 9001;
int port_pc_cam1 = 8081;                //サブカメラ1
int port_pc_cam2 = 8082;                //サブカメラ2
int baudRate = 9600; // BPS
int fps = 20;

//カメラ送信スレッド　
// ip：通信先IP  port：ポート　　WIDTH：横幅　　HEIGHT：縦幅　　num：カメラ番号　　ratio：圧縮率
void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio);



int main() {

    //カメラ用スレッド開始
    //thread th1(thread_cv, port_pc_cam1, 640, 360, 0, 60);
    thread th1(thread_cv, port_pc_cam1, 1920/3, 1080/3, 0, 50);
    th1.detach();

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
    serverAddr.sin_port = htons(ras_recv_port);       // ポート番号

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

    //送信も文字数設定
    int msgNum = 2;
    
    //udp受信のメモリ設定
    char buffer[16];
    char buffer_copy[3];

    while (true) {

        // タイムアウトの設定（例：2秒）
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        timeval timeout{};
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;

        // ソケットの監視
        int activity = select(sock + 1, &readfds, nullptr, nullptr, &timeout);
        if (activity < 0) {
            std::cerr << "select() error" << std::endl;
            break;
        }

        // タイムアウト時の割り込み処理
        if (activity == 0) {
            // タイムアウト時の処理（必要に応じて）
            std::cout << "Waiting for data..." << std::endl;

            // データ送信
            char dami_buffer[msgNum] = {'k', 0};
            int result = serWrite(serialHandle, dami_buffer, msgNum);

            //データ送信確認
            if (result < 0) {
                std::cerr << "[UART]Failed to send data!" << std::endl;
            } else {
                //std::cout << "[UART]Data sent: " << dami_buffer << std::endl;
                //printf("strlen size:%ld\n",strlen(dami_buffer));
                printf("Time Out! \ndami_buff[0]: %c , %u  dami_buff[1]: %c , %u\n",dami_buffer[0],dami_buffer[0],dami_buffer[1],dami_buffer[1]);                   
            }
            continue;
        }

        if (FD_ISSET(sock, &readfds)) {
            // データを受信
            ssize_t len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &addrLen);
            if (len > 0) {

                buffer[2] = '\0'; // 文字列の終端を追加
                std::cout << "[UDP]Received: " << buffer << std::endl;

/*
                if(strlen(buffer) >= 2){
                    buffer_copy[0] = buffer[0];
                    buffer_copy[1] = buffer[1];
                    buffer_copy[2] = '\0';
                }else{
                    buffer_copy[0] = 'k';
                    buffer_copy[1] = 0;
                    buffer_copy[2] = '\0';
                }
*/
                // データ送信
                int result = serWrite(serialHandle, buffer, msgNum);
                //int result = serWrite(serialHandle, buffer_copy, msgNum);

                if (result < 0) {
                    std::cerr << "[UART]Failed to send data!" << std::endl;
                } else {     
                    printf("uart送信数:%d\n buffer[0]: %c , %u  buffer[1]: %c , %u\n", msgNum, buffer[0], buffer[0], buffer[1], buffer[1]);              
                }
            }

            usleep(10000);

        }
    }

    // UARTの終了
    // serClose(serialHandle); // UARTポートのクローズ
    // gpioTerminate(); // pigpioの終了

    // UDPの終了
    close(sock);
    return 0;
}



void thread_cv(int port, int WIDTH, int HEIGHT, int num, int ratio)
{
    // ソケットの設定
    int sock;
    struct sockaddr_in addr;
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);             // ポート番号
    addr.sin_addr.s_addr = inet_addr(pc_ip); // 送信先IPアドレス


    // カメラの設定
    VideoCapture cap(num);
    cap.set(CAP_PROP_FRAME_WIDTH, WIDTH);
    cap.set(CAP_PROP_FRAME_HEIGHT, HEIGHT);
    cap.set(CAP_PROP_FPS, fps);

    if (!cap.isOpened())
    {
        cout << "Camera not Found\n!" << endl;
    }

    Mat frame;
    Mat jpgimg;
    static const int sendSize = 65500;      //通信最大パケット数
    char buff[sendSize];
    vector<unsigned char> ibuff;
    vector<int> param = vector<int>(2);
    param[0] = IMWRITE_JPEG_QUALITY;        //jpg使用
    param[1] = ratio;                       //圧縮率

    //メイン部分
    while (1)
    {
        cap >> frame;

        //最大パケット数を越えるとUDPできない
        //複数カメラを使うときは ibuff.size() の合計が65500を越えないように圧縮率で調整する。
        if (!frame.empty())
        {
            imencode(".jpg", frame, ibuff, param);

            for (std::vector<unsigned char>::size_type i = 0; i < ibuff.size(); i++)
                buff[i] = ibuff[i];
            sendto(sock, buff, sendSize, 0, (struct sockaddr*)&addr, sizeof(addr));
            jpgimg = imdecode(Mat(ibuff), IMREAD_COLOR);

        }

        // cout << "camera size" << ibuff.size() << "\n";

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
    }
    close(sock);
}