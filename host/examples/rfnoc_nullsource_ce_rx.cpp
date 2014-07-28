//
// Copyright 2014 Ettus Research LLC
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

#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/exception.hpp>
#include <uhd/usrp/rfnoc/block_ctrl.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <fstream>
#include <csignal>
#include <complex>

namespace po = boost::program_options;

static bool stop_signal_called = false;
void sig_int_handler(int){stop_signal_called = true;}


////////////////////////// APPS /////////////////////////////////////////////////////
////////////////////////// APP1: null source -> host ////////////////////////////////
void run_app_null_source_to_host(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t rate_factor = 12,
    boost::uint32_t lines_per_packet = 50
){
    std::cout << "===== NOTE: This app requires a null source on CE1. =========" << std::endl;
    rate_factor &= 0xFFFF;
    if (lines_per_packet == 0) {
        lines_per_packet = 50;
    } else if (lines_per_packet > 175) {
        lines_per_packet = 175;
    }
    unsigned long long num_total_samps = 0;
    // Create a receive streamer to CE1
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["src_addr"] = "1"; // 1 is null source
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = rx_stream->get_sid(0);
    // Configure null source:
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "set_fc",
            20000, // Host buffer: This is pretty big
            0 // No upstream block
    );
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            8, // Register 8: Set SID
            0x02140000 /* 2.20 */ | ((data_sid >> 16) & 0xFFFF)
    );
    std::cout << "Setting lines per packet to " << lines_per_packet << " => Packet size: " << lines_per_packet * 8 << " Bytes, " << lines_per_packet * 2 << " Samples." << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            9, // Register 9: Lines per packet
            lines_per_packet
    );
    std::cout << "Setting divider to " << rate_factor << ", ~" << (160.0 * 8.0 / (rate_factor + 1)) << " MByte/s" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            10, // Register 10: Rate
            rate_factor // Rate in clock cycles (max 16 bits)
    );

    size_t bytes_per_packet = lines_per_packet * 8;
    size_t samples_per_packet = bytes_per_packet / 4;

    uhd::rx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet);
    std::ofstream outfile;
    if (not null)
        outfile.open(file.c_str(), std::ofstream::binary);

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            true
    );
    std::cout << "Done" << std::endl;

    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;

    while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
        boost::system_time now = boost::get_system_time();
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
        if (num_rx_samps) {
            n_packets += num_rx_samps / samples_per_packet;
	}

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
            outfile.write((const char*)&buff.front(), num_rx_samps * 4);

        if (bw_summary) {
            last_update_samps += num_rx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps / t;
                std::cout << boost::format("\t%f Msps") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
        if (ticks_requested > 0){
            if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
                break;
        }
    } // end while

    // Stop streaming
    std::cout << "Sending command to stop streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            false
    );
    std::cout << "Done" << std::endl;

    // Run recv until nothing is left
    int num_post_samps = 0;
    do {
        num_post_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
    } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

    if (outfile.is_open())
        outfile.close();
    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Received %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Received %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f MByte/s") % (r/1e6*4) << std::endl;
    }
}

////////////////////////// APP: radio0 -> host, but as CE ////////////////////////////////
void run_app_radio_to_host(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t _sampling_rate = 0,
    boost::uint32_t _frequency = 0
){
    std::cout << "===== NOTE: This app requires a radio. =========" << std::endl;
    if (_sampling_rate == 0) {
        _sampling_rate = 1000000;
    }
    if (_frequency == 0) {
        _frequency = 100;
    }
    double sampling_rate = (double) _sampling_rate;
    double frequency = (double) _frequency;
    size_t samples_per_packet = 100;

    std::cout << "Setting rate to: " << sampling_rate/1e6 << " Msps" << std::endl;
    usrp->set_rx_rate(sampling_rate, 0);
    std::cout << "Setting frequency to: " << frequency << " MHz" << std::endl;
    usrp->set_rx_freq(frequency*1e6, 0);
    //usrp->set_rx_subdev_spec("A:A);

    // Create a receive streamer to radio, make it look like CE
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["src_addr"] = "8"; // Because we say so
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = rx_stream->get_sid(0);

    // Configure radio
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_dsp",
            samples_per_packet,
            0x020a0000 | ((data_sid >> 16) & 0xFFFF) // 2.10 -> 2.20 (to filter, CE1)
    );
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_fc",
            20000
    );

    uhd::rx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet);
    std::ofstream outfile;
    if (not null)
        outfile.open(file.c_str(), std::ofstream::binary);

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;

    unsigned long long num_total_samps = 0;
    while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
        boost::system_time now = boost::get_system_time();
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
            outfile.write((const char*)&buff.front(), num_rx_samps * 4);

        if (bw_summary) {
            last_update_samps += num_rx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps / t;
                std::cout << boost::format("\t%f Msps") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
        if (ticks_requested > 0){
            if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
                break;
        }
    } // end while

    // Stop streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    // Run recv until nothing is left
    int num_post_samps = 0;
    do {
        num_post_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
    } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

    if (outfile.is_open())
        outfile.close();
    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Received %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Received %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f MByte/s") % (r/1e6*4) << std::endl;
    }
}

