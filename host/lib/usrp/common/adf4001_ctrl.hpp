//
// Copyright 2013 Ettus Research LLC
//
// Original ADF4001 driver written by: bistromath
//                                     Mar 1, 2013
//
// Re-used and re-licensed with permission.
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

#ifndef INCLUDED_LIBUHD_USRP_COMMON_ADF4001_HPP
#define INCLUDED_LIBUHD_USRP_COMMON_ADF4001_HPP

#include "spi_core_3000.hpp"
#include <uhd/types/serial.hpp>
#include <boost/cstdint.hpp>

namespace uhd { namespace usrp {

class adf4001_regs_t {
public:

    /* Function prototypes */
    boost::uint32_t get_reg(boost::uint8_t addr);
    adf4001_regs_t(void);

    /* Register values / addresses */
    boost::uint16_t ref_counter; //14 bits
    boost::uint16_t n; //13 bits
    boost::uint8_t charge_pump_current_1; //3 bits
    boost::uint8_t charge_pump_current_2; //3 bits

    enum anti_backlash_width_t {
        ANTI_BACKLASH_WIDTH_2_9NS = 0,
        ANTI_BACKLASH_WIDTH_1_3NS = 1,
        ANTI_BACKLASH_WIDTH_6_0NS = 2,
        ANTI_BACKLASH_WIDTH_2_9NS_WAT = 3
    };
    anti_backlash_width_t anti_backlash_width;

    enum lock_detect_precision_t {
        LOCK_DETECT_PRECISION_3CYC = 0,
        LOCK_DETECT_PRECISION_5CYC = 1
    };
    lock_detect_precision_t lock_detect_precision;
    enum charge_pump_gain_t {
        CHARGE_PUMP_GAIN_1 = 0,
        CHARGE_PUMP_GAIN_2 = 1
    };
    charge_pump_gain_t charge_pump_gain;
    enum counter_reset_t {
        COUNTER_RESET_NORMAL = 0,
        COUNTER_RESET_RESET = 1
    };
    counter_reset_t    counter_reset;
    enum power_down_t {
        POWER_DOWN_NORMAL = 0,
        POWER_DOWN_ASYNC = 1,
        POWER_DOWN_SYNC = 3
    };
    power_down_t power_down;
    enum muxout_t {
        MUXOUT_TRISTATE_OUT = 0,
        MUXOUT_DLD = 1,
        MUXOUT_NDIV = 2,
        MUXOUT_AVDD = 3,
        MUXOUT_RDIV = 4,
        MUXOUT_NCH_OD_ALD = 5,
        MUXOUT_SDO = 6,
        MUXOUT_GND = 7
    };
    muxout_t muxout;
    enum phase_detector_polarity_t {
        PHASE_DETECTOR_POLARITY_NEGATIVE = 0,
        PHASE_DETECTOR_POLARITY_POSITIVE = 1
    };
    phase_detector_polarity_t phase_detector_polarity;
    enum charge_pump_mode_t {
        CHARGE_PUMP_NORMAL = 0,
        CHARGE_PUMP_TRISTATE = 1
    };
    charge_pump_mode_t charge_pump_mode;
    enum fastlock_mode_t {
        FASTLOCK_MODE_DISABLED = 0,
        FASTLOCK_MODE_1 = 1,
        FASTLOCK_MODE_2 = 2
    };
    fastlock_mode_t fastlock_mode;
    enum timer_counter_control_t {
        TIMEOUT_3CYC = 0,
        TIMEOUT_7CYC = 1,
        TIMEOUT_11CYC = 2,
        TIMEOUT_15CYC = 3,
        TIMEOUT_19CYC = 4,
        TIMEOUT_23CYC = 5,
        TIMEOUT_27CYC = 6,
        TIMEOUT_31CYC = 7,
        TIMEOUT_35CYC = 8,
        TIMEOUT_39CYC = 9,
        TIMEOUT_43CYC = 10,
        TIMEOUT_47CYC = 11,
        TIMEOUT_51CYC = 12,
        TIMEOUT_55CYC = 13,
        TIMEOUT_59CYC = 14,
        TIMEOUT_63CYC = 15,
    };
    timer_counter_control_t timer_counter_control;
};


class adf4001_ctrl {
public:

    adf4001_ctrl(spi_core_3000::sptr _spi, bool lock_to_ext_ref);
    void lock_to_ext_ref(void);
    bool locked(void);

private:
    spi_core_3000::sptr spi_iface;
    spi_config_t spi_config;
    adf4001_regs_t adf4001_regs;

    void program_regs(void);
    void write_reg(boost::uint8_t addr);
};

}}

#endif
