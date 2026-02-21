#include "emuthread.h"

#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <chrono>

#include <QDir>
#include <QEventLoop>
#include <QTimer>

#ifdef Q_OS_WINDOWS
    #include <time.h>
    #include <windows.h>
#endif

#include "core/debug/debug.h"
#include "core/debug/gdbstub.h"
#include "core/emu.h"
#include "core/usblink_queue.h"

namespace {

EmuThread *g_emu_thread = nullptr;

EmuThread &requireEmuThread()
{
    assert(g_emu_thread != nullptr);
    return *g_emu_thread;
}

} // namespace

EmuThread *emuThreadInstance()
{
    return g_emu_thread;
}

void gui_do_stuff(bool wait)
{
    requireEmuThread().doStuff(wait);
}

void gui_debug_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    gui_debug_vprintf(fmt, ap);

    va_end(ap);
}

void gui_debug_vprintf(const char *fmt, va_list ap)
{
    requireEmuThread().debugStr(QString::vasprintf(fmt, ap));
}

void gui_nlog_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    requireEmuThread().nlogStr(QString::vasprintf(fmt, ap));
    va_end(ap);
}

void gui_status_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    requireEmuThread().statusMsg(QString::vasprintf(fmt, ap));
    va_end(ap);
}

void gui_perror(const char *msg)
{
    gui_debug_printf("%s: %s\n", msg, strerror(errno));
}

void gui_debugger_entered_or_left(bool entered)
{
    requireEmuThread().debuggerEntered(entered);
}

static debug_input_cb debug_callback;

void gui_debugger_request_input(debug_input_cb callback)
{
    debug_callback = callback;
    requireEmuThread().debugInputRequested(callback != nullptr);
}

void gui_putchar(char c)
{
    requireEmuThread().serialChar(c);
}

int gui_getchar()
{
    // Serial input not implemented -- would need a queue fed from the Serial tab
    return -1;
}

void gui_set_busy(bool busy)
{
    emit requireEmuThread().isBusy(busy);
}

void gui_show_speed(double d)
{
    emit requireEmuThread().speedChanged(d);
}

void gui_usblink_changed(bool state)
{
    emit requireEmuThread().usblinkChanged(state);
}

void gui_lcd_frame_ready(void)
{
    emit requireEmuThread().lcdFrameReady();
}

void throttle_timer_off()
{
    requireEmuThread().setTurboMode(true);
}

void throttle_timer_on()
{
    requireEmuThread().setTurboMode(false);
}

void throttle_timer_wait(unsigned int usec)
{
    requireEmuThread().throttleTimerWait(usec);
}

EmuThread::EmuThread(QObject *parent) :
    QThread(parent)
{
    assert(g_emu_thread == nullptr);
    g_emu_thread = this;

    // Set default settings
    debug_on_start = debug_on_warn = false;
}

EmuThread::~EmuThread()
{
    if (g_emu_thread == this)
        g_emu_thread = nullptr;
}

//Called occasionally, only way to do something in the same thread the emulator runs in.
void EmuThread::doStuff(bool wait)
{
    do
    {
        if(do_suspend)
        {
            bool success = emu_suspend(snapshot_path.c_str());
            do_suspend = false;
            emit suspended(success);
        }

        if(enter_debugger)
        {
            setPaused(false);
            enter_debugger = false;
            if(!in_debugger)
                debugger(DBG_USER, 0);
        }

        if(is_paused && wait)
            msleep(100);

    } while(is_paused && wait);
}

void EmuThread::run()
{
    setTerminationEnabled();

    path_boot1 = QDir::toNativeSeparators(boot1).toStdString();
    path_flash = QDir::toNativeSeparators(flash).toStdString();

    bool do_reset = !do_resume;
    bool success = emu_start(port_gdb, port_rdbg, do_resume ? snapshot_path.c_str() : nullptr);

    if(do_resume)
        emit resumed(success);
    else
        emit started(success);

    do_resume = false;

    if(success)
        emu_loop(do_reset);

    emit stopped();
}

void EmuThread::throttleTimerWait(unsigned int usec)
{
    if(usec <= 1)
        return;

    #ifdef Q_OS_WINDOWS
        // QThread::usleep uses Sleep, which may sleep up to ~32ms more!
        // Use nanosleep inside a timeBeginPeriod/timeEndPeriod block for accuracy.
        timeBeginPeriod(10);
        struct timespec ts{};
        ts.tv_nsec = usec * 1000;
        nanosleep(&ts, nullptr);
        timeEndPeriod(10);
    #else
        QThread::usleep(usec);
    #endif
}

void EmuThread::setTurboMode(bool enabled)
{
    turbo_mode = enabled;
    emit turboModeChanged(enabled);
}

void EmuThread::toggleTurbo()
{
    setTurboMode(!turbo_mode);
}

void EmuThread::enterDebugger()
{
    enter_debugger = true;
}

void EmuThread::debuggerInput(QString str)
{
    debug_input = str.toStdString();
    if (gdb_connected && gdbstub_queue_local_command(debug_input.c_str()))
        return;
    if (debug_callback)
        debug_callback(debug_input.c_str());
}

void EmuThread::setPaused(bool paused)
{
    this->is_paused = paused;
    emit this->paused(paused);
}

bool EmuThread::stop()
{
    if(!isRunning())
        return true;

    exiting = true;
    setPaused(false);
    do_suspend = false;

    // Cause the cpu core to leave the loop and check for events
    cycle_count_delta = 0;

    if(!this->wait(200))
    {
        terminate();
        if(!this->wait(200))
            return false;
    }

    emu_cleanup();
    return true;
}

void EmuThread::reset()
{
    usblink_queue_reset();
    emu_request_reset_hard();
}

bool EmuThread::resume(QString path)
{
    snapshot_path = QDir::toNativeSeparators(path).toStdString();
    do_resume = true;
    if(!stop())
        return false;

    start();
    return true;
}

void EmuThread::suspend(QString path)
{
    snapshot_path = QDir::toNativeSeparators(path).toStdString();
    do_suspend = true;
}