////////////////////////// APP2: null source -> 8/16 converter -> host ////////////////////
void run_app_null_source_converter_host(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t rate_factor = 12,
    boost::uint32_t lines_per_packet = 50
){
    std::cout << "===== NOTE: This app requires a null source on CE1 and a converter on CE0. =========" << std::endl;
    rate_factor &= 0xFFFF;
    if (lines_per_packet == 0) {
        lines_per_packet = 50;
    } else if (lines_per_packet > 180) {
        lines_per_packet = 180;
    }

    size_t bytes_per_packet = lines_per_packet * 8;
    size_t samples_per_packet = bytes_per_packet / 4;
    double expected_rate = (160.0 * 8.0 / (rate_factor + 1)) * 2; // *2 'cause of converter

    unsigned long long num_total_samps = 0;

    std::string null_src_ce = "ce0";
    std::string other_ce = "ce1";

    // Create a receive streamer to CE0
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["src_addr"] = other_ce[2]; // 0 is converter
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = rx_stream->get_sid(0);
    boost::uint32_t other_ce_address = data_sid & 0xFFFF;
    std::cout << "Other CE Address: " << str(boost::format("0x%04x") % other_ce_address) << std::endl;
    boost::uint32_t null_source_address = 0x0210 + boost::lexical_cast<int>(null_src_ce[2]) * 4;
    std::cout << "Null Source Address: " << str(boost::format("0x%04x") % null_source_address) << std::endl;

    // Configure null source:
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "set_fc",
            7500/bytes_per_packet, // CE0 has 8k buffer
            0 // No upstream block
    );
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "poke",
            8, // Register 8: Set SID
            (null_source_address << 16) | other_ce_address
    );
    std::cout << "Setting lines per packet to " << lines_per_packet << " => Packet size: " << lines_per_packet * 8 << " Bytes, " << lines_per_packet * 2 << " Samples." << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "poke",
            9, // Register 9: Lines per packet
            lines_per_packet
    );
    std::cout << "Setting divider to " << rate_factor << ", ~" << expected_rate << " MByte/s" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "poke",
            10, // Register 10: Rate
            rate_factor // Rate in clock cycles (max 16 bits)
    );

    // Configure other block
    std::cout << "Second CE will send to address " << str(boost::format("0x%08x") % ((data_sid >> 16) & 0xFFFF)) << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            other_ce, "poke",
            8, // Register 8: Set SID
	    (1<<16) /* use SID */ | ((data_sid >> 16) & 0xFFFF) /* send to our streamer */
    );
    usrp->get_device3()->rfnoc_cmd(
            other_ce, "set_fc",
            20000, // Host has a large buffer
            2 // How often we report FC (every Nth packet)
    );

    uhd::rx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet*2);
    std::ofstream outfile;
    if (not null)
        outfile.open(file.c_str(), std::ofstream::binary);

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "poke",
            0x0B, // Register 11: Enable
            true
    );
    std::cout << "Done" << std::endl;

    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;

    while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
        boost::system_time now = boost::get_system_time();
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0, true);
        //if (num_rx_samps) {
            //n_packets += num_rx_samps / samples_per_packet;
	//}
        if (num_rx_samps) {
            n_packets++;
        }

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
            outfile.write((const char*)&buff.front(), num_rx_samps*4);

        if (bw_summary) {
            last_update_samps += num_rx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps * 4.0 / t;
                std::cout << boost::format("\t%f MByte/s") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
        if (ticks_requested > 0){
            if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
                break;
        }
    } // end while

    // Stop streaming
    std::cout << "Sending command to stop streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            null_src_ce, "poke",
            0x0B, // Register 11: Enable
            false
    );
    std::cout << "Done" << std::endl;

    // Run recv until nothing is left
    int num_post_samps = 0;
    do {
        num_post_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
    } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);
    boost::this_thread::sleep(boost::posix_time::seconds(2));

    if (outfile.is_open())
        outfile.close();
    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Received %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Received %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f MByte/s") % (r/1e6*4) << "  (Expected: " << expected_rate << " MByte/s)" << std::endl;
    }
}

