/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-
/*
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
  simulator connector for ardupilot version of last_letter
*/

#include <AP_HAL.h>
#if CONFIG_HAL_BOARD == HAL_BOARD_SITL
#include "SIM_last_letter.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

extern const AP_HAL::HAL& hal;

/*
  constructor
 */
last_letter::last_letter(const char *home_str, const char *frame_str) :
    Aircraft(home_str, frame_str),
    last_timestamp_us(0),
    sock(true),
    initialised(false)
{

      // try to bind to a specific port so that if we restart ArduPilot
      // last_letter keeps sending us packets. Not strictly necessary but
      // useful for debugging
      sock.bind("127.0.0.1", fdm_port+1);

      sock.reuseaddress();
      sock.set_blocking(false);
}

/*
  start last_letter child
 */
bool last_letter::start_last_letter(void)
{
    pid_t child_pid = fork();
    if (child_pid == 0) {
        close(0);
        open("/dev/null", O_RDONLY);
        // in child
        for (uint8_t i=3; i<100; i++) {
            close(i);
        }

        char argHome[50];
        sprintf(argHome,"home:=[%f,%f,%f]",home.lat*1.0e-7,home.lng*1.0e-7,(double)home.alt*1.0e-2);

        char argInstance[20];
        sprintf(argInstance,"instance:=%d",instance);

        printf("\n\nHOME LOCATION: %s \nINSTANCE ARG: %s\n\n", argHome, argInstance);

        int ret = execlp("roslaunch",
                         "roslaunch",
                         "last_letter",
                         "launcher.launch",
                         "ArduPlane:=true",
                         "simRate:=500",
                         "deltaT:=0.002",
                         argHome,
                         argInstance,
                         NULL);
        if (ret != 0) {
            perror("roslaunch");
        }
        exit(1);
    }

    return true;
}

/*
  send servos
*/
void last_letter::send_servos(const struct sitl_input &input)
{
    servo_packet pkt;
    memcpy(pkt.servos, input.servos, sizeof(pkt.servos));
    sock.sendto(&pkt, sizeof(pkt), "127.0.0.1", fdm_port);
}

/*
  receive an update from the FDM
  This is a blocking function
 */
void last_letter::recv_fdm(const struct sitl_input &input)
{
    fdm_packet pkt;

    /*
      we re-send the servo packet every 0.1 seconds until we get a
      reply. This allows us to cope with some packet loss to the FDM
     */
    while (sock.recv(&pkt, sizeof(pkt), 100) != sizeof(pkt)) {
        send_servos(input);
    }

    accel_body = Vector3f(pkt.xAccel, pkt.yAccel, pkt.zAccel);
    gyro = Vector3f(pkt.rollRate, pkt.pitchRate, pkt.yawRate);
    velocity_ef = Vector3f(pkt.speedN, pkt.speedE, pkt.speedD);
    location.lat = pkt.latitude * 1.0e7;
    location.lng = pkt.longitude * 1.0e7;
    location.alt = pkt.altitude*1.0e2;
    dcm.from_euler(pkt.roll, pkt.pitch, pkt.yaw);

    airspeed = pkt.airspeed;

    // auto-adjust to last_letter frame rate
    uint64_t deltat_us = pkt.timestamp_us - last_timestamp_us;
    time_now_us += deltat_us;

    if (deltat_us < 1.0e4 && deltat_us > 0) {
        adjust_frame_time(1.0e6/deltat_us);
    }
    last_timestamp_us = pkt.timestamp_us;
}

/*
  update the last_letter simulation by one time step
 */
void last_letter::update(const struct sitl_input &input)
{
    while (!initialised){

      if (!start_last_letter()){

        time_now_us = 1;
        return;
      }

      initialised = true;
    }
    send_servos(input);
    recv_fdm(input);
    sync_frame_time();
}
#endif // CONFIG_HAL_BOARD
