#include "libftdi_serial.h"
#include "serial.h"


#ifdef USE_LIBFTDI
#include <chrono>
#include <ftdi.h>

#include "board.h"

//////
// libusb forward declarations
//////

#if defined(_WIN32) || defined(__CYGWIN__)
#define LIBUSB_CALL WINAPI
#else
#define LIBUSB_CALL
#endif /* _WIN32 || __CYGWIN__ */
extern "C"
{
    struct libusb_version
    {
        const uint16_t major, minor, micro, nano;
    };
    const struct libusb_version *LIBUSB_CALL libusb_get_version ();
    enum libusb_option
    {
        LIBUSB_OPTION_WEAK_AUTHORITY = 2,
        LIBUSB_OPTION_ANDROID_JNIENV = 9997
    };
    int LIBUSB_CALL libusb_set_option (struct libusb_context *ctx, enum libusb_option option, ...);
}

//////
// end libusb declarations
//////

LibFTDISerial::LibFTDISerial (const char *description, Board *board)
    : description (description), port_open (false), board (board)
{
    // convert the libusb version to a uint64_t for version checking
    libusb_version const &verobj = *libusb_get_version ();
    uint64_t usbver = 0;
    for (uint64_t verpart : {verobj.major, verobj.minor, verobj.micro, verobj.nano})
    {
        usbver = (usbver << 16) | verpart;
    }

    // libusb_set_option was officially introduced in 1.0.22
    if (usbver >= 0x00001000001600)
    {
        // on android, this disables device scan during usb_init, which lets it succeed
        libusb_set_option (ftdi.usb_ctx, LIBUSB_OPTION_WEAK_AUTHORITY, nullptr);

        if (board != nullptr)
        {
            log_error ("LibFTDISerial", "looking for jnienv");
            if (board->params.platform_ptr != nullptr)
            {
                log_error ("LibFTDISerial", "jnienv pointer set, passing");
                // this is a prototype option for passing a JNIEnv pointer it.
                // libusb will just return an error code if it doesn't recognise it
                libusb_set_option (ftdi.usb_ctx, LIBUSB_OPTION_ANDROID_JNIENV, board->params.platform_ptr, nullptr);
            }
            else
            {
                log_error ("LibFTDISerial", "jnienv pointer not set");
            }
        }
    }
    else
    {
        log_error ("LibFTDISerial", "usb version is too old to set options");
    }

    // setup libftdi
    if (ftdi_init (&ftdi) != 0)
    {
        log_error ("LibFTDISerial");
    }
}

LibFTDISerial::~LibFTDISerial ()
{
    if (port_open)
    {
        ftdi_usb_close (&ftdi);
    }
    ftdi_deinit (&ftdi);
}

bool LibFTDISerial::is_libftdi (const char *port_name)
{
    LibFTDISerial serial (port_name);
    int open_result = ftdi_usb_open_string (&serial.ftdi, port_name);
    if (open_result == 0)
    {
        ftdi_usb_close (&serial.ftdi);
    }
    else if (open_result == -12)
    {
        // failed to init libftdi; do a manual check
        if (port_name[0] == 0 || port_name[0] == '/')
        {
            return false;
        }
        return port_name[1] == ':';
    }
    return open_result != -11;
}

void LibFTDISerial::log_error (const char *action, const char *message)
{
    if (board != nullptr)
    {
        if (message == nullptr)
        {
            message = ftdi_get_error_string (&ftdi);
        }
        board->safe_logger (
            spdlog::level::err, "libftdi {}: {} -> {}", description, action, message);
    }
}

int LibFTDISerial::open_serial_port ()
{
    // https://www.intra2net.com/en/developer/libftdi/documentation/ftdi_8c.html#aae805b82251a61dae46b98345cd84d5c
    switch (ftdi_usb_open_string (&ftdi, description.c_str ()))
    {
        case 0:
            port_open = true;
            return (int)SerialExitCodes::OK;
        case -10:
            log_error ("open_serial_port ()");
            return (int)SerialExitCodes::CLOSE_ERROR;
        default:
            log_error ("open_serial_port ()");
            return (int)SerialExitCodes::OPEN_PORT_ERROR;
    }
}

bool LibFTDISerial::is_port_open ()
{
    return port_open;
}

int LibFTDISerial::set_serial_port_settings (int ms_timeout, bool timeout_only)
{
    int result;
    if (!timeout_only)
    {
        result = ftdi_set_line_property (&ftdi, BITS_8, STOP_BIT_1, NONE);
        if (result != 0)
        {
            log_error ("set_serial_port_settings");
            return (int)SerialExitCodes::SET_PORT_STATE_ERROR;
        }
        result = set_custom_baudrate (115200);
        if (result != (int)SerialExitCodes::OK)
        {
            return result;
        }
        result = ftdi_setdtr_rts (&ftdi, 1, 1);
        result |= ftdi_setflowctrl (&ftdi, SIO_DISABLE_FLOW_CTRL);
        if (result != 0)
        {
            // -1 setting failed, -2 usb device unavailable
            log_error ("set_serial_port_settings");
            return (int)SerialExitCodes::SET_PORT_STATE_ERROR;
        }
    }

    ftdi.usb_read_timeout = ms_timeout;

    return (int)SerialExitCodes::OK;
}