////////////////////////// APP: host -> null sink ////////////////////
void run_app_host_to_null_sink(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t bytes_per_packet = 1400
){
    if (bytes_per_packet == 0) {
        bytes_per_packet = 1200;
    }
    std::cout << "===== NOTE: This app requires a null sink on CE2. =========" << std::endl;
    size_t samples_per_packet = bytes_per_packet / 4;
    bytes_per_packet = samples_per_packet * 4;
    std::cout << "Bytes per packet: " << bytes_per_packet << std::endl;

    unsigned long long num_total_samps = 0;

    // Create a transmit streamer to CE2
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["dst_addr"] = "2";
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = tx_stream->get_sid(0);
    std::cout << str(boost::format("Using SID: 0x%08x") % data_sid) << std::endl;

    // Configure null sink:
    usrp->get_device3()->rfnoc_cmd(
            "ce2", "set_fc",
            0, // No downstream block
            2 // Report every 2nd packet
    );

    uhd::tx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet, std::complex<short>(0xAAAA, 0xBBBB));

    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;

    while(
            not stop_signal_called
            and (num_requested_samples != num_total_samps or num_requested_samples == 0)
            and (ticks_requested == 0 or (unsigned long long)ticks_diff.ticks() <= ticks_requested)
    ) {
        boost::system_time now = boost::get_system_time();
        size_t num_tx_samps = tx_stream->send(&buff.front(), buff.size(), md, 3.0);
	if (num_tx_samps < buff.size()) {
            std::cout << "Timeout!" << std::endl;
	}
        if (num_tx_samps) {
            n_packets++; // We always send 1 pkt per send call
        }
        num_total_samps += num_tx_samps;

        if (bw_summary) {
            last_update_samps += num_tx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps * 4.0 / t;
                std::cout << boost::format("\t%f MByte/s") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
    } // end while

    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Transmitted %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Transmitted %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f MByte/s") % (r/1e6*4) << std::endl;
    }
}


////////////////////////// APP2: null source -> null sink ////////////////////////////////
void run_app_null_source_to_null_sink(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t rate_factor = 12,
    boost::uint32_t lines_per_packet = 50
){
    std::cout << "=== NOTE: This app requires a null source on CE1 and a null sink on CE2 =======" << std::endl;
    rate_factor &= 0xFFFF;
    if (lines_per_packet == 0) {
        lines_per_packet = 50;
    } else if (lines_per_packet > 175) {
        lines_per_packet = 175;
    }
    if (time_requested == 0.0) {
        time_requested = 10;
        std::cout << "Setting req'd time to " << time_requested << "s" << std::endl;
    }
    size_t bytes_per_packet = lines_per_packet * 8;
    size_t samples_per_packet = bytes_per_packet / 4;

    // Configure null source:
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "set_fc",
            7000/bytes_per_packet, // We have 8k buffer
            0 // No upstream block
    );
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            8, // Register 8: Set SID
            0x02140218 /* 2.20 to 2.24*/
    );
    std::cout << "Setting lines per packet to " << lines_per_packet << " => Packet size: " << lines_per_packet * 8 << " Bytes, " << lines_per_packet * 2 << " Samples." << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            9, // Register 9: Lines per packet
            lines_per_packet
    );
    std::cout << "Setting divider to " << rate_factor << ", ~" << (160.0 * 8.0 / (rate_factor + 1)) << " MByte/s" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            10, // Register 10: Rate
            rate_factor // Rate in clock cycles (max 16 bits)
    );
    // Configure null sink
    usrp->get_device3()->rfnoc_cmd(
            "ce2", "set_fc",
            0, // No downstream block
            2 // Report every 2nd block
    );

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            true
    );
    std::cout << "Done" << std::endl;

    std::cout << "Sleeping for " << time_requested << " s..." << std::endl;
    boost::this_thread::sleep(boost::posix_time::seconds(time_requested));

    // Stop streaming
    std::cout << "Sending command to stop streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            false
    );
    std::cout << "Done" << std::endl;

    // Sleep for a little bit to allow gunk to propagate
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
}

