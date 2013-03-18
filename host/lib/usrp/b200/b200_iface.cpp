//
// Copyright 2012 Ettus Research LLC
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "b200_iface.hpp"

#include <uhd/utils/msg.hpp>
#include <uhd/exception.hpp>
#include <boost/functional/hash.hpp>
#include <boost/thread/thread.hpp>
#include <boost/cstdint.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#include <iomanip>
#include <libusb.h>

using namespace uhd;
using namespace uhd::transport;

static const bool load_img_msg = true;

const static boost::uint8_t FX3_FIRMWARE_LOAD = 0xA0;
const static boost::uint8_t VRT_VENDOR_OUT = (LIBUSB_REQUEST_TYPE_VENDOR
                                              | LIBUSB_ENDPOINT_OUT);
const static boost::uint8_t VRT_VENDOR_IN = (LIBUSB_REQUEST_TYPE_VENDOR
                                             | LIBUSB_ENDPOINT_IN);
const static boost::uint8_t B200_VREQ_FPGA_START = 0x02;
const static boost::uint8_t B200_VREQ_FPGA_DATA = 0x12;
const static boost::uint8_t B200_VREQ_SET_FPGA_HASH = 0x1C;
const static boost::uint8_t B200_VREQ_GET_FPGA_HASH = 0x1D;
const static boost::uint8_t B200_VREQ_SET_FW_HASH = 0x1E;
const static boost::uint8_t B200_VREQ_GET_FW_HASH = 0x1F;
const static boost::uint8_t B200_VREQ_LOOP = 0x22;
const static boost::uint8_t B200_VREQ_SPI_WRITE = 0x32;
const static boost::uint8_t B200_VREQ_SPI_READ = 0x42;
const static boost::uint8_t B200_VREQ_FPGA_RESET = 0x62;
const static boost::uint8_t B200_VREQ_GPIF_RESET = 0x72;
const static boost::uint8_t B200_VREQ_GET_USB = 0x80;
const static boost::uint8_t B200_VREQ_FX3_RESET = 0x99;

typedef boost::uint32_t hash_type;


/***********************************************************************
 * Helper Functions
 **********************************************************************/
/*!
 * Create a file hash
 * The hash will be used to identify the loaded firmware and fpga image
 * \param filename file used to generate hash value
 * \return hash value in a size_t type
 */
static hash_type generate_hash(const char *filename)
{
    std::ifstream file(filename);
    if (not file){
        throw uhd::io_error(std::string("cannot open input file ") + filename);
    }

    size_t hash = 0;

    char ch;
    while (file.get(ch)) {
        boost::hash_combine(hash, ch);
    }

    if (not file.eof()){
        throw uhd::io_error(std::string("file error ") + filename);
    }

    file.close();
    return hash_type(hash);
}


/*!
 * Verify checksum of a Intel HEX record
 * \param record a line from an Intel HEX file
 * \return true if record is valid, false otherwise
 */
bool checksum(std::string *record) {

    size_t len = record->length();
    unsigned int i;
    unsigned char sum = 0;
    unsigned int val;

    for (i = 1; i < len; i += 2) {
        std::istringstream(record->substr(i, 2)) >> std::hex >> val;
        sum += val;
    }

    if (sum == 0)
       return true;
    else
       return false;
}


/*!
 * Parse Intel HEX record
 *
 * \param record a line from an Intel HEX file
 * \param len output length of record
 * \param addr output address
 * \param type output type
 * \param data output data
 * \return true if record is sucessfully read, false on error
 */
bool parse_record(std::string *record, boost::uint16_t &len, \
        boost::uint16_t &addr, boost::uint16_t &type, unsigned char* data) {

    unsigned int i;
    std::string _data;
    unsigned int val;

    if (record->substr(0, 1) != ":")
        return false;

    std::istringstream(record->substr(1, 2)) >> std::hex >> len;
    std::istringstream(record->substr(3, 4)) >> std::hex >> addr;
    std::istringstream(record->substr(7, 2)) >> std::hex >> type;

    for (i = 0; i < len; i++) {
        std::istringstream(record->substr(9 + 2 * i, 2)) >> std::hex >> val;
        data[i] = (unsigned char) val;
    }

    return true;
}


/***********************************************************************
 * The implementation class
 **********************************************************************/
class b200_iface_impl : public b200_iface{
public:

    b200_iface_impl(usb_control::sptr usb_ctrl):
        _usb_ctrl(usb_ctrl)
    {
        //NOP
    }


    int fx3_control_write(boost::uint8_t request,
                           boost::uint16_t value,
                           boost::uint16_t index,
                           unsigned char *buff,
                           boost::uint16_t length,
                           boost::int32_t timeout = 0)
    {
        return _usb_ctrl->submit(VRT_VENDOR_OUT,        // bmReqeustType
                                   request,             // bRequest
                                   value,               // wValue
                                   index,               // wIndex
                                   buff,                // data
                                   length,              // wLength
                                   timeout);            // timeout
    }


