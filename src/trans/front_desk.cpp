//
// Created by hzl on 19-6-2.
//

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <new>



#include <tomato.h>

#include "loop.h"
#include "front_desk.h"

#define log printf

class reception
{
public:
    reception(int fd)
    {
        m_iFd = fd;
        m_eventFd = -1;
    }
    ~reception(){}
    int start(void);
    void run(void);
    int processEvent(int fd, int event);
    int processMsg(int fd);

    int m_iFd;
    int m_eventFd;

private:
    pthread_t m_tid;

private:
    static void *callRun(void *pData);

};

int reception::start()
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_create(&m_tid, &attr, reception::callRun, (void *)this);
    pthread_attr_destroy(&attr);
}

void *reception::callRun(void *pData)
{
    reception *pThis = (reception *)pData;

    pThis->run();

    return nullptr;
}

void reception::run()
{
    int iRet = -1;
    int fd = -1;
    int listenFd = -1;
    int acceptFd = -1;
    struct epoll_event event;
    struct epoll_event astEvent[1];
    int iNum = 0;
    int i;

    fd = epoll_create(4);
    if (- 1 == fd)
    {
        goto end;
    }


    if (-1 == m_iFd)
    {
        goto end;
    }

    event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
    event.data.fd = m_iFd;
    iRet = epoll_ctl(fd, EPOLL_CTL_ADD, m_iFd, &event);
    if (0 != iRet)
    {
        goto end;
    }

    m_eventFd =fd;
    for (;;)
    {
        iNum = epoll_wait(fd, astEvent, 1, -1);
        if (iNum > 0)
        {
            /* process event */
            iRet = processEvent(astEvent[0].data.fd, astEvent[0].events);
            if (0 != iRet)
            {
                break;
            }

        }
        else if (iNum < 0)
        {
            break;
        }

    }


    end:
    if (-1 != fd)
    {
        close(fd);
        fd = -1;
    }

    if (-1 != m_iFd)
    {
        close(m_iFd);
        m_iFd = -1;
    }

    delete this;

    return;
}

int reception::processEvent(int fd, int event)
{
    int iRet = -1;

    if (0 != (event & EPOLLIN))
    {
        iRet = processMsg(fd);
        if (0 != iRet)
        {
            log("processMsg error event:%x\n", event);
            return -1;
        }

    }

    if (0 != (event & (EPOLLHUP | EPOLLERR)))
    {
        epoll_ctl(m_eventFd, EPOLL_CTL_DEL, fd, NULL);
        //close(fd);
        log("epoll EPOLLHUP\n");
        return -1;
        /* close connect */
    }


    return 0;
}

int reception::processMsg(int fd)
{

    static char szBuf[64];
    static char szPage[TOMATO_PAGE_SIZE];
    static struct tag_tomaoto_address stAddress;
    ssize_t lLen = 0;
    int iFlag = 0;

    lLen = recv(fd, szBuf, sizeof(int), MSG_WAITALL);
    if ((size_t)lLen != sizeof(int))
    {
        log("recv len:%ld error\n", lLen);
        goto err;
    }

    iFlag = *(int *)(char *)szBuf;

    switch(iFlag)
    {
        case TRANS_WRITE:
        {
            lLen = recv(fd, szPage, TOMATO_PAGE_SIZE, MSG_WAITALL);
            if (TOMATO_PAGE_SIZE != lLen)
            {
                log("recv error lLen:%ld\n", lLen);
                goto err;
            }

            bzero(&stAddress, sizeof(stAddress));
            stAddress.uiId = 1;
            lLen = send(fd, &stAddress, sizeof(stAddress),0);
            if (sizeof(stAddress) != lLen)
            {
                log("send error lLen:%ld\n", lLen);
                goto err;
            }

            break;
        }
        case TRANS_READ:
        {
            lLen = recv(fd, &stAddress, sizeof(stAddress), MSG_WAITALL);
            if (sizeof(stAddress) != lLen)
            {
                log("recv error lLen:%ld\n", lLen);
                goto err;
            }

            lLen = send(fd, szPage, TOMATO_PAGE_SIZE,0);
            if (TOMATO_PAGE_SIZE != lLen)
            {
                log("send error lLen:%ld\n", lLen);
                goto err;
            }
            break;
        }
        default:
        {
            break;
        }
    }

    goto end;

err:
    return -1;
end:

    return 0;
}

/*============================================================*/


int front_desk::start()
{
    run(nullptr);

    return 0;
}

int front_desk::listenCB(int fd, int iEvent, void *pData)
{
    int acceptFd = -1;
    acceptFd = accept(fd, nullptr, nullptr);
    reception *pRe = new(std::nothrow) reception(acceptFd) ;
    pRe->start();

    return 0;
}


void *front_desk::run(void *pData)
{
    int iRet = -1;
    int fd = -1;
    int listenFd = -1;
    int acceptFd = -1;
    struct epoll_event event;
    struct epoll_event astEvent[1];
    struct sockaddr_in stAddr;
    int iNum = 0;

    loop lp;


    listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (-1 == listenFd)
    {
        goto end;
    }

    bzero(&stAddr, sizeof(struct sockaddr_in));
    stAddr.sin_family = AF_INET;
    stAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    stAddr.sin_port = htons(FRONT_PORT);
    iRet = bind(listenFd, (struct sockaddr *)&stAddr, sizeof(stAddr));
    if (0 != iRet)
    {
        goto end;
    }

    iRet = listen(listenFd, 256);
    if (0 != iRet)
    {
        goto end;
    }

    iRet = lp.insert(listenFd, listenCB);
    printf("lp.insert ret=%d\n", iRet);


    lp.looping();

end:

    return nullptr;
}