////////////////////////// APP2: null source -> null sink ////////////////////////////////
void run_app_null_source_converter_null_sink(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t rate_factor = 12,
    boost::uint32_t lines_per_packet = 50
){
    std::cout << "=== NOTE: This app requires a null source on CE1,  a null sink on CE2 and a converter on CE 0 =======" << std::endl;
    rate_factor &= 0xFFFF;
    if (lines_per_packet == 0) {
        lines_per_packet = 50;
    } else if (lines_per_packet > 175) {
        lines_per_packet = 175;
    }
    if (time_requested == 0.0) {
        time_requested = 10;
        std::cout << "Setting req'd time to " << time_requested << "s" << std::endl;
    }
    size_t bytes_per_packet = lines_per_packet * 8;
    size_t samples_per_packet = bytes_per_packet / 4;

    // Configure null source:
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "set_fc",
            7000/bytes_per_packet, // We have 8k buffer
            0 // No upstream block
    );
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            8, // Register 8: Set SID
            0x02140210 /* 2.20 to 2.16*/
    );
    std::cout << "Setting lines per packet to " << lines_per_packet << " => Packet size: " << lines_per_packet * 8 << " Bytes, " << lines_per_packet * 2 << " Samples." << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            9, // Register 9: Lines per packet
            lines_per_packet
    );
    std::cout << "Setting divider to " << rate_factor << ", ~" << (160.0 * 8.0 / (rate_factor + 1)) << " MByte/s" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            10, // Register 10: Rate
            rate_factor // Rate in clock cycles (max 16 bits)
    );
    // Configure converter
    usrp->get_device3()->rfnoc_cmd(
            "ce0", "set_fc",
            7000/bytes_per_packet, // No downstream block
            2 // Report every 2nd block
    );
    usrp->get_device3()->rfnoc_cmd(
            "ce0", "poke",
            8, // Register 8: Set SID
	    (1<<16) /* use SID */ | (0x0218 & 0xFFFF) /* send to null sink 2.24 */
    );
    // Configure null sink
    usrp->get_device3()->rfnoc_cmd(
            "ce2", "set_fc",
            0, // No downstream block
            2 // Report every 2nd block
    );

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            true
    );
    std::cout << "Done" << std::endl;

    std::cout << "Sleeping for " << time_requested << " s..." << std::endl;
    boost::this_thread::sleep(boost::posix_time::seconds(time_requested));

    // Stop streaming
    std::cout << "Sending command to stop streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "ce1", "poke",
            0x0B, // Register 11: Enable
            false
    );
    std::cout << "Done" << std::endl;

    // Sleep for a little bit to allow gunk to propagate
    boost::this_thread::sleep(boost::posix_time::milliseconds(100));
}
////////////////////////// APP: radio -> filter -> host ////////////////////
void run_app_radio_filter_host(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t _sampling_rate = 0,
    boost::uint32_t _frequency = 0
){
    std::cout << "===== NOTE: This app requires a radio and a filter on CE1. =========" << std::endl;
    if (_sampling_rate == 0) {
        _sampling_rate = 1000000;
    }
    if (_frequency == 0) {
        _frequency = 100;
    }
    double sampling_rate = (double) _sampling_rate;
    double frequency = (double) _frequency;
    size_t samples_per_packet = 200;

    std::cout << "Setting rate to: " << sampling_rate/1e6 << " Msps" << std::endl;
    usrp->set_rx_rate(sampling_rate, 0);
    std::cout << "Setting frequency to: " << frequency << " MHz" << std::endl;
    usrp->set_rx_freq(frequency*1e6, 0);
    //usrp->set_rx_subdev_spec("A:A);

    std::string ce_select = "ce2";

    // Create a receive streamer to CE1 (filter)
    uhd::stream_args_t stream_args("fc32", "sc16");
    stream_args.args["src_addr"] = ce_select[2];
    //stream_args.args["fc_pkts_per_ack"] = "1000"; // ack every Nth packet
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = rx_stream->get_sid(0);
    boost::uint32_t ce_address = data_sid & 0xFFFF;
    std::cout << str(boost::format("CE Address: 0x%04x") % ce_address) << std::endl;

    // Configure radio
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_dsp",
            samples_per_packet,
            0x02080000 | ce_address // 2.10 -> CE
    );
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_fc",
            8000/(samples_per_packet*4) - 2
    );

    // Configure filter:
    usrp->get_device3()->rfnoc_cmd(
            ce_select, "poke",
            8, // Register 8: Set SID
            //(data_sid >> 16) | (data_sid << 16) // Reverse host SID
            (1<<16)  | ((data_sid >> 16) & 0xFFFF) /* send to our streamer */
    );
    usrp->get_device3()->rfnoc_cmd(
            ce_select, "set_fc",
            20000, // Host buffer: This is pretty big
            2 // Report every Nth packet
    );

    uhd::rx_metadata_t md;
    std::vector<std::complex<float> > buff(samples_per_packet);
    std::ofstream outfile;
    if (not null)
        outfile.open(file.c_str(), std::ofstream::binary);

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    // Start loop
    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;
    unsigned long long num_total_samps = 0;
    while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
        boost::system_time now = boost::get_system_time();
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0, true);
        //if (num_rx_samps) {
            //n_packets += num_rx_samps / samples_per_packet;
	//}
        if (num_rx_samps) {
            n_packets++;
        }

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
            //break;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
            break;
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
            outfile.write((const char*)&buff.front(), num_rx_samps*4);

        if (bw_summary) {
            last_update_samps += num_rx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps * 4.0 / t;
                std::cout << boost::format("\t%f MByte/s") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
        if (ticks_requested > 0){
            if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
                break;
        }
    } // end while

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    // Run recv until nothing is left
    int num_post_samps = 0;
    do {
        num_post_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
    } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

    if (outfile.is_open())
        outfile.close();
    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Received %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Received %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f Msps") % (r/1e6) << "  (Expected: " << sampling_rate << " Msps)" << std::endl;
    }
}

