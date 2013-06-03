#include <stdio.h>
#include <stdlib.h>
#include <pcap.h>
#include <netinet/in.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "uhd_dump.h"

//#define DEBUG 1

void usage()
{
  fprintf(stderr,"Usage: vrlp_dump [-h host_ip] filename.pcap\n");
  exit(2);
}

int main(int argc, char *argv[])
{
  struct pbuf_info *packet_buffer;      // Store all packets of interest here 
  struct in_addr host_addr;             // Apparent Host IP addr in this capture
  struct in_addr usrp_addr;             // Apparent USRP IP addr in this capture
  struct timeval *origin_ts;            // Timestamp of first packet in file.
  long long origin_ts_in_us;                  
  int direction;                        // Flag to show direction of packet flow. 0=H->U, 1=U->H.
  int packet_count[2];                  // Number of packets that match filter 
  double size_average[2];               // Average size of packets Host to USRP 
  int size_histogram[90][2];            // Array captures histogram of packet sizes Host to USRP in 100 byte bins
  int x,y;                              // Local integer scratch variables
  char *conversion_error[1];
  int c;
  char buffer[26];                      // Buffer to format GMT time stamp strings for output
  double time_since_start;              // Time elapsed in seconds since start

  // const struct eth_header *eth_header;
  const struct ip_header *ip_header;
  const struct udp_header *udp_header;
  const struct vrlp_header *vrlp_header;
  const struct vrt_header *vrt_header;
  const struct chdr_sid *chdr_sid;



  host_addr.s_addr = 0x0;

  while ((c = getopt(argc, argv, "h:")) != -1) {
    switch(c) {
    case 'h':
      // Explicit IP address for host on command line
      if (*optarg == '\0')
	usage();
      host_addr.s_addr = strtol(strtok(optarg,"."),conversion_error,10) ;
      if  (**conversion_error != '\0') 
      	usage();
      host_addr.s_addr = host_addr.s_addr | strtol(strtok(NULL,"."),conversion_error,10) << 8;
      if  (**conversion_error != '\0') 
       	usage();
      host_addr.s_addr = host_addr.s_addr | strtol(strtok(NULL,"."),conversion_error,10) << 16;
      if  (**conversion_error != '\0') 
       	usage();
      host_addr.s_addr = host_addr.s_addr | strtol(strtok(NULL,"\0"),conversion_error,10) << 24;
      if  (**conversion_error != '\0') 
	usage();
      break;
    case'?':
    default:
      usage();
    }
  }

  argc -= (optind - 1);
  argv += (optind -1);


  // Just a mandatory pcap filename for now, better parser and options later.
  if (argc != 2) {
    fprintf(stderr,"Usage: vrlp_dump  <PCAPFILENAME>\n");
    return(2);
  }


  // Init packet buffer
  packet_buffer = malloc(sizeof(struct pbuf_info));

  // Init origin timestamp
  origin_ts = malloc(sizeof(struct timeval));

  // Go read matching packets from capture file into memory
  get_udp_port_from_file(VRLP_PORT,argv[1],packet_buffer,origin_ts);

  // Extract origin tome of first packet and convert to uS.
  origin_ts_in_us = origin_ts->tv_sec * 1000000 + origin_ts->tv_usec;                                                                                                                                                                       
  

  // Count number of packets in capture
  packet_buffer->current = packet_buffer->start;
  x = 0;

  while (packet_buffer->current != NULL) {
    x++;
    packet_buffer->current = packet_buffer->current->next;	  
  }

  fprintf(stdout,"\n===================================================================\n");
  fprintf(stdout,"\n Total matching packet count in capture file: %d\n",x);
  fprintf(stdout,"\n===================================================================\n\n");
  
  // If no packets were VRLP then just exit now
  if (x == 0) {
    exit(0);
  }

  // Determine host and USRP IP addresses so that we can classify direction of packet flow later.
 if (host_addr.s_addr == 0x0)
   get_connection_endpoints(packet_buffer,&host_addr,&usrp_addr);

  // Count packets in list. 
  // Build histogram of packet sizes
  // Build histogram of Stream ID's
  packet_buffer->current = packet_buffer->start;
		
  for (x=0;x<90;x+=1)
    size_histogram[x][H2U] = size_histogram[x][U2H] = 0;

  size_average[H2U] = size_average[U2H] = 0;
  packet_count[H2U] = packet_count[U2H] = 0;

  while (packet_buffer->current != NULL) {

    // Overlay IP header on packet payload	
    ip_header = (struct ip_header *)(packet_buffer->current->payload+ETH_SIZE);

    // Identify packet direction
    if (ip_header->ip_src.s_addr == host_addr.s_addr)
      direction = H2U;
    else
      direction = U2H;

    packet_count[direction]++;
    size_average[direction]+=(double)packet_buffer->current->size;
    if ((x=packet_buffer->current->size) > 9000) 
      fprintf(stderr,"Current packet size = %d at absolute time %s, relative time %f, exceeds MTU! Skip counting.",
	      x,format_gmt(&packet_buffer->current->ts,buffer),(relative_time(&packet_buffer->current->ts,origin_ts)));
    else
      size_histogram[x/100][direction]++;

    packet_buffer->current = packet_buffer->current->next;

  }

  fprintf(stdout,"\n===================================================================\n");
  fprintf(stdout,"\n Average packet size Host -> USRP: %d\n",(int)(size_average[H2U]/packet_count[H2U]));
  fprintf(stdout,"\n Average packet size USRP -> Host: %d\n",(int)(size_average[U2H]/packet_count[U2H]));
  fprintf(stdout,"\n===================================================================\n\n");

  //
  // Now produce packet by packet log
  //
  packet_buffer->current = packet_buffer->start;
  x = 0;

  while (packet_buffer->current != NULL) {
    x++;

    // Overlay IP header on packet payload	
    ip_header = (struct ip_header *)(packet_buffer->current->payload+ETH_SIZE);

    // Overlay VRLP header on packet payload
    vrlp_header = (struct vrlp_header *)(packet_buffer->current->payload+ETH_SIZE+IP_SIZE+UDP_SIZE);

    // Overlay VRT header on packet payload
    vrt_header = (struct vrt_header *)(packet_buffer->current->payload+ETH_SIZE+IP_SIZE+UDP_SIZE+VRLP_SIZE);

    // Overlay CHDR SID definition on VRT SID.
    chdr_sid = (struct chdr_sid *)&(vrt_header->vrt_sid);

    // Calculate time offset of this packet from start
    time_since_start = ((double) packet_buffer->current->ts.tv_sec * 1000000 + packet_buffer->current->ts.tv_usec - origin_ts_in_us)/1000000;

    // Is it VRT inside VRLP?
    if ((vrlp_header->vrlp_start) != 0x504C5256) // VRLP start code
      {
	fprintf(stdout,"%8d %f NOT VRLP. %x\n",x,time_since_start,vrlp_header->vrlp_start);
      }
    else
      {
	// Implicitly VRT encapuslauted in VRLP.
	// Extract the device portion of the SID to see which packet flow this belongs in
	y = (int) &chdr_sid->src_endpoint;
	fprintf(stdout,"%8d %f \t",x,time_since_start);
	print_direction(packet_buffer,&host_addr,&usrp_addr);
	fprintf(stdout,"\t");
	print_sid(packet_buffer);
	fprintf(stdout,"\t");
	print_vita_header(packet_buffer,&host_addr);
	fprintf(stdout,"\n");
	
      }
    packet_buffer->current = packet_buffer->current->next;

  }

}
 
