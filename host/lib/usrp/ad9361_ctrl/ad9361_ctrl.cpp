//
// Copyright 2012-2013 Ettus Research LLC
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

#include "ad9361_ctrl.hpp"
#include "ad9361_transaction.h"
#include <uhd/exception.hpp>
#include <uhd/utils/msg.hpp>
#include <cstring>

using namespace uhd;

struct ad9361_ctrl_impl : public ad9361_ctrl
{
    ad9361_ctrl_impl(ad9361_ctrl_iface_sptr iface):
        _iface(iface), _seq(0)
    {
        ad9361_transaction_t request;

        request.action = AD9361_ACTION_ECHO;
        this->do_transaction(request);

        request.action = AD9361_ACTION_INIT;
        this->do_transaction(request);
    }

    double set_gain(const std::string &which, const double value)
    {
        ad9361_transaction_t request;

        if (which == "RX1") request.action = AD9361_ACTION_SET_RX1_GAIN;
        if (which == "RX2") request.action = AD9361_ACTION_SET_RX2_GAIN;
        if (which == "TX1") request.action = AD9361_ACTION_SET_TX1_GAIN;
        if (which == "TX2") request.action = AD9361_ACTION_SET_TX2_GAIN;

        ad9361_trans_double_pack(value, request.value.gain);
        const ad9361_transaction_t reply = this->do_transaction(request);
        return ad9361_trans_double_unpack(reply.value.gain);
    }

    //! set a new clock rate, return the exact value
    double set_clock_rate(const double rate)
    {
        ad9361_transaction_t request;
        request.action = AD9361_ACTION_SET_CLOCK_RATE;
        ad9361_trans_double_pack(rate, request.value.rate);
        const ad9361_transaction_t reply = this->do_transaction(request);
        return ad9361_trans_double_unpack(reply.value.rate);
    }

    //! set which RX and TX chains/antennas are active
    void set_active_chains(bool tx1, bool tx2, bool rx1, bool rx2)
    {
        boost::uint32_t mask = 0;
        if (tx1) mask |= (1 << 0);
        if (tx2) mask |= (1 << 1);
        if (rx1) mask |= (1 << 2);
        if (rx2) mask |= (1 << 3);

        ad9361_transaction_t request;
        request.action = AD9361_ACTION_SET_ACTIVE_CHAINS;
        request.value.enable_mask = mask;
        this->do_transaction(request);
    }

    //! tune the given frontend, return the exact value
    double tune(const std::string &which, const double raw_value)
    {
        ad9361_transaction_t request;

        if (which[0] == 'R') request.action = AD9361_ACTION_SET_RX_FREQ;
        if (which[0] == 'T') request.action = AD9361_ACTION_SET_TX_FREQ;

        const double value = ad9361_ctrl::get_rf_freq_range().clip(raw_value);
        ad9361_trans_double_pack(value, request.value.freq);
        const ad9361_transaction_t reply = this->do_transaction(request);
        return ad9361_trans_double_unpack(reply.value.freq);
    }

    //! turn on/off Catalina's data port loopback
    void data_port_loopback(const bool on)
    {
        ad9361_transaction_t request;
        request.action = AD9361_ACTION_SET_CODEC_LOOP;
        request.value.codec_loop = on? 1 : 0;
        this->do_transaction(request);
    }

    ad9361_transaction_t do_transaction(const ad9361_transaction_t &request)
    {
        //declare in/out buffers
        unsigned char in_buff[64];
        unsigned char out_buff[64];

        //copy the input transaction
        std::memcpy(in_buff, &request, sizeof(request));
    
        //fill in other goodies
        ad9361_transaction_t *in = (ad9361_transaction_t *)in_buff;
        in->version = AD9361_TRANSACTION_VERSION;
        in->sequence = _seq++;

        //transact
        _iface->transact(in_buff, out_buff);
        ad9361_transaction_t *out = (ad9361_transaction_t *)out_buff;

        //sanity checks
        UHD_ASSERT_THROW(out->version == in->version);
        UHD_ASSERT_THROW(out->sequence == in->sequence);

        //handle errors
        const size_t len = strnlen(out->error_msg, AD9361_TRANSACTION_MAX_ERROR_MSG);
        const std::string error_msg(out->error_msg, len);
        if (not error_msg.empty()) throw uhd::runtime_error(error_msg);

        //return result done!
        return *out;
    }

    ad9361_ctrl_iface_sptr _iface;
    size_t _seq;

};


/***********************************************************************
 * Make an instance of the implementation
 **********************************************************************/
ad9361_ctrl::sptr ad9361_ctrl::make(ad9361_ctrl_iface_sptr iface)
{
    return sptr(new ad9361_ctrl_impl(iface));
}
