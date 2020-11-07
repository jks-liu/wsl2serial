// serialpipe.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>


#include <Windows.h>

#include <fcntl.h>
#include <io.h>

std::string g_stdin2serial;
std::mutex g_stdin2serial_mutex;
std::condition_variable g_stdin2serial_cv;

void read_stdin()
{
    _setmode(_fileno(stdin), _O_BINARY);

    while (true) {
        int c = std::fgetc(stdin);
        if (c != EOF) {
            unsigned char b = c;

            const std::lock_guard<std::mutex> lock(g_stdin2serial_mutex);
            g_stdin2serial.push_back(b);
        }
        else {
            return;
        }

        g_stdin2serial_cv.notify_one();
    }
}


void write_serial(HANDLE serial)
{
    while (true) {
        std::unique_lock<std::mutex> lock(g_stdin2serial_mutex);
        g_stdin2serial_cv.wait(lock, [] {return !g_stdin2serial.empty(); });
        auto s = g_stdin2serial;
        g_stdin2serial.clear();
        lock.unlock();

        auto c_s = s.c_str();
        DWORD len = s.size();
        DWORD written = 0;

        while (written < len) {
            DWORD w;
            if (WriteFile(serial, c_s, len - written, &w, 0)) {
                written += w;
            }
            else {
                return;
            }
        }
    }
}

#if 0
std::string g_serial2stdout;
std::mutex g_serial2stdout_mutex;
std::condition_variable g_serial2stdout_cv;

void read_serial(HANDLE serial)
{
    _setmode(_fileno(stdout), _O_BINARY);

}
#endif


void serial2stdout(HANDLE serial)
{
    // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/setmode?view=msvc-160
    _setmode(_fileno(stdout), _O_BINARY);
    const DWORD buf_size = 1024;

    char buffer[buf_size] = { 0 };
    DWORD n_byte_read = 0;
    while (ReadFile(serial, buffer, buf_size, &n_byte_read, NULL)) {
        if (n_byte_read > 0) {
            if (std::fwrite(buffer, 1, n_byte_read, stdout) < n_byte_read) {
                return;
            }
        }
    }

    std::exit(6);
}


int main(int argc, char *argv[])
{
    if (argc != 3) {
        // Arg1 should be serial port name, sth. likes COM1
        // Arg2 should be baud rate
        return 1;
    }

    // \\.\COM1
    auto serial_path = std::string("\\\\.\\") + argv[1];

    HANDLE serial_handle = CreateFileA(
        serial_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        0,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        0);
    if (serial_handle == INVALID_HANDLE_VALUE) {
        return 2;
    }

    DCB serial_parameter = { 0 };
    serial_parameter.DCBlength = sizeof(serial_parameter);
    if (!GetCommState(serial_handle, &serial_parameter)) {
        return 3;
    }
    serial_parameter.BaudRate=std::atoi(argv[2]);
    serial_parameter.ByteSize=8;
    serial_parameter.StopBits=ONESTOPBIT;
    serial_parameter.Parity=NOPARITY;
    if(!SetCommState(serial_handle, &serial_parameter)){
        return 4;
    }

    // WriteFile is very slow before
    // This setting is very helpful
    // https://stackoverflow.com/questions/15752272/serial-communication-with-minimal-delay
    // https://stackoverflow.com/a/57781569/4144109
    COMMTIMEOUTS timeouts = { MAXDWORD,0,0,0,0 };
    if (!SetCommTimeouts(serial_handle, &timeouts)) {
        return 5;
    }

#if 0
    // https://web.archive.org/web/20180127160838/http://bd.eduweb.hhs.nl:80/micprg/pdf/serial-win.pdf
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;

    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;
#endif

    std::thread t1(serial2stdout, serial_handle);
    std::thread t2(read_stdin);
    std::thread t3(write_serial, serial_handle);

    t1.join();
    t2.join();
    t3.join();

    return 0;
}


