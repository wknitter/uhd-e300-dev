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

#include "b250_impl.hpp"
#include "b250_regs.hpp"
#include <uhd/usrp/dboard_iface.hpp>
#include <uhd/utils/safe_call.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/math/special_functions/round.hpp>
#include "ad7922_regs.hpp" //aux adc
#include "ad5623_regs.hpp" //aux dac

using namespace uhd;
using namespace uhd::usrp;
using namespace boost::assign;

class b250_dboard_iface : public dboard_iface
{
public:
    b250_dboard_iface(const b250_dboard_iface_config_t &config);
    ~b250_dboard_iface(void);

    special_props_t get_special_props(void)
    {
        special_props_t props;
        props.soft_clock_divider = false;
        props.mangle_i2c_addrs = false;
        return props;
    }

    void write_aux_dac(unit_t, aux_dac_t, double);
    double read_aux_adc(unit_t, aux_adc_t);

    void _set_pin_ctrl(unit_t, boost::uint16_t);
    void _set_atr_reg(unit_t, atr_reg_t, boost::uint16_t);
    void _set_gpio_ddr(unit_t, boost::uint16_t);
    void _set_gpio_out(unit_t, boost::uint16_t);

    void set_gpio_debug(unit_t, int);
    boost::uint16_t read_gpio(unit_t);

    void write_i2c(boost::uint8_t, const byte_vector_t &);
    byte_vector_t read_i2c(boost::uint8_t, size_t);

    void set_clock_rate(unit_t, double);
    double get_clock_rate(unit_t);
    std::vector<double> get_clock_rates(unit_t);
    void set_clock_enabled(unit_t, bool);
    double get_codec_rate(unit_t);

    void write_spi(
        unit_t unit,
        const spi_config_t &config,
        boost::uint32_t data,
        size_t num_bits
    );

    boost::uint32_t read_write_spi(
        unit_t unit,
        const spi_config_t &config,
        boost::uint32_t data,
        size_t num_bits
    );

    const b250_dboard_iface_config_t _config;
    uhd::dict<unit_t, ad5623_regs_t> _dac_regs;
    uhd::dict<unit_t, double> _clock_rates;
    void _write_aux_dac(unit_t);

};

/***********************************************************************
 * Make Function
 **********************************************************************/
dboard_iface::sptr b250_make_dboard_iface(const b250_dboard_iface_config_t &config)
{
    return dboard_iface::sptr(new b250_dboard_iface(config));
}

/***********************************************************************
 * Structors
 **********************************************************************/
b250_dboard_iface::b250_dboard_iface(const b250_dboard_iface_config_t &config):
    _config(config)
{
    //reset the aux dacs
    _dac_regs[UNIT_RX] = ad5623_regs_t();
    _dac_regs[UNIT_TX] = ad5623_regs_t();
    BOOST_FOREACH(unit_t unit, _dac_regs.keys())
    {
        _dac_regs[unit].data = 1;
        _dac_regs[unit].addr = ad5623_regs_t::ADDR_ALL;
        _dac_regs[unit].cmd  = ad5623_regs_t::CMD_RESET;
        this->_write_aux_dac(unit);
    }
}

b250_dboard_iface::~b250_dboard_iface(void)
{
    UHD_SAFE_CALL
    (
    //TODO shut down
    )
}

/***********************************************************************
 * Clocks
 **********************************************************************/
void b250_dboard_iface::set_clock_rate(unit_t unit, double rate)
{
    _clock_rates[unit] = rate; //set to shadow
    //switch(unit){
    //case UNIT_RX: _clock_ctrl->set_rate_rx_dboard_clock(rate); return;
    //case UNIT_TX: _clock_ctrl->set_rate_tx_dboard_clock(rate); return;
    //}
}

double b250_dboard_iface::get_clock_rate(unit_t unit)
{
    return _clock_rates[unit]; //get from shadow
}

std::vector<double> b250_dboard_iface::get_clock_rates(unit_t unit)
{
    //switch(unit){
    //case UNIT_RX: return _clock_ctrl->get_rates_rx_dboard_clock();
    //case UNIT_TX: return _clock_ctrl->get_rates_tx_dboard_clock();
    //default: UHD_THROW_INVALID_CODE_PATH();
    //}
}

void b250_dboard_iface::set_clock_enabled(unit_t unit, bool enb)
{
    //switch(unit){
    //case UNIT_RX: _clock_ctrl->enable_rx_dboard_clock(enb); return;
    //case UNIT_TX: _clock_ctrl->enable_tx_dboard_clock(enb); return;
    //}
}

double b250_dboard_iface::get_codec_rate(unit_t)
{
    return B250_RADIO_CLOCK_RATE;
}

/***********************************************************************
 * GPIO
 **********************************************************************/
void b250_dboard_iface::_set_pin_ctrl(unit_t unit, boost::uint16_t value)
{
    return _config.gpio->set_pin_ctrl(unit, value);
}

void b250_dboard_iface::_set_gpio_ddr(unit_t unit, boost::uint16_t value)
{
    return _config.gpio->set_gpio_ddr(unit, value);
}

