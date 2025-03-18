#include "server.h"

unordered_map<int,std::string> Server::place_;
vector<pid_t> Server::sonPid_;
int Server::camCount_;

std::string httpFileName_;
unsigned long long httpFileLen_;

Server::Server(int port, int maxCam, bool openLog, int logLevel,
               int logQueSize, const char* mmapFileName,
               std::string httpMmapFileName, long long httpFileLen){
    port_ = port;
    maxCam_ = maxCam;
    openLog_ = openLog;
    logLevel_ = logLevel;
    sonPid_.resize(maxCam);
    for(pid_t& pid:sonPid_)
        pid = 0;
     // 是否打开日志标志
    if(openLog) {
        Log::Instance()->init(logLevel, "./log", ".log", logQueSize);
        LOG_INFO("========= TCPServer init =========");
        LOG_INFO("port %d, maxCam %d, logLevel %d, logQueSize %d",port, maxCam, logLevel, logQueSize);
        LOG_INFO("mmapFileName %s, httpMmapFileName %s",mmapFileName, httpMmapFileName.c_str());
    }

    httpFileName_ = httpMmapFileName;
    httpFileLen_ = httpFileLen;
    mmap_ = MmapInit_(mmapFd_,mmapFileName, __MMAP_FILE_LEN);
    exitMmap_ = MmapInit_(exitMmapFd_,"errorMmap", __MMAP_FILE_LEN);
    SockInit_();
}

Server:: ~Server(){
    close(listenFd_);
    LOG_INFO("========= TCPServer Close =========");
}

// 将文件描述符设置为非阻塞
void SetNonBlock(int fd){
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd , F_SETFL , flags|O_NONBLOCK);
}

char* Server::MmapInit_(int mmapFd, const char* mmapFileName, unsigned long long fileLen){
    // 打开文件
    mmapFd = open(mmapFileName, O_RDWR | O_CREAT, 0644);
    if(mmapFd == -1){
        LOG_ERROR("mmapFd init error");
        return NULL;
    }

    // 拓展内存
    ftruncate(mmapFd, fileLen);
    int mmapLen_ = lseek(mmapFd, 0, SEEK_END);
    if(mmapLen_ <= 0){
        LOG_ERROR("ftruncate error!");
        return NULL;
    }

    // 开始映射
    char* mmapPoint = (char*)mmap(NULL,mmapLen_,PROT_WRITE|PROT_READ,MAP_SHARED, mmapFd,0);
    if(mmapPoint == MAP_FAILED){
        LOG_ERROR("mmap error!");
        return NULL;
    }
    //起始位为\0说明无数据
    mmapPoint[0] = '\0';

    // // mmap赋值给对应指针
    // if(type)
    //     exitMmap_ = mmapPoint;
    // else
    //     mmap_ = mmapPoint;
    return mmapPoint;
}

