#ifndef __SERVER_H
#define __SERVER_H

#include <stdio.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>       // fcntl()
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <unordered_map>
#include <vector>
#include "../buffer/buffer.h"
#include "../log/log.h"

#define __MMAP_FILE_LEN 128 //mmap内存映射的文件大小
#define __FAIL_TO_READ_COUNT 50 //100ms读取一次socket数据，如果超过该数则断开连接

class Server{
public:
    Server(int port, int maxCam, bool openLog, int logLevel,
            int logQueSize, const char* mmapFileName,
            std::string httpMmapFileName, long long httpFileLen);
    ~Server();
    void start();

private:
    int port_;
    bool openLog_;  //是否开启日志
    int logLevel_;  //日志等级
    int maxCam_;    //最大摄像头数目
    static int camCount_;   //摄像头数量

    int listenFd_;
    int clientFd_;
    socklen_t client_addr_len;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    pid_t pid_;  //当前pid
    char* mmap_; //内存映射 父子进程通信
    int mmapFd_;
    int mmapLen_;
    char* exitMmap_;
    int exitMmapFd_;
    char* httpMmap_;    //内存映射 与http服务器通信

    void SockInit_();
    char* MmapInit_(int mmapFd, const char* mmapFileName, unsigned long long fileLen);
    void WorkLoop_();
    void ParseCamInitData_(const char* str, pid_t sonPid);    //解析摄像头初始化信息 --> id:place;
    int ParseID(const char* str);  //解析ID
    void CleanCam(const char* str); //清理已退出的摄像头

    // Key:id Value:place
    static unordered_map<int,std::string> place_;
    // 处理摄像头传输子进程pid  为0说明该摄像头未打开
    static vector<pid_t> sonPid_;


    Buffer buff_;
};

#endif