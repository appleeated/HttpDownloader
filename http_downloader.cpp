#include "http_downloader.h"
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace http
{

static const char *protocal_set[] = 
{
    "http://",
    "https://",
    0
};

static const char http_head_format[] = {
    "GET %s HTTP/1.1\r\n"
    "Host:%s\r\n"
    "Connection:Keep-Alive\r\n"
    "Accept:*/*\r\n"
    "User-Agent:Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537(KHTML, like Gecko) Chrome/47.0.2526Safari/537.36\r\n"
    "Accept-Language:zh-cn\r\n"
    "Accept-Encoding:gzip, deflate\r\n"
    "\r\n"
};

class DownloaderBase
{
public:
    DownloaderBase()
    {
        WSAStartup(MAKEWORD(2,2), &_wsadata);
    }
    
    ~DownloaderBase()
    {
        WSACleanup();
    }
   
public:
    static int ParseUrl(const char *url, char *domain, int &port, char *file_name)
    {
        int j = 0;
        int pos1 = 0;
        
        port = 80;

        for (int i = 0; protocal_set[i]; ++i)
            if (strncmp(protocal_set[i], url, strlen(protocal_set[i])) == 0)
                pos1 = strlen(protocal_set[i]);

        for (int i = pos1; url[i] != '/' && url[i] != '\0'; ++i, ++j)
            domain[j] = url[i];
        domain[j] = '\0';

        char *substr = strstr(domain, ":");
        
        if (substr)
            sscanf(substr, ":%d", port);

        for (int i = 0; i != strlen(domain); ++i)
        {
            if (domain[i] == ':')
            {
                domain[i] = '\0';
                break;
            }
        }

        j = 0;
        
        for (int i = pos1; url[i] != '\0'; ++i)
        {
            if (url[i] == '/')
            {
                if (i != strlen(url) - 1)
                    j = 0;
                continue;
            }
            else
                file_name[j++] = url[i];
        }
        file_name[j] = '\0';

        return 0;
    }
    
    static int GetIpFromDomain(const char *domain, char *ip)
    {
        ADDRINFO hint;
        ADDRINFO *result = 0;

        ZeroMemory(&hint, sizeof(hint));

        if (getaddrinfo(domain, 0, &hint, &result) != 0)
            return -1;

        char *_ip = inet_ntoa(((sockaddr_in*)result[0].ai_addr)->sin_addr);

        strcpy(ip, _ip);

        return 0;
    }

private:
    WSADATA _wsadata;
};

// a global wsadata
static DownloaderBase downloader_base;

void default_download_result_callback(int result, const std::string &path) {}
void default_download_process_callback(int64_t now, int64_t total) {}

Downloader::Downloader() :
    _stop(1),
    _thread_exist(false)
{
    
}

Downloader::~Downloader()
{
    if (_stop == 0)
        Abort();
}

int Downloader::Download(const std::string &url,
    const std::string &path,
    const DownloadResultCallback &rcb,
    const DownloadProcessCallback &pcb)
{
    if (_stop == 0)
        Abort();

    char domain[64] = "";
    char ip[64] = "";
    int port = 80;
    char file_name[512] = "";

    if (DownloaderBase::ParseUrl(url.c_str(), domain, port, file_name) != 0)
        return -1;

    if (DownloaderBase::GetIpFromDomain(domain, ip) != 0)
        return -1;
    
    std::string fpath = path;
    
    fpath.append(file_name);
    
    std::thread new_thread(std::bind(&Downloader::_proc_func, this, ip, port, url, domain, fpath, rcb, pcb));
    
    _thread.swap(new_thread);

    _thread_exist = true;

    return 0;
}

void Downloader::Abort()
{
    if (_thread_exist)
    {
        _stop = 1;
        _thread.join();
    }
}

void Downloader::_proc_func(Downloader *downloader,
    const std::string &ip,
    int port,
    const std::string &url,
    const std::string &host,
    const std::string &path,
    const DownloadResultCallback &rcb,
    const DownloadProcessCallback &pcb)
{
    int result = 0;

    downloader->_stop = 0; 
    
    Socket socket = Downloader::_open();

    if (socket == NULL)
    {
        rcb(-1, "");
        return;
    }
    
    do
    {
        if ((result = Downloader::_connect(socket, ip, port)) != 0 || downloader->_stop == 1)
            break;
       
        if ((result = Downloader::_send(socket, url, host)) != 0 || downloader->_stop == 1)
            break;

        if ((result = Downloader::_receive(socket, downloader, path, rcb, pcb)) != 0 || downloader->_stop == 1)
            break;
    }
    while (0);

    Downloader::_close(socket);
    downloader->_stop = 1;

    rcb(result, path);
}

Downloader::Socket Downloader::_open()
{
    return ::socket(AF_INET, SOCK_STREAM, 0);
}

int Downloader::_connect(SOCKET socket, const std::string &ip, int port)
{
    sockaddr_in saddr = {0};

    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = inet_addr(ip.c_str());
    saddr.sin_port = htons(port);

    return ::connect(socket, (sockaddr*)&saddr, sizeof(saddr));
}

int Downloader::_send(SOCKET socket, const std::string &url, const std::string &host)
{
    char request[2048] = "";
    
    sprintf(request, http_head_format, url.c_str(), host.c_str());
    
    return ::send(socket, request, 2048, 0) > 0 ? 0 : -1;
}

int Downloader::_receive(SOCKET socket,
    Downloader *downloader,
    const std::string &path,
    const DownloadResultCallback &rcb,
    const DownloadProcessCallback &pcb)
{
    int mem_size = 4096;
    int length = 0;
    int len;
    char *buf = (char *)malloc(mem_size * sizeof(char));
    char *response = (char *)malloc(mem_size * sizeof(char));

    ZeroMemory(buf, mem_size * sizeof(char));
    ZeroMemory(response, mem_size * sizeof(char));
    buf[0] = '\0';
    response[0] = '\0';

    while ((len = recv(socket, buf, 1, 0)) > 0)
    {
        if (length + len > mem_size)
        {
            mem_size *= 2;
            char * temp = (char *)realloc(response, sizeof(char) * mem_size);
            if (temp == NULL)
            {
                printf("realloc failed\n");
                exit(-1);
            }
            response = temp;
        }

        buf[len] = '\0';
        strcat(response, buf);

        int flag = 0;
        for (int i = strlen(response) - 1; response[i] == '\n' || response[i] == '\r'; i--, flag++);
        if (flag == 4)
            break;

        length += len;
    }

    if (len < 0)
        return -1;

    char *str_size = NULL;
    char *str_size_end = NULL;
    int str_len = 0;

    // total size maybe not equal to the real size, which is set by the server
    int64_t total = 0;

    str_size = strstr(response, "Content-Length: ");
    str_size += strlen("Content-Length: ");
    str_size_end = strstr(str_size, "\r\n");
    str_len = str_size_end - str_size;
    str_size[str_len] = '\0';

    sscanf(str_size, "%lld", &total);

    len = 0;
    char buff2[1024] = "";

    FILE *file = fopen(path.c_str(), "wb");

    if (!file)
        return -1;

    int64_t now = 0;
    
    while ((len = recv(socket, buff2, 1024, 0)) > 0)
    {
        if (downloader->_stop == 1)
        {
            fclose(file);
            return 1;
        }

        fwrite(buff2, len, 1, file);
        now += len;
        pcb(now, total);
    }

    fclose(file);
    
    return len == 0 ? 0 : -1;
}

void Downloader::_close(SOCKET socket)
{
    ::closesocket(socket);
}

}

