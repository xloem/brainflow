#include "libftdi_serial.h"
#include "serial.h"


#ifdef USE_LIBFTDI
#include <chrono>
#include <ftdi.h>

#include "board.h"

#if defined(__ANDROID__)
static void android_ensure_libusb_init ();
#endif

LibFTDISerial::LibFTDISerial (const char *description, Board *board)
    : description (description), port_open (false), lib_init (false), board (board)
{
#if defined(__ANDROID__)
    android_ensure_libusb_init ();
#endif

    // setup libftdi
    if (ftdi_init (&ftdi) == 0)
    {
        lib_init = true;
    }
    else
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
    if (lib_init)
    {
        ftdi_deinit (&ftdi);
    }
}

bool LibFTDISerial::is_libftdi (const char *port_name)
{
    struct ftdi_context ftdi;
    int init_result = ftdi_init (&ftdi);
    int open_result;
    if (init_result != 0)
    {
        // failed to init libftdi; do a manual check
        if (port_name[0] == 0 || port_name[0] == '/')
        {
            return false;
        }
        return port_name[1] == ':';
    }
    open_result = ftdi_usb_open_string (&ftdi, port_name);
    if (open_result == 0)
    {
        ftdi_usb_close (&ftdi);
    }
    ftdi_deinit (&ftdi);
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

#if defined(__ANDROID__)
//////
// on android we can pass the jnienv pointer to libusb
//////

extern "C"
{
    // forward declarations from libusb.h
    struct libusb_version
    {
        const uint16_t major, minor, micro, nano;
    };
    enum libusb_option
    {
        LIBUSB_OPTION_WEAK_AUTHORITY = 2,
        LIBUSB_OPTION_ANDROID_JNIENV = 3
    };
    const struct libusb_version *libusb_get_version ();
    int libusb_set_option (struct libusb_context *ctx, enum libusb_option option, ...);
}

void android_ensure_libusb_init ()
{
    // constructed on first call
    class Init
    {
    public:
        Init ()
        {
            libusb_version const &ver_obj = *libusb_get_version ();
            uint64_t usb_version = 0;
            for (uint64_t ver_part : {ver_obj.major, ver_obj.minor, ver_obj.micro, ver_obj.nano})
            {
                usb_version = (usb_version << 16) | ver_part;
            }

            // libusb_set_option was officially introduced in 1.0.22
            if (usb_version >= 0x0001000000160000)
            {
                jnienv_set = false;
                // on android, this disables device scan during usb_init, which lets it succeed
                libusb_set_option (nullptr, LIBUSB_OPTION_WEAK_AUTHORITY, nullptr);
            }
            else
            {
                // libusb is too old to pass options in
                jnienv_set = true;
            }
        }
        bool jnienv_set;
    } static init;

    if (!init.jnienv_set && Board::java_jnienv != nullptr)
    {
        init.jnienv_set = true;
        // pass jnienv pointer to libusb
        int r =
            libusb_set_option (nullptr, LIBUSB_OPTION_ANDROID_JNIENV, Board::java_jnienv, nullptr);
        if (r == 0)
        {
            // disable weak authority
            libusb_set_option (nullptr, LIBUSB_OPTION_WEAK_AUTHORITY, -1, nullptr);
        }
    }
}
#endif // defined(__ANDROID__)

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