////////////////////////// APP: radio -> ceX -> ceY -> host ////////////////////
void run_app_radio_2ce_host(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t _sampling_rate = 0,
    boost::uint32_t _frequency = 0
){
    std::cout << "===== NOTE: This app requires a radio and a filter on CE1. =========" << std::endl;
    if (_sampling_rate == 0) {
        _sampling_rate = 1000000;
    }
    if (_frequency == 0) {
        _frequency = 100;
    }
    double sampling_rate = (double) _sampling_rate;
    double frequency = (double) _frequency;
    size_t samples_per_packet = 100;

    std::cout << "Setting rate to: " << sampling_rate/1e6 << " Msps" << std::endl;
    usrp->set_rx_rate(sampling_rate, 0);
    std::cout << "Setting frequency to: " << frequency << " MHz" << std::endl;
    usrp->set_rx_freq(frequency*1e6, 0);
    //usrp->set_rx_subdev_spec("A:A);

    std::string ce_select1 = "ce1"; // first ce after radio
    std::string ce_select2 = "ce0"; // 2nd

    boost::uint32_t ce1_address = 0x0214;

    // Create a receive streamer to CE1 (filter)
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["src_addr"] = ce_select2[2];
    //stream_args.args["fc_pkts_per_ack"] = "1000"; // ack every Nth packet
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = rx_stream->get_sid(0);
    boost::uint32_t ce2_address = data_sid & 0xFFFF;
    std::cout << str(boost::format("First CE Address: 0x%04x") % ce1_address) << std::endl;
    std::cout << str(boost::format("Second CE Address: 0x%04x") % ce2_address) << std::endl;

    // Configure radio
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_dsp",
            samples_per_packet,
            0x02080000 | ce1_address // 2.10 -> CE
    );
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "setup_fc",
            8000/(samples_per_packet*4) - 2
    );

    // Configure first CE:
    usrp->get_device3()->rfnoc_cmd(
            ce_select1, "poke",
            8, // Register 8: Set SID
            (1<<16)  | ce2_address
    );
    usrp->get_device3()->rfnoc_cmd(
            ce_select1, "set_fc",
            8000/(samples_per_packet*4) - 2,
            2 // Report every Nth packet
    );

    // Configure second CE:
    usrp->get_device3()->rfnoc_cmd(
            ce_select2, "poke",
            8, // Register 8: Set SID
            (1<<16)  | ((data_sid >> 16) & 0xFFFF)
    );
    usrp->get_device3()->rfnoc_cmd(
            ce_select2, "set_fc",
            20000,
            2 // Report every Nth packet
    );

    uhd::rx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet);
    std::ofstream outfile;
    if (not null)
        outfile.open(file.c_str(), std::ofstream::binary);

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    // Start loop
    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;
    unsigned long long num_total_samps = 0;
    while(not stop_signal_called and (num_requested_samples != num_total_samps or num_requested_samples == 0)) {
        boost::system_time now = boost::get_system_time();
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0, true);
        //if (num_rx_samps) {
            //n_packets += num_rx_samps / samples_per_packet;
	//}
        if (num_rx_samps) {
            n_packets++;
        }

        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cout << boost::format("Timeout while streaming") << std::endl;
            boost::this_thread::sleep(boost::posix_time::milliseconds(100));
            //break;
        }
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE){
            std::string error = str(boost::format("Receiver error: %s") % md.strerror());
            std::cerr << error << std::endl;
            break;
        }

        num_total_samps += num_rx_samps;

        if (outfile.is_open())
            outfile.write((const char*)&buff.front(), num_rx_samps*4);

        if (bw_summary) {
            last_update_samps += num_rx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps * 4.0 / t;
                std::cout << boost::format("\t%f MByte/s") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
        if (ticks_requested > 0){
            if ((unsigned long long)ticks_diff.ticks() > ticks_requested)
                break;
        }
    } // end while

    // Setup streaming
    std::cout << "Sending command to start streaming:" << std::endl;
    usrp->get_device3()->rfnoc_cmd(
            "radio_rx0", "stream_cmd",
            uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS,
            true
    );
    std::cout << "Done" << std::endl;

    // Run recv until nothing is left
    int num_post_samps = 0;
    do {
        num_post_samps = rx_stream->recv(&buff.front(), buff.size(), md, 3.0);
    } while(num_post_samps and md.error_code == uhd::rx_metadata_t::ERROR_CODE_NONE);

    if (outfile.is_open())
        outfile.close();
    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Received %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Received %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f Msps") % (r/1e6) << "  (Expected: " << sampling_rate << " Msps)" << std::endl;
    }
}