    int fx3_control_read(boost::uint8_t request,
                           boost::uint16_t value,
                           boost::uint16_t index,
                           unsigned char *buff,
                           boost::uint16_t length,
                           boost::int32_t timeout = 0)
    {
        return _usb_ctrl->submit(VRT_VENDOR_IN,         // bmReqeustType
                                   request,             // bRequest
                                   value,               // wValue
                                   index,               // wIndex
                                   buff,                // data
                                   length,              // wLength
                                   timeout);            // timeout
    }


    void write_i2c(boost::uint8_t addr, const byte_vector_t &bytes)
    {
        //TODO
    }


    void transact_spi(
        unsigned char *tx_data,
        size_t num_tx_bits,
        unsigned char *rx_data,
        size_t num_rx_bits
    ){
        int ret = 0;
        boost::uint16_t tx_length = num_tx_bits / 8;

        if(tx_data[0] & 0x80) {
            ret = fx3_control_write(B200_VREQ_SPI_WRITE, 0x00, \
                    0x00, tx_data, tx_length);
        } else {
            ret = fx3_control_write(B200_VREQ_SPI_READ, 0x00, \
                    0x00, tx_data, tx_length);
        }

        if(ret < 0) {
            throw uhd::io_error("transact_spi: fx3_control_write failed!");
        }


        if(num_rx_bits) {
            boost::uint16_t total_length = num_rx_bits / 8;

            ret = fx3_control_read(B200_VREQ_LOOP, 0x00, \
                    0x00, rx_data, total_length);

            if(ret < 0) {
                throw uhd::io_error("transact_spi: readback failed!");
            }
        }
    }


    void write_reg(boost::uint16_t reg, boost::uint8_t val)
    {
        //std::cout << "SPIWrite\t" << std::hex << std::setw(3) << std::setfill('0') << (int) reg << "," << std::setw(2) << (int) val << std::endl;
        boost::uint8_t buf[3];
        buf[0] = (0x80 | ((reg >> 8) & 0x3F));
        buf[1] = (reg & 0x00FF);
        buf[2] = val;
        transact_spi(buf, 24, NULL, 0);
    }

    uint8_t read_reg(uint16_t reg) {
        boost::uint8_t buf[3];
        buf[0] = (reg >> 8) & 0x3F;
        buf[1] = (reg & 0x00FF);
        transact_spi(buf, 16, buf, 24);
        return buf[2];
    }


    byte_vector_t read_i2c(boost::uint8_t addr, size_t num_bytes)
    {
        //TODO
        return byte_vector_t();
    }


    void load_firmware(const std::string filestring, bool force = false)
    {
        const char *filename = filestring.c_str();

        /* Fields used in each USB control transfer. */
        boost::uint16_t len = 0;
        boost::uint16_t type = 0;
        boost::uint16_t lower_address_bits = 0x0000;
        unsigned char data[512];

        /* Can be set by the Intel HEX record 0x04, used for all 0x00 records
         * thereafter. Note this field takes the place of the 'index' parameter in
         * libusb calls, and is necessary for FX3's 32-bit addressing. */
        boost::uint16_t upper_address_bits = 0x0000;

        std::ifstream file;
        file.open(filename, std::ifstream::in);

        if(!file.good()) {
            throw uhd::io_error("fx3_load_firmware: cannot open firmware input file");
        }

        if (load_img_msg) UHD_MSG(status) << "Loading firmware image: " \
            << filestring << "..." << std::flush;

        while (!file.eof()) {
            boost::int32_t ret = 0;
            std::string record;
            file >> record;

            /* Check for valid Intel HEX record. */
            if (!checksum(&record) || !parse_record(&record, len, \
                        lower_address_bits, type, data)) {
                throw uhd::io_error("fx3_load_firmware: bad intel hex record checksum");
            }

            /* Type 0x00: Data. */
            if (type == 0x00) {
                ret = fx3_control_write(FX3_FIRMWARE_LOAD, \
                        lower_address_bits, upper_address_bits, data, len);

                if (ret < 0) {
                    throw uhd::io_error("usrp_load_firmware: usrp_control_write failed");
                }
            }

            /* Type 0x01: EOF. */
            else if (type == 0x01) {
                if (lower_address_bits != 0x0000 || len != 0 ) {
                    throw uhd::io_error("fx3_load_firmware: For EOF record, address must be 0, length must be 0.");
                }

                //TODO
                //usrp_set_firmware_hash(hash); //set hash before reset

                /* Successful termination! */
                file.close();

                /* Let the system settle. */
                boost::this_thread::sleep(boost::posix_time::milliseconds(1000));
                return;
            }

            /* Type 0x04: Extended Linear Address Record. */
            else if (type == 0x04) {
                if (lower_address_bits != 0x0000 || len != 2 ) {
                    throw uhd::io_error("fx3_load_firmware: For ELA record, address must be 0, length must be 2.");
                }

                upper_address_bits = ((boost::uint16_t)((data[0] & 0x00FF) << 8))\
                                     + ((boost::uint16_t)(data[1] & 0x00FF));
            }

            /* Type 0x05: Start Linear Address Record. */
            else if (type == 0x05) {
                if (lower_address_bits != 0x0000 || len != 4 ) {
                    throw uhd::io_error("fx3_load_firmware: For SLA record, address must be 0, length must be 4.");
                }

                /* The firmware load is complete.  We now need to tell the CPU
                 * to jump to an execution address start point, now contained within
                 * the data field.  Parse these address bits out, and then push the
                 * instruction. */
                upper_address_bits = ((boost::uint16_t)((data[0] & 0x00FF) << 8))\
                                     + ((boost::uint16_t)(data[1] & 0x00FF));
                lower_address_bits = ((boost::uint16_t)((data[2] & 0x00FF) << 8))\
                                     + ((boost::uint16_t)(data[3] & 0x00FF));

                fx3_control_write(FX3_FIRMWARE_LOAD, lower_address_bits, \
                        upper_address_bits, 0, 0);

                if (load_img_msg) UHD_MSG(status) << " done" << std::endl;
            }

            /* If we receive an unknown record type, error out. */
            else {
                throw uhd::io_error("fx3_load_firmware: unsupported record type.");
            }
        }

        /* There was no valid EOF. */
        throw uhd::io_error("fx3_load_firmware: No EOF record found.");
    }