int LibFTDISerial::set_custom_baudrate (int baudrate)
{
    switch (ftdi_set_baudrate (&ftdi, baudrate))
    {
        case 0:
            return (int)SerialExitCodes::OK;
        case -3: // usb device unavailable
            log_error ("set_custom_baudrate");
            return (int)SerialExitCodes::OPEN_PORT_ERROR;
        default: // -1 invalid baudrate, -2 setting baudrate failed
            log_error ("set_custom_baudrate");
            return (int)SerialExitCodes::SET_PORT_STATE_ERROR;
    }
}

int LibFTDISerial::flush_buffer ()
{
#if FTDI_MAJOR_VERSION > 2 || (FTDI_MAJOR_VERSION == 1 && FTDI_MINOR_VERSIOM >= 5)
    // correct tcflush was added in libftdi 1.5
    switch (ftdi_tcioflush (&ftdi))
    {
        case 0:
            return (int)SerialExitCodes::OK;
        case -3: // usb device unavailable
            log_error ("flush_buffer ()");
            return (int)SerialExitCodes::OPEN_PORT_ERROR;
        default: // -1,-2 chip failed to purge a buffer
            log_error ("flush_buffer ()");
    }
#else
    log_error ("flush_buffer ()", "libftdi version <=1.4, tcflush unimplemented");
#endif
    return (int)SerialExitCodes::SET_PORT_STATE_ERROR;
}

int LibFTDISerial::read_from_serial_port (void *bytes_to_read, int size)
{
    // the ftdi will send us data after its latency (max 255ms, default
    // 16ms) times out, even if its buffer is empty, despite the usb
    // timeout.  libftdi does not enforce waiting for the usb timeout if
    // the chip responds, even if the chip responds with an empty buffer.
    // so, the read is repeated until the timeout is reached.

    // this latency behavior is documented in
    // http://www.ftdichip.com/Support/Documents/AppNotes/AN232B-04_DataLatencyFlow.pdf

    auto deadline =
        std::chrono::steady_clock::now () + std::chrono::milliseconds (ftdi.usb_read_timeout);
    int bytes_read = 0;
    while (bytes_read == 0 && size > 0 && std::chrono::steady_clock::now () < deadline)
    {
        bytes_read = ftdi_read_data (&ftdi, static_cast<unsigned char *> (bytes_to_read), size);
        // TODO: negative values are libusb error codes, -666 means usb device unavailable
        if (bytes_read < 0)
        {
            log_error ("read_from_serial_port");
        }
    }

    return bytes_read;
}

int LibFTDISerial::send_to_serial_port (const void *message, int length)
{
    int bytes_written =
        ftdi_write_data (&ftdi, static_cast<unsigned const char *> (message), length);
    // TODO: negative values are libusb error codes, -666 means usb device unavailable
    if (bytes_written < 0)
    {
        log_error ("send_to_serial_port");
    }
    return bytes_written;
}

int LibFTDISerial::close_serial_port ()
{
    if (ftdi_usb_close (&ftdi) == 0)
    {
        port_open = false;
        return (int)SerialExitCodes::OK;
    }
    else
    {
        log_error ("close_serial_port ()");
        return (int)SerialExitCodes::CLOSE_ERROR;
    }
}

const char *LibFTDISerial::get_port_name ()
{
    return description.c_str ();
}

#else

LibFTDISerial::LibFTDISerial (const char *description, Board *board)
{
}

LibFTDISerial::~LibFTDISerial ()
{
}

bool LibFTDISerial::is_libftdi (const char *port_name)
{
    return false;
}

int LibFTDISerial::open_serial_port ()
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

bool LibFTDISerial::is_port_open ()
{
    return false;
}

int LibFTDISerial::set_serial_port_settings (int ms_timeout, bool timeout_only)
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

int LibFTDISerial::set_custom_baudrate (int baudrate)
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

int LibFTDISerial::flush_buffer ()
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

int LibFTDISerial::read_from_serial_port (void *bytes_to_read, int size)
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

int LibFTDISerial::send_to_serial_port (const void *message, int length)
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

int LibFTDISerial::close_serial_port ()
{
    return (int)SerialExitCodes::NO_LIBFTDI_ERROR;
}

const char *LibFTDISerial::get_port_name ()
{
    return "";
}

#endif