////////////////////////// APP: host -> filter -> radio ////////////////////
void run_app_host_filter_radio(
    uhd::usrp::multi_usrp::sptr usrp,
    const std::string &file,
    unsigned long long num_requested_samples,
    double time_requested = 0.0,
    bool bw_summary = false,
    bool stats = false,
    bool null = false,
    boost::uint32_t _sampling_rate = 0,
    boost::uint32_t _frequency = 0
){
    std::cout << "===== NOTE: This app requires a radio and a filter on CE1. =========" << std::endl;
    if (_sampling_rate == 0) {
        _sampling_rate = 1000000;
    }
    if (_frequency == 0) {
        _frequency = 100;
    }
    double sampling_rate = (double) _sampling_rate;
    double frequency = (double) _frequency;
    size_t samples_per_packet = 300;
    size_t bytes_per_packet = 4 * samples_per_packet;
    std::cout << "Setting rate to: " << sampling_rate/1e6 << " Msps" << std::endl;
    usrp->set_tx_rate(sampling_rate, 0);
    std::cout << "Setting frequency to: " << frequency << " MHz" << std::endl;
    usrp->set_tx_freq(frequency*1e6, 0);
    //usrp->set_rx_subdev_spec("A:A);

    // Choose CE:
    std::string ce_id = "ce2";

    // Create a transmit streamer to CE
    uhd::stream_args_t stream_args("sc16", "sc16");
    stream_args.args["dst_addr"] = ce_id[2];
    stream_args.channels = std::vector<size_t>(1, 0);
    uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);
    // Get sid for this connection (channel 0 because there's only 1 channel):
    boost::uint32_t data_sid = tx_stream->get_sid(0);
    boost::uint32_t ce_address = data_sid & 0xFFFF;
    std::cout << str(boost::format("Using SID: 0x%08x") % data_sid) << std::endl;
    std::cout << str(boost::format("CE address: 0x%04x") % ce_address) << std::endl;

    // Configure filter:
    usrp->get_device3()->rfnoc_cmd(
            ce_id, "poke",
            8, // Register 8: Set SID
            (ce_address << 16) | 0x0208 // CE to Radio 2.8
    );
    usrp->get_device3()->rfnoc_cmd(
            ce_id, "set_fc",
            500000/4/samples_per_packet, // Radio has large buffer
            2 // Report every 2nd packet
    );
    // Configure radio:
    usrp->get_device3()->rfnoc_cmd(
            "radio_tx0", "setup_fc",
            500000/4/samples_per_packet/8
    );

    uhd::tx_metadata_t md;
    std::vector<std::complex<short> > buff(samples_per_packet);

    unsigned long long num_total_samps = 0;
    boost::system_time start = boost::get_system_time();
    unsigned long long ticks_requested = (long)(time_requested * (double)boost::posix_time::time_duration::ticks_per_second());
    boost::posix_time::time_duration ticks_diff;
    boost::system_time last_update = start;
    unsigned long long last_update_samps = 0;
    size_t n_packets = 0;

    std::ifstream infile;
    infile.open(file.c_str(), std::ifstream::binary);

    while(
        not stop_signal_called
        and (num_requested_samples != num_total_samps or num_requested_samples == 0)
        and (ticks_requested == 0 or (unsigned long long)ticks_diff.ticks() <= ticks_requested)
    ) {
        if (infile.eof()) {
            infile.clear();
            infile.seekg(0);
        }
        infile.read((char *) &buff[0], bytes_per_packet);

        boost::system_time now = boost::get_system_time();
        size_t num_tx_samps = tx_stream->send(&buff.front(), infile.gcount() / 4, md, 3.0);
	if (num_tx_samps < buff.size()) {
            std::cout << "Timeout!" << std::endl;
	}
        if (num_tx_samps) {
            n_packets++; // We always send 1 pkt per send call
        }
        num_total_samps += num_tx_samps;

        if (bw_summary) {
            last_update_samps += num_tx_samps;
            boost::posix_time::time_duration update_diff = now - last_update;
            if (update_diff.ticks() > boost::posix_time::time_duration::ticks_per_second()) {
                double t = (double)update_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
                double r = (double)last_update_samps * 4.0 / t;
                std::cout << boost::format("\t%f MByte/s") % (r/1e6) << std::endl;
                last_update_samps = 0;
                last_update = now;
            }
        }

        ticks_diff = now - start;
    } // end while
    // Send EOB

    if (stats) {
        std::cout << std::endl;
        double t = (double)ticks_diff.ticks() / (double)boost::posix_time::time_duration::ticks_per_second();
        std::cout << boost::format("Transmitted %d packets in %f seconds") % n_packets % t << std::endl;
        std::cout << boost::format("Transmitted %d bytes in %f seconds") % (num_total_samps*4) % t << std::endl;
        double r = (double)num_total_samps / t;
        std::cout << boost::format("%f MByte/s") % (r/1e6*4) << std::endl;
    }
}

