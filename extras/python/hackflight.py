#!/usr/bin/env python3
'''
Hackflight in PythoSimple take-off-and-move-forward scriptn

Copyright (C) 2021 S.Basnet, N. Manaye, N. Nguyen, S.D. Levy

MIT License
'''

import numpy as np
import argparse
from argparse import ArgumentDefaultsHelpFormatter
import cv2
from threading import Thread
import socket
from sys import stdout
from time import sleep

from receiver import Receiver
from mixers import mixer_quadxap, mixer_coaxial
from pidcontrollers import RatePid, LevelPid, make_yaw_pid


def _handleImage(image):
    cv2.imshow('Image', image)
    cv2.waitKey(1)


def _debug(msg):
    print(msg)
    stdout.flush()


def _make_udpsocket():

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, True)

    return sock


def _run_telemetry(host,
                   motor_port,
                   telemetry_server_socket,
                   motor_client_socket,
                   receiver,
                   rate_pid,
                   yaw_pid_closure,
                   level_pid,
                   mixer,
                   done):

    running = False

    yaw_pid_fun, yaw_pid_state = yaw_pid_closure

    while True:

        try:
            data, _ = telemetry_server_socket.recvfrom(8*13)
        except Exception:
            done[0] = True
            break

        telemetry_server_socket.settimeout(.1)

        telem = np.frombuffer(data)

        # time = telem[0]
        vehicle_state = telem[1:]

        if not running:
            _debug('Running')
            running = True

        if telem[0] < 0:
            motor_client_socket.close()
            telemetry_server_socket.close()
            break

        # Start with demands from receiver
        demands = np.array(list(receiver.getDemands()))

        # Pass demands through closed-loop controllers
        demands, _pidstate = rate_pid.modifyDemands(vehicle_state, demands)
        demands, yaw_pid_state = yaw_pid_fun(vehicle_state, yaw_pid_state,
                                             demands)
        demands, _pidstate = level_pid.modifyDemands(vehicle_state, demands)

        # for pid_controller in pid_controllers:
        #     demands, _pidstate = pid_controller.modifyDemands(state, demands)

        # Run mixer on demands to get motor values
        motorvals = mixer(demands)

        # Send motor values to client (simulator)
        motor_client_socket.sendto(np.ndarray.tobytes(motorvals),
                                   (host, motor_port))

        # Yield to main thread
        sleep(.001)


def main(host='127.0.0.1',
         motor_port=5000,
         telem_port=5001,
         image_port=5002,
         image_rows=480,
         image_cols=640):

    parser = argparse.ArgumentParser(
            formatter_class=ArgumentDefaultsHelpFormatter)

    parser.add_argument('--vehicle', required=False, default='Phantom',
                        help='Vehicle name')

    args = parser.parse_args()

    mixer_dict = {'Phantom': mixer_quadxap, 'Ingenuity': mixer_coaxial}

    if args.vehicle not in mixer_dict:
        print('Unrecognized vehicle: %s' % args.vehicle)
        exit(1)

    receiver = Receiver()

    mixer = mixer_dict[args.vehicle]

    '''
    pid_controllers = (RatePid(0.225, 0.001875, 0.375),
                       YawPid(2.0, 0.1),
                       LevelPid(0.2))
    '''

    # Allows telemetry thread to tell this thread when user closes socket
    done = [False]

    # Telemetry in and motors out run on their own thread
    motor_client_socket = _make_udpsocket()
    telemetry_server_socket = _make_udpsocket()
    telemetry_server_socket.bind((host, telem_port))

    _debug('Hit the Play button ...')

    # Serve a socket with a maximum of one client
    image_server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    image_server_socket.bind((host, image_port))
    image_server_socket.listen(1)

    # This will block (wait) until a client connets
    image_conn, _ = image_server_socket.accept()
    image_conn.settimeout(1)
    _debug('Got a connection!')

    # Start telemetry thread
    Thread(target=_run_telemetry,
           args=(host,
                 motor_port,
                 telemetry_server_socket,
                 motor_client_socket,
                 receiver,
                 RatePid(0.225, 0.001875, 0.375),
                 make_yaw_pid(2.0, 0.1),
                 LevelPid(0.2),
                 mixer,
                 done)).start()

    while not done[0]:

        try:
            imgbytes = image_conn.recv(image_rows*image_cols*4)

        except Exception:  # likely a timeout from sim quitting
            break

        if len(imgbytes) == image_rows*image_cols*4:

            rgba_image = np.reshape(np.frombuffer(imgbytes, 'uint8'),
                                    (image_rows, image_cols, 4))

            image = cv2.cvtColor(rgba_image, cv2.COLOR_RGBA2RGB)

            _handleImage(image)

        receiver.update()


main()
