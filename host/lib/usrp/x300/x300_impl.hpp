//
// Copyright 2013 Ettus Research LLC
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

#ifndef INCLUDED_X300_IMPL_HPP
#define INCLUDED_X300_IMPL_HPP

#include <uhd/property_tree.hpp>
#include <uhd/device.hpp>
#include <uhd/usrp/mboard_eeprom.hpp>
#include <uhd/usrp/dboard_manager.hpp>
#include <uhd/usrp/dboard_eeprom.hpp>
#include <uhd/usrp/subdev_spec.hpp>
#include <uhd/types/sensors.hpp>
#include "x300_clock_ctrl.hpp"
#include "x300_fw_common.h"
#include <uhd/transport/udp_simple.hpp>
#include <uhd/utils/tasks.hpp>
#include "spi_core_3000.hpp"
#include "x300_adc_ctrl.hpp"
#include "x300_dac_ctrl.hpp"
#include "rx_vita_core_3000.hpp"
#include "tx_vita_core_3000.hpp"
#include "time_core_3000.hpp"
#include "rx_dsp_core_3000.hpp"
#include "tx_dsp_core_3000.hpp"
#include "i2c_core_100_wb32.hpp"
#include "radio_ctrl_core_3000.hpp"
#include "rx_frontend_core_200.hpp"
#include "gpio_core_200.hpp"
#include <boost/weak_ptr.hpp>
#include <uhd/usrp/gps_ctrl.hpp>
#include <uhd/transport/bounded_buffer.hpp>
#include <uhd/transport/nirio/niusrprio_session.h>
#include <uhd/transport/vrt_if_packet.hpp>
#include "recv_packet_demuxer_3000.hpp"

static const size_t X300_TX_FC_PKT_WINDOW = 2048; //16MB/8Kpkts
static const std::string X300_FW_FILE_NAME = "usrp_x300_fw.bin";
static const double X300_DEFAULT_TICK_RATE = 200e6;
static const double X300_DEFAULT_PLL2REF_FREQ = 96e6;
static const double X300_BUS_CLOCK_RATE = 175000000;
static const bool X300_ENABLE_RX_FC = false;
static const size_t X300_PCIE_DATA_FRAME_SIZE = 8192;   //bytes
static const size_t X300_PCIE_MSG_FRAME_SIZE  = 256;    //bytes

#define X300_RADIO_DEST_PREFIX_TX 0
#define X300_RADIO_DEST_PREFIX_CTRL 1
#define X300_RADIO_DEST_PREFIX_RX 2

#define X300_XB_DST_E0 0
#define X300_XB_DST_E1 1
#define X300_XB_DST_R0 2
#define X300_XB_DST_R1 3
#define X300_XB_DST_CE0 4
#define X300_XB_DST_CE1 5
#define X300_XB_DST_CE2 5
#define X300_XB_DST_PCI 7

#define X300_DEVICE_THERE 2
#define X300_DEVICE_HERE 0

//eeprom addrs for various boards
enum
{
    X300_DB0_RX_EEPROM = 0x5,
    X300_DB0_TX_EEPROM = 0x4,
    X300_DB0_GDB_EEPROM = 0x1,
    X300_DB1_RX_EEPROM = 0x7,
    X300_DB1_TX_EEPROM = 0x6,
    X300_DB1_GDB_EEPROM = 0x3,
};

struct x300_dboard_iface_config_t
{
    gpio_core_200::sptr gpio;
    spi_core_3000::sptr spi;
    size_t rx_spi_slaveno;
    size_t tx_spi_slaveno;
    i2c_core_100_wb32::sptr i2c;
    x300_clock_ctrl::sptr clock;
    x300_clock_which_t which_rx_clk;
    x300_clock_which_t which_tx_clk;
};

uhd::usrp::dboard_iface::sptr x300_make_dboard_iface(const x300_dboard_iface_config_t &);
uhd::uart_iface::sptr x300_make_uart_iface(uhd::wb_iface::sptr iface);

uhd::wb_iface::sptr x300_make_ctrl_iface_enet(uhd::transport::udp_simple::sptr udp);
uhd::wb_iface::sptr x300_make_ctrl_iface_pcie(nirio_interface::niriok_proxy& drv_proxy);

struct x300_impl : public uhd::device
{
    x300_impl(const uhd::device_addr_t &);
    void setup_mb(const size_t which, const uhd::device_addr_t &);
    ~x300_impl(void);

    //the io interface
    uhd::rx_streamer::sptr get_rx_stream(const uhd::stream_args_t &);
    uhd::tx_streamer::sptr get_tx_stream(const uhd::stream_args_t &);

    //support old async call
    typedef uhd::transport::bounded_buffer<uhd::async_metadata_t> async_md_type;
    boost::shared_ptr<async_md_type> _async_md;
    bool recv_async_msg(uhd::async_metadata_t &, double);

    uhd::property_tree::sptr _tree;
    //device properties interface
    uhd::property_tree::sptr get_tree(void) const
    {
        return _tree;
    }

    //perifs in the radio core
    struct radio_perifs_t
    {
        radio_ctrl_core_3000::sptr ctrl;
        spi_core_3000::sptr spi;
        x300_adc_ctrl::sptr adc;
        x300_dac_ctrl::sptr dac;
        time_core_3000::sptr time64;
        rx_vita_core_3000::sptr framer;
        rx_dsp_core_3000::sptr ddc;
        tx_vita_core_3000::sptr deframer;
        tx_dsp_core_3000::sptr duc;
        gpio_core_200_32wo::sptr leds;
        rx_frontend_core_200::sptr rx_fe;
    };

