#include <cassert>
#include <atomic>
#include <queue>
#include <mutex>
#include <chrono>
#include <thread>

#include "usb/usblink_queue.h"

struct usblink_queue_action {
    enum {
        PUT_FILE,
        SEND_OS,
        DIRLIST,
        MOVE,
        DEL_FILE,
        NEW_DIR,
        DEL_DIR,
        GET_FILE
    } action;

    //Members only used if the appropriate action is set
    std::string local;
    std::string remote;
    usblink_progress_cb progress_callback = nullptr;
    usblink_dirlist_cb dirlist_callback = nullptr;
    void *user_data;
};

static std::atomic_bool busy;
static std::queue<usblink_queue_action> usblink_queue;
static std::mutex usblink_queue_mut;

static void dirlist_callback(struct usblink_file *f, bool is_error, void *user_data)
{
    usblink_dirlist_cb callback = nullptr;
    {
        std::lock_guard<std::mutex> lg(usblink_queue_mut);
        if(usblink_queue.empty()) {
            busy.store(false);
            return;
        }

        const auto &front = usblink_queue.front();
        if(front.user_data != user_data || front.action != usblink_queue_action::DIRLIST) {
            busy.store(false);
            return;
        }

        callback = front.dirlist_callback;
        if(!f) {
            usblink_queue.pop();
            busy.store(false);
        }
    }

    if(callback != nullptr)
        callback(f, is_error, user_data);
}

static void progress_callback(int progress, void *user_data)
{
    usblink_progress_cb callback = nullptr;
    int action = usblink_queue_action::PUT_FILE;
    {
        std::lock_guard<std::mutex> lg(usblink_queue_mut);
        if(usblink_queue.empty()) {
            busy.store(false);
            return;
        }

        const auto &front = usblink_queue.front();
        if(front.user_data != user_data) {
            busy.store(false);
            return;
        }

        callback = front.progress_callback;
        action = front.action;

        if(progress < 0 || progress == 100)
        {
            usblink_queue.pop();
            busy.store(false);
        }
    }

    if(callback != nullptr)
    {
        if(action == usblink_queue_action::PUT_FILE
                || action == usblink_queue_action::SEND_OS
                || action == usblink_queue_action::MOVE
                || action == usblink_queue_action::NEW_DIR
                || action == usblink_queue_action::DEL_DIR
                || action == usblink_queue_action::DEL_FILE
                || action == usblink_queue_action::GET_FILE)
            callback(progress, user_data);
        else
            assert(false);
    }
}

void usblink_queue_do()
{
    usblink_queue_action action;
    {
        std::lock_guard<std::mutex> lg(usblink_queue_mut);
        if(!usblink_connected || usblink_queue.empty() || busy.load())
            return;

        busy.store(true);
        action = usblink_queue.front();
    }

    switch(action.action)
    {
    case usblink_queue_action::PUT_FILE:
        if(!usblink_put_file(action.local.c_str(), action.remote.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy.store(false);
        }
        break;
    case usblink_queue_action::SEND_OS:
        if(!usblink_send_os(action.local.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy.store(false);
        }
        break;
    case usblink_queue_action::DIRLIST:
        usblink_dirlist(action.remote.c_str(), dirlist_callback, action.user_data);
        break;
    case usblink_queue_action::MOVE:
        usblink_move(action.local.c_str(), action.remote.c_str(), progress_callback, action.user_data);
        break;
    case usblink_queue_action::NEW_DIR:
        usblink_new_dir(action.remote.c_str(), progress_callback, action.user_data);
        break;
    case usblink_queue_action::DEL_DIR:
        usblink_delete(action.remote.c_str(), true, progress_callback, action.user_data);
        break;
    case usblink_queue_action::DEL_FILE:
        usblink_delete(action.remote.c_str(), false, progress_callback, action.user_data);
        break;
    case usblink_queue_action::GET_FILE:
        if(!usblink_get_file(action.remote.c_str(), action.local.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy.store(false);
        }
    }
}

void usblink_queue_reset()
{
    while(true)
    {
        usblink_queue_action action;
        {
            std::lock_guard<std::mutex> lg(usblink_queue_mut);
            if(usblink_queue.empty())
                break;

            action = usblink_queue.front();
            usblink_queue.pop();
        }

        // Treat as error
        if(action.dirlist_callback)
            action.dirlist_callback(nullptr, true, action.user_data);
        else if(action.progress_callback)
            action.progress_callback(-1, action.user_data);
    }
    {
        std::lock_guard<std::mutex> lg(usblink_queue_mut);
        busy.store(false);
    }

    usblink_reset();
}

void usblink_queue_add(usblink_queue_action &action)
{
    {
        std::lock_guard<std::mutex> lg(usblink_queue_mut);
        usblink_queue.push(action);
    }

    if(!usblink_connected)
        usblink_connect();
}

void usblink_queue_delete(std::string path, bool is_dir, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = is_dir ? usblink_queue_action::DEL_DIR : usblink_queue_action::DEL_FILE;
    action.user_data = user_data;
    action.remote = path;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_dirlist(std::string path, usblink_dirlist_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::DIRLIST;
    action.user_data = user_data;
    action.remote = path;
    action.dirlist_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_download(std::string path, std::string destpath, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::GET_FILE;
    action.user_data = user_data;
    action.local = destpath;
    action.remote = path;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_put_file(std::string local, std::string remote, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::PUT_FILE;
    action.user_data = user_data;
    action.local = local;
    action.remote = remote;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_send_os(std::string filepath, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::SEND_OS;
    action.user_data = user_data;
    action.local = filepath;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_new_dir(std::string path, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::NEW_DIR;
    action.user_data = user_data;
    action.remote = path;
    action.progress_callback = callback,

    usblink_queue_add(action);
}

void usblink_queue_move(std::string old_path, std::string new_path, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::MOVE;
    action.user_data = user_data;
    action.local = old_path;
    action.remote = new_path;
    action.progress_callback = callback,

    usblink_queue_add(action);
}

unsigned int usblink_queue_size()
{
    std::lock_guard<std::mutex> lg(usblink_queue_mut);
    return usblink_queue.size();
}
