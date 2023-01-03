#!/usr/bin/python3
'''
This file is part of Hackflight.

Hackflight is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.

Hackflight is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
Hackflight. If not, see <https://www.gnu.org/licenses/>.
'''

from comms import Comms
from serial.tools.list_ports import comports
import os
import tkinter as tk
from numpy import radians as rad

from mspparser import MspParser

from dialogs.imu import IMU
from dialogs.motors import MotorsQuadXMW, MotorsCoaxial
from dialogs.receiver import Receiver
from dialogs.sensors import Sensors

from resources import resource_path
from debugging import debug

DISPLAY_WIDTH = 800
DISPLAY_HEIGHT = 600

SPLASH_LOCATION = 430, 260

BACKGROUND_COLOR = 'white'

CONNECTION_DELAY_MSEC = 4000

USB_UPDATE_MSEC = 200

# Viz class runs the show =====================================================


class Viz(MspParser):

    def __init__(self):

        MspParser.__init__(self)

        # No communications or arming yet
        self.comms = None
        self.armed = False
        self.gotimu = False

        # Do basic Tk initialization
        self.root = tk.Tk()
        self.root.configure(bg=BACKGROUND_COLOR,
                            highlightbackground='black',
                            highlightthickness=3)
        self.root.resizable(False, False)
        self.root.title('Hackflight Visualizer')
        left = (self.root.winfo_screenwidth() - DISPLAY_WIDTH) / 2
        top = (self.root.winfo_screenheight() - DISPLAY_HEIGHT) / 2
        self.root.geometry('%dx%d+%d+%d' % (DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                            left, top))
        self.frame = tk.Frame(self.root)

        # Too much hassle on Windows
        if 'nt' != os.name:
            self.root.tk.call('wm', 'iconphoto', self.root._w,
                              tk.PhotoImage('icon.xbm'))

        self.root.protocol('WM_DELETE_WINDOW', self.quit)

        # Create panes for two rows of widgets
        self.pane1 = self._add_pane()
        self.pane2 = self._add_pane()

        # Add buttons with callbacks
        self.button_connect = self._add_button('Connect', self.pane1,
                                               self._connect_button_callback)
        self.button_imu = self._add_button('IMU', self.pane2,
                                           self._imu_button_callback)
        self.button_motors = self._add_button('Motors', self.pane2,
                                              self._motors_button_callback)
        self.button_receiver = self._add_button('Receiver', self.pane2,
                                                self._receiver_button_callback)
        self.button_sensors = self._add_button('Sensors', self.pane2,
                                               self._sensors_button_callback)

        # Prepare for adding ports as they are detected by our timer task
        self.portsvar = tk.StringVar(self.root)
        self.portsmenu = None
        self.connected = False
        self.ports = []

        # Finalize Tk stuff
        self.frame.pack()
        self.canvas = tk.Canvas(self.root, width=DISPLAY_WIDTH,
                                height=DISPLAY_HEIGHT, background='black')
        self.canvas.pack()

        # Set up a text label for reporting errors
        errmsg = ('No response from board.  Possible reasons:\n\n' +
                  '    * You connected to the wrong port.\n\n' +
                  '    * IMU is not responding.\n\n')
        self.error_label = tk.Label(self.canvas, text=errmsg, bg='black',
                                    fg='red', font=(None, 24), justify=tk.LEFT)
        self.hide(self.error_label)

        # Add widgets for motor-testing dialogs; hide them immediately
        self.motors_quadxmw = self._add_motor_dialog(MotorsQuadXMW)
        self.motors_coaxial = self._add_motor_dialog(MotorsCoaxial)

        # Create receiver dialog
        self.receiver = Receiver(self)

        # Creaate sensors dialog
        self.sensors = Sensors(self)

        # Create IMU dialog
        self.imu = IMU(self)
        self._schedule_connection_task()

        # Create a splash image
        self.splashimage = tk.PhotoImage(file=resource_path('splash.gif'))
        self._show_splash()

        # Set up parser's request strings
        self.attitude_request = MspParser.serialize_ATTITUDE_Request()
        self.rc_request = MspParser.serialize_RC_Request()
        # self.paa3905_request = MspParser.serialize_PAA3905_Request()
        # self.vl53l5_request = MspParser.serialize_VL53L5_Request()

        # No messages yet
        self.roll_pitch_yaw = [0]*3
        self.rxchannels = [0]*6

        self.mock_mocap_xdir = +1
        self.mock_mocap_ydir = -1
        self.mock_mocap_dx = 0
        self.mock_mocap_dy = 0

    def quit(self):
        self.motors_quadxmw.stop()
        self.motors_coaxial.stop()
        self.root.destroy()

    def hide(self, widget):

        widget.place(x=-9999)

    def getChannels(self):

        return self.rxchannels

    def getRanger(self):

        import numpy.random as random
        return (random.randint(0, 256) for _ in range(16))

    def getMocap(self):

        SCALE = 0.1

        self.mock_mocap_dx += self.mock_mocap_xdir * SCALE
        if self.mock_mocap_dx > 100:
            self.mock_mocap_xdir = -1
        if self.mock_mocap_dx < -100:
            self.mock_mocap_xdir = +1

        self.mock_mocap_dy += self.mock_mocap_ydir * SCALE
        if self.mock_mocap_dy > 100:
            self.mock_mocap_ydir = -1
        if self.mock_mocap_dy < -100:
            self.mock_mocap_ydir = +1

        return self.mock_mocap_dx, self.mock_mocap_dy

    def getRollPitchYaw(self):

        # Configure widgets to show connected
        self._enable_widget(self.button_motors)
        self._enable_widget(self.button_receiver)
        self._enable_widget(self.button_sensors)
        self._disable_widget(self.portsmenu)

        self.button_connect['text'] = 'Disconnect'
        self._enable_widget(self.button_connect)

        return self.roll_pitch_yaw

    def scheduleTask(self, delay_msec, task):

        self.root.after(delay_msec, task)

    def handle_RC(self, c1, c2, c3, c4, c5, c6):

        def norm(x):

            MIN = 987
            MAX = 2011

            return (x - MIN) / (MAX - MIN)

        def scale(x):

            return 2 * norm(x) - 1

        # Scale throttle from [-1,+1] to [0,1]
        self.rxchannels = (norm(c1),
                           scale(c2),
                           scale(c3),
                           scale(c4),
                           scale(c5),
                           scale(c6))

        # As soon as we handle the callback from one request, send another
        # request, if receiver dialog is running
        if self.receiver.running:
            self._send_rc_request()

    def handle_ATTITUDE(self, angx, angy, heading):

        self.roll_pitch_yaw = rad(angx/10), -rad(angy/10), rad(heading)

        self.gotimu = True

        # As soon as we handle the callback from one request, send another
        # request, if receiver dialog is running
        if self.imu.running:
            self._send_attitude_request()

    def handle_VL53L5(self,
                      p11, p12, p13, p14, p21, p22, p23, p24,
                      p31, p32, p33, p34, p41, p42, p43, p44):
        return

    def handle_PAA3905(self, x, y):
        return

    def _add_pane(self):

        pane = tk.PanedWindow(self.frame, bg=BACKGROUND_COLOR)
        pane.pack(fill=tk.BOTH, expand=1)
        return pane

    def _add_button(self, label, parent, callback, disabled=True):

        button = tk.Button(parent, text=label, command=callback)
        button.pack(side=tk.LEFT)
        button.config(state=('disabled' if disabled else 'normal'))
        return button

    def _add_motor_dialog(self, dialog):
        d = dialog(self)
        d.stop()
        return d

    # unsigned int => signed float conversion
    def _float(self, intval):
        return intval / 1000 - 2

    # Callback for IMU button
    def _imu_button_callback(self):

        self._clear()

        self._disable_widget(self.button_imu)
        self._enable_widget(self.button_motors)
        self._enable_widget(self.button_receiver)
        self._enable_widget(self.button_sensors)

        self.motors_quadxmw.stop()
        self.motors_coaxial.stop()
        self.receiver.stop()
        self.sensors.stop()
        self._send_attitude_request()
        self.imu.start()

    def _start(self):

        self._send_attitude_request()
        self.imu.start()

        self.gotimu = False
        self.hide(self.error_label)
        self.scheduleTask(CONNECTION_DELAY_MSEC, self._checkimu)

    def _checkimu(self):

        if not self.gotimu:

            self.imu.stop()
            self.error_label.place(x=50, y=50)
            self._disable_widget(self.button_imu)
            self._disable_widget(self.button_motors)
            self._disable_widget(self.button_receiver)

    # Sends state request to FC
    def _send_attitude_request(self):

        self.comms.send_request(self.attitude_request)

    # Sends RC request to FC
    def _send_rc_request(self):

        self.comms.send_request(self.rc_request)

    # Sends sensor requests to FC
    def _send_sensors_request(self):

        return

        # self.comms.send_request(self.vl53l5_request)
        # self.comms.send_request(self.paa3905_request)

    # Callback for Motors button
    def _motors_button_callback(self):

        self._clear()

        self._enable_widget(self.button_imu)
        self._disable_widget(self.button_motors)
        self._enable_widget(self.button_receiver)
        self._enable_widget(self.button_sensors)

        self.imu.stop()
        self.receiver.stop()
        self.sensors.stop()
        self.motors_quadxmw.start()

    def _clear(self):

        self.canvas.delete(tk.ALL)

    # Callback for Receiver button
    def _receiver_button_callback(self):

        self._clear()

        self._enable_widget(self.button_imu)
        self._enable_widget(self.button_motors)
        self._disable_widget(self.button_receiver)
        self._enable_widget(self.button_sensors)

        self.imu.stop()
        self.sensors.stop()
        self.motors_quadxmw.stop()
        self.motors_coaxial.stop()
        self._send_rc_request()
        self.receiver.start()

    # Callback for Sensors button
    def _sensors_button_callback(self):

        self._clear()

        self._enable_widget(self.button_imu)
        self._enable_widget(self.button_motors)
        self._enable_widget(self.button_receiver)
        self._disable_widget(self.button_sensors)

        self.imu.stop()
        self.receiver.stop()
        self.motors_quadxmw.stop()
        self.motors_coaxial.stop()
        self._send_sensors_request()
        self.sensors.start()

    # Callback for Connect / Disconnect button
    def _connect_button_callback(self):

        if self.connected:

            self.imu.stop()
            self.motors_quadxmw.stop()
            self.motors_coaxial.stop()
            self.receiver.stop()

            if self.comms is not None:

                self.comms.stop()

            self._clear()

            self._disable_widget(self.button_imu)
            self._disable_widget(self.button_motors)
            self._disable_widget(self.button_receiver)
            self._disable_widget(self.button_sensors)

            self.button_connect['text'] = 'Connect'
            self._enable_widget(self.portsmenu)

            self.hide(self.error_label)

            self._show_splash()

        else:

            self.comms = Comms(self)
            self.comms.start()

            self.button_connect['text'] = 'Connecting ...'
            self._disable_widget(self.button_connect)

            self.canvas.delete(self.splash)

            self.scheduleTask(CONNECTION_DELAY_MSEC, self._start)

        self.connected = not self.connected

    # Gets available ports
    def _getports(self):

        allports = comports()

        ports = []

        for port in allports:

            portname = port[0]

            if any(name in portname
                    for name in ('usbmodem', 'ttyACM', 'ttyUSB', 'COM')):
                if portname not in ('COM1', 'COM2'):
                    ports.append(portname)

        return ports

    # Checks for changes in port status (hot-plugging USB cables)
    def _connection_task(self):

        ports = self._getports()

        if ports != self.ports:

            if self.portsmenu is None:

                self.portsmenu = tk.OptionMenu(self.pane1, self.portsvar,
                                               *ports)
            else:

                for port in ports:

                    self.portsmenu['menu'].add_command(label=port)

            self.portsmenu.pack(side=tk.LEFT)

            # Disconnected
            if len(ports) < len(self.ports):
                debug('disconnected')
                exit(0)

            # Connected
            else:
                self.portsvar.set(ports[0])  # default value
                self._enable_widget(self.button_connect)

            self.ports = ports

        self._schedule_connection_task()

    # Mutually recursive with above
    def _schedule_connection_task(self):

        self.root.after(USB_UPDATE_MSEC, self._connection_task)

    def _enable_widget(self, button):

        button['state'] = 'normal'

    def _disable_widget(self, button):

        button['state'] = 'disabled'

    def sendMotorMessage(self, motors):

        if self.comms is not None:  # Guard at startup
            self.comms.send_message(MspParser.serialize_SET_MOTOR,
                                    (motors[0],
                                     motors[1],
                                     motors[2],
                                     motors[3]))

    def _show_splash(self):

        self.splash = self.canvas.create_image(SPLASH_LOCATION,
                                               image=self.splashimage)


def main():

    Viz()
    tk.mainloop()


main()