    //overflow recovery impl
    void handle_overflow(radio_perifs_t &perif, boost::weak_ptr<uhd::rx_streamer> streamer);

    //vector of member objects per motherboard
    struct mboard_members_t
    {
        uhd::dict<size_t, boost::weak_ptr<uhd::rx_streamer> > rx_streamers;
        uhd::dict<size_t, boost::weak_ptr<uhd::tx_streamer> > tx_streamers;

        uhd::task::sptr claimer_task;
        std::string addr;
        std::string xport_path;
        int router_dst_here;
        uhd::device_addr_t send_args;
        uhd::device_addr_t recv_args;
        bool if_pkt_is_big_endian;
        nifpga_interface::niusrprio_session::sptr  rio_fpga_interface;

        //perifs in the zpu
        uhd::wb_iface::sptr zpu_ctrl;
        spi_core_3000::sptr zpu_spi;
        i2c_core_100_wb32::sptr zpu_i2c;

        //perifs in each radio
        radio_perifs_t radio_perifs[2];
        uhd::usrp::dboard_eeprom_t db_eeproms[8];

        //per mboard frontend mapping
        uhd::usrp::subdev_spec_t rx_fe_map;
        uhd::usrp::subdev_spec_t tx_fe_map;

        //other perifs on mboard
        x300_clock_ctrl::sptr clock;
        uhd::gps_ctrl::sptr gps;
        gpio_core_200::sptr fp_gpio;

        //clock control register bits
        int clock_control_regs__clock_source;
        int clock_control_regs__pps_select;
        int clock_control_regs__pps_out_enb;
        int clock_control_regs__tcxo_enb;
        int clock_control_regs__gpsdo_pwr;
    };
    std::vector<mboard_members_t> _mb;

    //task for periodically reclaiming the device from others
    void claimer_loop(uhd::wb_iface::sptr);
    static bool is_claimed(uhd::wb_iface::sptr);

    boost::mutex _transport_setup_mutex;

    void register_loopback_self_test(uhd::wb_iface::sptr iface);

    void setup_radio(const size_t, const size_t which_radio, const std::string &db_name);

    size_t _sid_framer;
    struct sid_config_t
    {
        boost::uint8_t router_addr_there;
        boost::uint8_t dst_prefix; //2bits
        boost::uint8_t router_dst_there;
        boost::uint8_t router_dst_here;
    };
    boost::uint32_t allocate_sid(mboard_members_t &mb, const sid_config_t &config);

    struct both_xports_t
    {
        uhd::transport::zero_copy_if::sptr recv;
        uhd::transport::zero_copy_if::sptr send;
    };
    both_xports_t make_transport(
        const size_t mb_index,
        const uint8_t& destination,
        const uint8_t& prefix,
        const uhd::device_addr_t& args,
        boost::uint32_t& sid);

    ////////////////////////////////////////////////////////////////////
    //
    //Caching for transport interface re-use -- like sharing a DMA.
    //The cache is optionally used by make_transport by use-case.
    //The cache maps an ID string to a transport-ish object.
    //The ID string identifies a purpose for the transport.
    //
    //For recv, there is a demux cache, which maps a ID string
    //to a recv demux object. When a demux is used, the underlying transport
    //must never be used outside of the demux. Use demux->make_proxy(sid).
    //
    uhd::dict<std::string, uhd::usrp::recv_packet_demuxer_3000::sptr> _demux_cache;
    //
    //For send, there is a shared send xport, which maps an ID string
    //to a transport capable of sending buffers. Send transports
    //can be shared amongst multiple callers, unlike recv.
    //
    uhd::dict<std::string, uhd::transport::zero_copy_if::sptr> _send_cache;
    //
    ////////////////////////////////////////////////////////////////////

    uhd::dict<std::string, uhd::usrp::dboard_manager::sptr> _dboard_managers;
    uhd::dict<std::string, uhd::usrp::dboard_iface::sptr> _dboard_ifaces;

    void set_rx_fe_corrections(const uhd::fs_path &mb_path, const std::string &fe_name, const double lo_freq);

    void update_rx_subdev_spec(const size_t, const uhd::usrp::subdev_spec_t &spec);
    void update_tx_subdev_spec(const size_t, const uhd::usrp::subdev_spec_t &spec);

    void set_tick_rate(mboard_members_t &, const double);
    void update_tick_rate(mboard_members_t &, const double);
    void update_rx_samp_rate(mboard_members_t&, const size_t, const double);
    void update_tx_samp_rate(mboard_members_t&, const size_t, const double);

    void update_clock_control(mboard_members_t&);
    void set_time_source_out(mboard_members_t&, const bool);
    void update_clock_source(mboard_members_t&, const std::string &);
    void update_time_source(mboard_members_t&, const std::string &);

    uhd::sensor_value_t get_ref_locked(uhd::wb_iface::sptr);

    void set_db_eeprom(uhd::i2c_iface::sptr i2c, const size_t, const uhd::usrp::dboard_eeprom_t &);
    void set_mb_eeprom(uhd::i2c_iface::sptr i2c, const uhd::usrp::mboard_eeprom_t &);

    void check_fw_compat(const uhd::fs_path &mb_path, uhd::wb_iface::sptr iface);
    void check_fpga_compat(const uhd::fs_path &mb_path, uhd::wb_iface::sptr iface);

    void update_atr_leds(gpio_core_200_32wo::sptr, const std::string &ant);
    boost::uint64_t get_fp_gpio(gpio_core_200::sptr, const std::string &);
    void set_fp_gpio(const uhd::fs_path &mb_path, gpio_core_200::sptr, const std::string &, const boost::uint64_t);
};

#endif /* INCLUDED_X300_IMPL_HPP */