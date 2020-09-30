/*   Bridge Command 5.0 Ship Simulator
     Copyright (C) 2020 James Packer

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY Or FITNESS For A PARTICULAR PURPOSE.  See the
     GNU General Public License For more details.

     You should have received a copy of the GNU General Public License along
     with this program; if not, write to the Free Software Foundation, Inc.,
     51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA. */

#include "AISOverUDP.hpp"
#include <vector>
#include <iostream>
#include <mutex>
#include "libs/aisparser/c/src/portable.h"
#include "libs/aisparser/c/src/nmea.h"
#include "libs/aisparser/c/src/sixbit.h"
#include "libs/aisparser/c/src/vdm_parse.h"

extern int terminateAISThread;
extern std::mutex terminateAISThread_mutex;

AISOverUDP::AISOverUDP(int port) {
    _port = port;
}

void AISOverUDP::AISThread()
{
    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context);
    socket.open(asio::ip::udp::v4());
    socket.bind(asio::ip::udp::endpoint(asio::ip::udp::v4(), _port));

    std::cout << "Set up on port " << _port << std::endl;

    ais_state ais = {};
    aismsg_1  msg_1 = {};
    aismsg_2  msg_2 = {};
    aismsg_3  msg_3 = {};

    for (;;)
    {
      
      //Check if we've been asked to stop
      terminateAISThread_mutex.lock();
      if (terminateAISThread!=0) {
        break;
      }
      terminateAISThread_mutex.unlock();
      
      try {

        //unsigned char buf[128];
        char buf[128];

        std::cout << "Waiting for data 1." << std::endl;

        //This is all with low level socket functions, not actually asio. Needs to be checked on Windows!

        //Set timeout
        #ifdef WIN32
        DWORD timeout = 1000;
        setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(DWORD))!=0;
        #else
        struct timeval tv = { 1, 0 };
        setsockopt(socket.native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        #endif

        //Read from UDP socket
        #ifdef WIN32
        int nread = ::recv(socket.native_handle(), buf, sizeof(buf),0);
        #else
        ssize_t nread = ::read(socket.native_handle(), buf, sizeof(buf));
        #endif

        if (nread>0) {
          std::cout << "Received: " << buf << std::endl;

          if (assemble_vdm( &ais, buf ) == 0) {
            ais.msgid = (unsigned char) get_6bit( &ais.six_state, 6 );
            std::cout << "Recieved AIS Message type " << (int)ais.msgid << std::endl;
            if( (ais.msgid == 1)&&(parse_ais_1( &ais, &msg_1 ) == 0) ) {  
              double lat;
              double lon;
              pos2ddd(msg_1.latitude,msg_1.longitude,&lat,&lon); //Convert to decimal lat and long

              std::cout << "Received AIS Message type 1: COG:" << msg_1.cog << " SOG:" << msg_1.sog << " MMSI:" << msg_1.userid << " Lat:" << lat << " Long: " << lon << std::endl;
            }
          }

        }

      } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
      }
    }

    std::cout << "Ending AIS UDP" << std::endl;

}