///////////////////// HELPER FUNCTIONS ////////////////////////////////////////////////////
typedef boost::function<uhd::sensor_value_t (const std::string&)> get_sensor_fn_t;

bool check_locked_sensor(std::vector<std::string> sensor_names, const char* sensor_name, get_sensor_fn_t get_sensor_fn, double setup_time){
    if (std::find(sensor_names.begin(), sensor_names.end(), sensor_name) == sensor_names.end())
        return false;

    boost::system_time start = boost::get_system_time();
    boost::system_time first_lock_time;

    std::cout << boost::format("Waiting for \"%s\": ") % sensor_name;
    std::cout.flush();

    while (true){
        if ((not first_lock_time.is_not_a_date_time()) and
                (boost::get_system_time() > (first_lock_time + boost::posix_time::seconds(setup_time))))
        {
            std::cout << " locked." << std::endl;
            break;
        }

        if (get_sensor_fn(sensor_name).to_bool()){
            if (first_lock_time.is_not_a_date_time())
                first_lock_time = boost::get_system_time();
            std::cout << "+";
            std::cout.flush();
        }
        else{
            first_lock_time = boost::system_time();	//reset to 'not a date time'

            if (boost::get_system_time() > (start + boost::posix_time::seconds(setup_time))){
                std::cout << std::endl;
                throw std::runtime_error(str(boost::format("timed out waiting for consecutive locks on sensor \"%s\"") % sensor_name));
            }

            std::cout << "_";
            std::cout.flush();
        }

        boost::this_thread::sleep(boost::posix_time::milliseconds(100));
    }

    std::cout << std::endl;

    return true;
}