void Server::SockInit_(){
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 创建socket
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if(listenFd_ == -1){
        LOG_ERROR("Socket error!");
        return;
    }

    // 绑定ip和端口
    int ret = bind(listenFd_, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if(ret == -1){
        LOG_ERROR("Bind error!");
        return;
    }

    //设置上限
    ret = listen(listenFd_, 128);
    if(ret == -1){
        LOG_ERROR("Listen error!");
        return;
    }

    client_addr_len = sizeof(client_addr);
    LOG_DEBUG("Socket Init suceess");
}

//捕捉信号的回调函数
void CatchChild_(int sigsum){
    while((waitpid(0,NULL,WNOHANG))>0);
    return;
}

// 接收格式为 id:place的数据
void Server::ParseCamInitData_(const char* str, pid_t sonPid){
    if(str[0] == '\0' || str[1] != ':'){
        LOG_ERROR("Parse Failed, Data type error");
        return;
    }
    int id = str[0] - '0';
    std::string place;
    for(int i=2;str[i] != '\n';i++){
        place += str[i];
    }
    place_[id] = place;
    if(sonPid_.size() < id -1){
        LOG_ERROR("ID is bigger then maxCam!!");
        return;
    }
    if(sonPid_[id] != 0) {
        LOG_ERROR("Cam id %d conflict",id);
    }
    sonPid_[id] = sonPid;
    camCount_++;
    LOG_INFO("Cam Connect: (id %d, place %s, pid %d)",id, place.c_str(), sonPid);
    LOG_INFO("CamCount : %d", camCount_);
}

int Server::ParseID(const char* str){
    return str[0] - '0';
}

void Server::CleanCam(const char* str){
    if(camCount_ <= 0){
        LOG_ERROR("camCount is under 0 but still call function CleanCam");
        return;
    }
    int id = ParseID(str);
    sonPid_[id] = 0;
    camCount_--;
    LOG_INFO("Cam id %d disconnect", id);
    LOG_INFO("CamCount : %d", camCount_);
}

void Server::WorkLoop_(){
    // 将套接字设置为非阻塞， 因为父进程还需要处理Cam的清理
    SetNonBlock(listenFd_);
    while(1){
        clientFd_ = accept(listenFd_, (struct sockaddr*)&client_addr, &client_addr_len);
        if(clientFd_ < 0){
            if(errno == EWOULDBLOCK){
                //说明有摄像头退出
                if(exitMmap_[0] != '\0'){ 
                    CleanCam(exitMmap_);//清除摄像头数据
                    exitMmap_[0] = '\0';
                }
                continue;
            }
            LOG_ERROR("accept error!");
            break;
        }

        pid_ = fork();
        if(pid_ < 0){
            LOG_ERROR("Fork error");
            break;
        }else if(pid_ == 0){    //子进程
            bool flag = true;   //是否已初始化标志
            int id = 0;         //id标号
            int len = -1;
            int count = 0;      //未从套接字读取到信息的次数
            int fd;
            std::string data;
            char* mmapPoint;
            //测试
                char buffer[1024];
            //测试
            // 与esp32cam通讯循环
            close(listenFd_);
            // 将套接字设置为非阻塞模式用来判断设备有没有断开连接
            SetNonBlock(clientFd_);
            while(1){

                // 先读取ESP32CAM发来的初始化信息 --> id:place
                if(flag){
                    while(len == -1){   //len为-1则说明未接受到数据
                        usleep(100000);
                        len = read(clientFd_, mmap_, __MMAP_FILE_LEN);
                    }
                    mmap_[len] = '\0';
                    write(STDOUT_FILENO, mmap_, len);
                    id = ParseID(mmap_);
                    char c = id + '0';
                    write(STDOUT_FILENO, &c, 1);
                    flag = false;
                    std::string name = httpFileName_ + c;
                    mmapPoint = MmapInit_(fd, name.c_str(), httpFileLen_);
                    //MmapInit_(name.c_str(), fd, )
                    continue;
                }
                
                len = read(clientFd_, buffer, sizeof(buffer));
                if(len == -1 && count<__FAIL_TO_READ_COUNT){//len为-1说明没有接收到数据
                    usleep(100000);    //延时100ms
                    count++;
                    continue;
                }else if(len == 0 || count>=__FAIL_TO_READ_COUNT){// len==0 说明已经关闭连接 或者count大于最大值服务器自动断开连接
                    close(clientFd_);               //关闭连接
                    exitMmap_[0] = id + '0';       
                    exitMmap_[1] = '\0';
                    LOG_DEBUG("Cam id %d  PID %d is done", id, getpid());
                    exit(1);
                }
                count = 0;  //次数清0
                
                //将数据加入缓冲区
                data.assign(buffer, len);
                if(data.size() >= 30000){
                    data = "";
                    continue;
                }else if(mmapPoint[0] == '\0'){ //mmap首字符不等于\0的时候再放入数据
                    // 将数据放入mmap中
                    //write(STDOUT_FILENO,data.substr(0, data.size()).c_str(),data.size());
                    strcpy(mmapPoint, data.substr(0, httpFileLen_ - 1).c_str());
                    data.erase(0, httpFileLen_ - 1);
                }
            }
        }else{               //父进程
            LOG_DEBUG("Son pid is %d",pid_);
            //信号回收子进程
            struct sigaction act;
            act.sa_handler = CatchChild_;
            sigemptyset(&act.sa_mask);
            act.sa_flags = 0;

            sigaction(SIGCHLD,&act,NULL);
            close(clientFd_);             //关闭用于通信的套接字
            while(mmap_[0] == '\0')
                usleep(1000); //读取到了初始化数据则进行数据解析 延时确保数据正确性
            usleep(1000);       //再次延时保证子进程已经成功解析id
            ParseCamInitData_(mmap_,pid_);   //解析起始数据
            mmap_[0] = '\0';
            continue;
        }
    }   
    LOG_ERROR("WorkLoop exit!!!");
}

void Server::start(){
    WorkLoop_();
}