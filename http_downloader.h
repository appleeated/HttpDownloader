/**
 * Written by Chendi Jin, 2018.3.14
 */

#ifndef __HTTP_DOWNLOADER_H__
#define __HTTP_DOWNLOADER_H__

#include <Winsock2.h>
#include <stdint.h>
#include <string>
#include <functional>
#include <thread>

namespace http
{

void default_download_result_callback(int result, const std::string &path);
void default_download_process_callback(int64_t now, int64_t total);

class Downloader
{
public:
    typedef ::SOCKET Socket;
    typedef std::function<void(int, const std::string &)> DownloadResultCallback;
    typedef std::function<void(int64_t, int64_t)> DownloadProcessCallback;
    
public:
    Downloader();
    ~Downloader();

public:
    int Download(const std::string &url,
         const std::string &path,
         const DownloadResultCallback &rcb = default_download_result_callback,
         const DownloadProcessCallback &pcb = default_download_process_callback);
    void Abort();
    
private:
    static void _proc_func(Downloader *downloader,
        const std::string &ip,
        int port,
        const std::string &url,
        const std::string &host,
        const std::string &path,
        const DownloadResultCallback &rcb,
        const DownloadProcessCallback &pcb);
    static Socket _open();
    static int _connect(Socket socket, const std::string &ip, int port);
    static int _send(Socket socket, const std::string &url, const std::string &host);
    static int  _receive(Socket socket,
        Downloader *downloader,
        const std::string &path,
        const DownloadResultCallback &rcb,
        const DownloadProcessCallback &pcb);
    static void _close(Socket socket);
    
private:
    std::thread _thread;

    /**
     * indicate the running state of the thread
     *   0 is running
     *   1 is stopped
     * it also used to stop the thread by set to 1
     */
    volatile int _stop;
    bool _thread_exist;
};

}

#endif