///////////////////// MAIN ////////////////////////////////////////////////////
int UHD_SAFE_MAIN(int argc, char *argv[])
{
    uhd::set_thread_priority_safe();

    //variables to be set by po
    std::string args, file, type, nullid, blockid;
    size_t total_num_samps, spb, spp;
    double rate, total_time, setup_time, block_rate;

    //setup the program options
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value("type=x300"), "multi uhd device address args")
        ("file", po::value<std::string>(&file)->default_value("usrp_samples.dat"), "name of the file to write binary samples to, set to stdout to print")
        ("null", "run without writing to file")
        ("nsamps", po::value<size_t>(&total_num_samps)->default_value(0), "total number of samples to receive")
        ("time", po::value<double>(&total_time)->default_value(0), "total number of seconds to receive")
        ("spb", po::value<size_t>(&spb)->default_value(10000), "samples per buffer")
        ("spp", po::value<size_t>(&spp)->default_value(64), "samples per packet (on FPGA and wire)")
        ("block_rate", po::value<double>(&block_rate)->default_value(160e6), "The clock rate of the processing block.")
        ("rate", po::value<double>(&rate)->default_value(1e6), "rate at which samples are produced in the null source")
        ("setup", po::value<double>(&setup_time)->default_value(1.0), "seconds of setup time")
        ("progress", "periodically display short-term bandwidth")
        ("stats", "show average bandwidth on exit")
        ("continue", "don't abort on a bad packet")
        // TODO: change default block ID to 0/NullSource_0 when proper block names work
        ("nullid", po::value<std::string>(&nullid)->default_value("0/CE_0"), "The block ID for the null source.")
        ("blockid", po::value<std::string>(&blockid)->default_value(""), "The block ID for the processing block.")
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    //print the help message
    if (vm.count("help")){
        std::cout
            << boost::format("[RFNOC] Connect a null source to another (processing) block, and stream the result to file %s.") % desc
            << std::endl;
        return ~0;
    }

    bool bw_summary = vm.count("progress") > 0;
    bool stats = vm.count("stats") > 0;
    bool null = vm.count("null") > 0 or vm.count("file") == 0;
    bool continue_on_bad_packet = vm.count("continue") > 0;

    // Check settings
    if (not uhd::rfnoc::block_id_t::is_valid_block_id(nullid)) {
        std::cout << "Must specify a valid block ID for the null source." << std::endl;
        return ~0;
    }
    if (not uhd::rfnoc::block_id_t::is_valid_block_id(blockid)) {
        std::cout << "Must specify a valid block ID for the processing block." << std::endl;
        return ~0;
    }

    //create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the USRP device with: %s...") % args << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);

    // Check it's a Gen-3 device
    if (not usrp->is_device3()) {
        std::cout << "This example only works with generation-3 devices running RFNoC." << std::endl;
        return ~0;
    }
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;
    boost::this_thread::sleep(boost::posix_time::seconds(setup_time)); //allow for some setup time

    // Set up SIGINT handler. For indefinite streaming, display info on how to stop.
    std::signal(SIGINT, &sig_int_handler);
    if (total_num_samps == 0) {
        std::cout << "Press Ctrl + C to stop streaming..." << std::endl;
    }

    // Get block control objects
    uhd::rfnoc::block_ctrl_base::sptr null_src_ctrl   = usrp->get_device3()->find_block_ctrl(nullid);
    uhd::rfnoc::block_ctrl_base::sptr proc_block_ctrl = usrp->get_device3()->find_block_ctrl(blockid);

    // Set channel definitions
    // TODO: tbw

    // Configure packet size and rate
    std::cout << str(boost::format("Requested rate: %.2f Msps (%.2f MByte/s).") % (rate / 1e6) % (rate * 4 / 1e6)) << std::endl;
    null_src_ctrl->sr_write(
            9, // Register number
            spp * 2 // This register is 'lines per packet', and there's 2 sc16-samples per line
    );
    boost::uint32_t cycs_between_pkts = (2 * block_rate / rate) - 1;
    if (cycs_between_pkts > 0xFFFF) {
        std::cout << "Warning: Requested rate is lower than minimum rate." << std::endl;
    }
    double actual_rate = 2 * block_rate / (cycs_between_pkts + 1);
    std::cout << str(boost::format("Setting rate to: %.2f Msps (%.2f MByte/s).") % (actual_rate / 1e6) % (actual_rate * 4 / 1e6)) << std::endl;
    null_src_ctrl->sr_write(
            10, // Register number
            cycs_between_pkts // This register is 'clock cycles between packets'
    );
    // TODO: tbw

    // Connect blocks
    // TODO: tbw

    // Start receiving


    //finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
// vim: sw=4 expandtab:
