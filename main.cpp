#include <iostream>
#include <queue>
#include <map>
#include <set>
#include <unistd.h>
#include <stdlib.h>
#include <uv.h>
#include <string.h>
#include <time.h>

using namespace std;

uv_loop_t *loop;
uv_async_t async_send_msg;

queue<struct task> taskQueue;
pthread_mutex_t mutexTaskQueue = PTHREAD_MUTEX_INITIALIZER;


typedef struct {
	uv_write_t req;
	uv_buf_t buf;
} write_req_t;

struct task {
    task():client(NULL),size(0){
        memset(buffer,0,4097);
    }
    ~task() {}
    uv_work_t req;
    uv_stream_t* client;
    uint8_t buffer[4096+1];
    size_t  size;
};

void free_write_req(uv_write_t *req) {
    write_req_t *wr = (write_req_t*) req;
    free(wr->buf.base);
    free(wr);
}

void echo_write(uv_write_t *req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
    free_write_req(req);
}

void on_close(uv_handle_t* handle) {
    free(handle);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    *buf = uv_buf_init((char*)malloc(4097), 4097);
}

void business_task(uv_work_t *req)
{

    struct task* tsk = (struct task*)(req->data);
    /*
    cout << "tsk->size=" << tsk->size << endl;
    for (int i=0;i<tsk->size;i++) {
        printf("%02X ", tsk->buffer[i]);
    }
    cout << endl;
    */
    
    struct task new_task;
    new_task.size = tsk->size;
    new_task.client = tsk->client;
    memcpy(new_task.buffer, tsk->buffer, new_task.size);
    
    (void)pthread_mutex_lock(&mutexTaskQueue);
    taskQueue.push(new_task);
    (void)pthread_mutex_unlock(&mutexTaskQueue);

    uv_async_send(&async_send_msg);
}

void after_business(uv_work_t *req, int status) {
    /*
    struct task tsk;
    (void)pthread_mutex_lock(&mutexTaskQueue);
    while (!taskQueue.empty()) {
        tsk = taskQueue.front();
        taskQueue.pop();
        if (tsk.client != NULL) {
            write_req_t *req = (write_req_t*)malloc(sizeof(write_req_t));
            req->buf = uv_buf_init((char*)malloc(tsk.size),tsk.size);
            memcpy(req->buf.base, tsk.buffer, tsk.size);
            uv_write((uv_write_t*)req, (uv_stream_t*)tsk.client, &req->buf, 1, echo_write);
        }
    }
    (void)pthread_mutex_unlock(&mutexTaskQueue);
    */
    struct task* tsk1 = (struct task*)req->data;

    if (tsk1 != NULL) {
        delete tsk1;
        tsk1 = NULL;
    }
    
}

void echo_read(uv_stream_t* client, ssize_t nread, const uv_buf_t *buf)
{
    if (nread > 0) { 
        //StickyPacketHandling(buf->base, (int)nread, client);
        struct task *tsk = new struct task;
        tsk->req.data = (void*)tsk;
        tsk->size = nread;
        tsk->client = client;
        memcpy(tsk->buffer, buf->base, tsk->size);

        uv_queue_work(loop,&tsk->req, business_task, after_business);

        free(buf->base);
        return ;
    }

    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*)client, on_close);
    }
    
    free(buf->base);
}


void on_new_connection(uv_stream_t* server, int status) {  
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        return ;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    if (uv_accept(server, (uv_stream_t*)client) == 0) {
        uv_read_start((uv_stream_t*)client, alloc_buffer, echo_read);
    } else {
        uv_close((uv_handle_t*)client, on_close);
    }
}

void write_task(uv_async_t *handle)
{
    struct task tsk;
    (void)pthread_mutex_lock(&mutexTaskQueue);
    while (!taskQueue.empty()) {
        tsk = taskQueue.front();
        taskQueue.pop();
        if (tsk.client != NULL) {
            write_req_t *req = (write_req_t*)malloc(sizeof(write_req_t));
            req->buf = uv_buf_init((char*)malloc(tsk.size),tsk.size);
            memcpy(req->buf.base, tsk.buffer, tsk.size);
            uv_write((uv_write_t*)req, (uv_stream_t*)tsk.client, &req->buf, 1, echo_write);
        }
    }
    (void)pthread_mutex_unlock(&mutexTaskQueue);
}

int main(int argc, char **argv)
{
    loop = uv_default_loop();
    uv_tcp_t terminal_server;
    uv_tcp_init(loop, &terminal_server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", 10000, &addr);
    uv_tcp_bind(&terminal_server, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&terminal_server, -1, on_new_connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        exit(1);
    }

    uv_async_init(loop, &async_send_msg, write_task);
    
    uv_run(loop, UV_RUN_DEFAULT);
    
    return 0;
}