void b250_dboard_iface::_set_gpio_out(unit_t unit, boost::uint16_t value)
{
    return _config.gpio->set_gpio_out(unit, value);
}

boost::uint16_t b250_dboard_iface::read_gpio(unit_t unit)
{
    return _config.gpio->read_gpio(unit);
}

void b250_dboard_iface::_set_atr_reg(unit_t unit, atr_reg_t atr, boost::uint16_t value)
{
    return _config.gpio->set_atr_reg(unit, atr, value);
}

void b250_dboard_iface::set_gpio_debug(unit_t, int)
{
    throw uhd::not_implemented_error("no set_gpio_debug implemented");
}

/***********************************************************************
 * SPI
 **********************************************************************/
#define toslaveno(unit) \
    (((unit) == dboard_iface::UNIT_TX)? _config.tx_spi_slaveno : _config.rx_spi_slaveno)

void b250_dboard_iface::write_spi(
    unit_t unit,
    const spi_config_t &config,
    boost::uint32_t data,
    size_t num_bits
){
    _config.spi->write_spi(toslaveno(unit), config, data, num_bits);
}

boost::uint32_t b250_dboard_iface::read_write_spi(
    unit_t unit,
    const spi_config_t &config,
    boost::uint32_t data,
    size_t num_bits
){
    return _config.spi->read_spi(toslaveno(unit), config, data, num_bits);
}

/***********************************************************************
 * I2C
 **********************************************************************/
void b250_dboard_iface::write_i2c(boost::uint8_t addr, const byte_vector_t &bytes)
{
    return _config.i2c->write_i2c(addr, bytes);
}

byte_vector_t b250_dboard_iface::read_i2c(boost::uint8_t addr, size_t num_bytes)
{
    return _config.i2c->read_i2c(addr, num_bytes);
}

/***********************************************************************
 * Aux DAX/ADC
 **********************************************************************/
void b250_dboard_iface::_write_aux_dac(unit_t unit)
{
    static const uhd::dict<unit_t, int> unit_to_spi_dac = map_list_of
        (UNIT_RX, DB_RX_LSDAC_SEN)
        (UNIT_TX, DB_TX_LSDAC_SEN)
    ;
    _config.spi->write_spi(
        unit_to_spi_dac[unit], spi_config_t::EDGE_FALL,
        _dac_regs[unit].get_reg(), 24
    );
}

void b250_dboard_iface::write_aux_dac(unit_t unit, aux_dac_t which, double value)
{
    _dac_regs[unit].data = boost::math::iround(4095*value/3.3);
    _dac_regs[unit].cmd = ad5623_regs_t::CMD_WR_UP_DAC_CHAN_N;

    typedef uhd::dict<aux_dac_t, ad5623_regs_t::addr_t> aux_dac_to_addr;
    static const uhd::dict<unit_t, aux_dac_to_addr> unit_to_which_to_addr = map_list_of
        (UNIT_RX, map_list_of
            (AUX_DAC_A, ad5623_regs_t::ADDR_DAC_B)
            (AUX_DAC_B, ad5623_regs_t::ADDR_DAC_A)
            (AUX_DAC_C, ad5623_regs_t::ADDR_DAC_A)
            (AUX_DAC_D, ad5623_regs_t::ADDR_DAC_B)
        )
        (UNIT_TX, map_list_of
            (AUX_DAC_A, ad5623_regs_t::ADDR_DAC_A)
            (AUX_DAC_B, ad5623_regs_t::ADDR_DAC_B)
            (AUX_DAC_C, ad5623_regs_t::ADDR_DAC_B)
            (AUX_DAC_D, ad5623_regs_t::ADDR_DAC_A)
        )
    ;
    _dac_regs[unit].addr = unit_to_which_to_addr[unit][which];
    this->_write_aux_dac(unit);
}

double b250_dboard_iface::read_aux_adc(unit_t unit, aux_adc_t which)
{
    static const uhd::dict<unit_t, int> unit_to_spi_adc = map_list_of
        (UNIT_RX, DB_RX_LSADC_SEN)
        (UNIT_TX, DB_TX_LSADC_SEN)
    ;

    //setup spi config args
    spi_config_t config;
    config.mosi_edge = spi_config_t::EDGE_FALL;
    config.miso_edge = spi_config_t::EDGE_RISE;

    //setup the spi registers
    ad7922_regs_t ad7922_regs;
    switch(which){
    case AUX_ADC_A: ad7922_regs.mod = 0; break;
    case AUX_ADC_B: ad7922_regs.mod = 1; break;
    } ad7922_regs.chn = ad7922_regs.mod; //normal mode: mod == chn

    //write and read spi
    _config.spi->write_spi(
        unit_to_spi_adc[unit], config,
        ad7922_regs.get_reg(), 16
    );
    ad7922_regs.set_reg(boost::uint16_t(_config.spi->read_spi(
        unit_to_spi_adc[unit], config,
        ad7922_regs.get_reg(), 16
    )));

    //convert to voltage and return
    return 3.3*ad7922_regs.result/4095;
}