    void reset_fx3(void) {
        unsigned char data[4];
        memset(data, 0x00, sizeof(data));

        fx3_control_write(B200_VREQ_FX3_RESET, 0x00, 0x00, data, 4);
    }

    void reset_gpif(void) {
        unsigned char data[4];
        memset(data, 0x00, sizeof(data));

        fx3_control_write(B200_VREQ_GPIF_RESET, 0x00, 0x00, data, 4);
    }

    void set_fpga_reset_pin(const bool reset)
    {
        unsigned char data[4];
        memset(data, (reset)? 0xFF : 0x00, sizeof(data));

        fx3_control_write(B200_VREQ_FPGA_RESET, 0x00, 0x00, data, 4);
    }

    boost::uint8_t get_usb_speed(void) {

        unsigned char rx_data[1];

        fx3_control_read(B200_VREQ_GET_USB, 0x00, 0x00, rx_data, 1);

        return boost::lexical_cast<boost::uint8_t>(rx_data[0]);
    }

    void usrp_get_firmware_hash(hash_type &hash) {
        fx3_control_read(B200_VREQ_GET_FW_HASH, 0x00, 0x00,
                (unsigned char*) &hash, 4, 500);
    }

    void usrp_set_firmware_hash(hash_type hash) {
        fx3_control_write(B200_VREQ_SET_FW_HASH, 0x00, 0x00,
                (unsigned char*) &hash, 4);
    }

    void usrp_get_fpga_hash(hash_type &hash) {
        fx3_control_read(B200_VREQ_GET_FPGA_HASH, 0x00, 0x00,
                (unsigned char*) &hash, 4, 500);
    }

    void usrp_set_fpga_hash(hash_type hash) {
        fx3_control_write(B200_VREQ_SET_FPGA_HASH, 0x00, 0x00,
                (unsigned char*) &hash, 4);
    }

    void load_fpga(const std::string filestring) {

        const char *filename = filestring.c_str();

        hash_type hash = generate_hash(filename);
        hash_type loaded_hash; usrp_get_fpga_hash(loaded_hash);
        if (hash == loaded_hash) return;

        std::ifstream file;
        file.open(filename, std::ios::in | std::ios::binary);

        if(!file.good()) {
            throw uhd::io_error("load_fpga: cannot open FPGA input file.");
        }

        if (load_img_msg) UHD_MSG(status) << "Loading FPGA image: " \
            << filestring << "..." << std::flush;
        boost::system_time next_dot = boost::get_system_time() + boost::posix_time::milliseconds(700);

        unsigned char out_buff[64];
        memset(out_buff, 0x00, sizeof(out_buff));
        fx3_control_write(B200_VREQ_FPGA_START, 0, 0, out_buff, 1, 1000);

        while(!file.eof()) {
            file.read((char *) out_buff, sizeof(out_buff));
            const std::streamsize n = file.gcount();
            if(n == 0) continue;

            boost::uint16_t transfer_count = boost::uint16_t(n);

            /* Send the data to the device. */
            fx3_control_write(B200_VREQ_FPGA_DATA, 0, 0, out_buff, transfer_count, 5000);

            if (boost::get_system_time() > next_dot)
            {
                if (load_img_msg) UHD_MSG(status) << "." << std::flush;
                next_dot = boost::get_system_time() + boost::posix_time::milliseconds(700);
            }

        }

        file.close();

        boost::this_thread::sleep(boost::posix_time::milliseconds(1000));

        usrp_set_fpga_hash(hash);

        if (load_img_msg) UHD_MSG(status) << " done" << std::endl;
    }

private:
    usb_control::sptr _usb_ctrl;
};

/***********************************************************************
 * Make an instance of the implementation
 **********************************************************************/
b200_iface::sptr b200_iface::make(usb_control::sptr usb_ctrl)
{
    return sptr(new b200_iface_impl(usb_ctrl));
}