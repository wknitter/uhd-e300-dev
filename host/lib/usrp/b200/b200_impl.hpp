// // Copyright 2012 Ettus Research LLC
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

#ifndef INCLUDED_B200_IMPL_HPP
#define INCLUDED_B200_IMPL_HPP

#include "b200_iface.hpp"
#include "b200_ctrl.hpp"
#include "b200_codec_ctrl.hpp"
#include "rx_frontend_core_200.hpp"
#include "tx_frontend_core_200.hpp"
#include "rx_dsp_core_200.hpp"
#include "tx_dsp_core_200.hpp"
#include "time64_core_200.hpp"
#include "gpio_core_200.hpp"
#include "user_settings_core_200.hpp"
#include "recv_packet_demuxer.hpp"
#include <uhd/device.hpp>
#include <uhd/property_tree.hpp>
#include <uhd/utils/pimpl.hpp>
#include <uhd/types/dict.hpp>
#include <uhd/types/sensors.hpp>
#include <uhd/types/clock_config.hpp>
#include <uhd/types/stream_cmd.hpp>
#include <uhd/usrp/mboard_eeprom.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/transport/usb_zero_copy.hpp>
#include <boost/weak_ptr.hpp>

static const std::string     B200_FW_FILE_NAME = "usrp_b200_fw.ihx";
static const std::string     B200_FPGA_FILE_NAME = "usrp_b200_fpga.bin";
static const boost::uint16_t B200_FW_COMPAT_NUM = 0x03;
static const boost::uint16_t B200_FPGA_COMPAT_NUM = 0x09;
static const size_t          B200_MAX_PKT_BYTE_LIMIT = 2048;
static const double          B200_LINK_RATE_BPS = 256e6/5; //pratical link rate (< 480 Mbps) //TODO for USB3
static const boost::uint32_t B200_ASYNC_SID_BASE = 10;
static const boost::uint32_t B200_CTRL_MSG_SID = 20;
static const boost::uint32_t B200_RX_SID_BASE = 30;
static const boost::uint32_t B200_TX_SID_BASE = 40;
static const size_t          B200_NUM_RX_FE = 2;
static const size_t          B200_NUM_TX_FE = 2;

/* ATR GPIO TX Output Settings */
static const boost::uint32_t LED_TXRX_TX = (1 << 16);
static const boost::uint32_t LED_TXRX_RX = (1 << 17);
static const boost::uint32_t LED_RX = (1 << 18);
static const boost::uint32_t SRX_TX = (1 << 19);
static const boost::uint32_t SRX_RX = (1 << 20);
static const boost::uint32_t SFDX_TX = (1 << 21);
static const boost::uint32_t SFDX_RX = (1 << 22);
static const boost::uint32_t TX_ENABLE = (1 << 23);

static const boost::uint32_t STATE_OFF = 0x00;
static const boost::uint32_t STATE_TX = (LED_TXRX_TX | SFDX_TX | TX_ENABLE);
static const boost::uint32_t STATE_RX_ON_TXRX = (LED_TXRX_RX | SRX_TX | SRX_RX);
static const boost::uint32_t STATE_RX_ON_RX2 = (LED_RX | SFDX_RX);
static const boost::uint32_t STATE_FDX = (LED_TXRX_TX | LED_RX | SFDX_TX 
                                  | SFDX_RX | TX_ENABLE);

/* ATR GPIO RX Output Settings */
//FIXME -- What do these do?
static const boost::uint32_t CODEC_CTRL_IN = 0x0F;
static const boost::uint32_t CODEC_EN_AGC = (1 << 4);
static const boost::uint32_t CODEC_TXRX = (1 << 5);

//! Implementation guts
class b200_impl : public uhd::device {
public:
    //structors
    b200_impl(const uhd::device_addr_t &);
    ~b200_impl(void);

    //the io interface
    uhd::rx_streamer::sptr get_rx_stream(const uhd::stream_args_t &args);
    uhd::tx_streamer::sptr get_tx_stream(const uhd::stream_args_t &args);
    bool recv_async_msg(uhd::async_metadata_t &, double);

private:
    uhd::property_tree::sptr _tree;

    //controllers
    b200_iface::sptr _iface;
    b200_ctrl::sptr _ctrl;
    b200_codec_ctrl::sptr _codec_ctrl;
    std::vector<rx_frontend_core_200::sptr> _rx_fes;
    std::vector<tx_frontend_core_200::sptr> _tx_fes;
    std::vector<rx_dsp_core_200::sptr> _rx_dsps;
    std::vector<tx_dsp_core_200::sptr> _tx_dsps;
    time64_core_200::sptr _time64;
    user_settings_core_200::sptr _user;
    gpio_core_200_32wo::sptr _atr0;
    gpio_core_200_32wo::sptr _atr1;

    //transports
    uhd::transport::zero_copy_if::sptr _data_transport;
    uhd::transport::zero_copy_if::sptr _ctrl_transport;
    uhd::usrp::recv_packet_demuxer::sptr _rx_demux;

    //device properties interface
    uhd::property_tree::sptr get_tree(void) const{
        return _tree;
    }

    std::vector<boost::weak_ptr<uhd::rx_streamer> > _rx_streamers;
    std::vector<boost::weak_ptr<uhd::tx_streamer> > _tx_streamers;

    void set_mb_eeprom(const uhd::usrp::mboard_eeprom_t &);
    void check_fw_compat(void);
    void check_fpga_compat(void);
    void update_rates(void);
    void update_rx_subdev_spec(const uhd::usrp::subdev_spec_t &);
    void update_tx_subdev_spec(const uhd::usrp::subdev_spec_t &);
    void update_rx_samp_rate(const size_t, const double rate);
    void update_tx_samp_rate(const size_t, const double rate);
    void update_clock_source(const std::string &);
    void update_tick_rate(const double rate);

    struct gpio_state{
        boost::uint32_t ext_ref_enable, dac_shdn, pps_fpga_out_enable, pps_gps_out_enable, gps_out_enable, gps_ref_enable;
        boost::uint32_t tx_bandsel_a, tx_bandsel_b, rx_bandsel_a, rx_bandsel_b, rx_bandsel_c;
        boost::uint32_t mimo;
        boost::uint32_t LED_RX1, LED_RX2, LED_TXRX1_RX, LED_TXRX1_TX, LED_TXRX2_RX, LED_TXRX2_TX;
        boost::uint32_t tx_enable1, tx_enable2;
        boost::uint32_t SFDX2_RX, SFDX2_TX, SRX2_RX, SRX2_TX, SFDX1_RX, SFDX1_TX, SRX1_RX, SRX1_TX;
        boost::uint32_t codec_txrx, codec_en_agc, codec_ctrl_in;
    } _gpio_state;

    void update_gpio_state(void);
};

#endif /* INCLUDED_B200_IMPL_HPP */
