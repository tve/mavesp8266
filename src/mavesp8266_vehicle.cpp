/****************************************************************************
 *
 * Copyright (c) 2015, 2016 Gus Grubba. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file mavesp8266_vehicle.cpp
 * ESP8266 Wifi AP, MavLink UART/UDP Bridge
 *
 * @author Gus Grubba <mavlink@grubba.com>
 */

#include "mavesp8266.h"
#include "mavesp8266_vehicle.h"
#include "mavesp8266_parameters.h"

//---------------------------------------------------------------------------------
MavESP8266Vehicle::MavESP8266Vehicle()
    : _queue_count(0)
    , _queue_time(0)
{
    memset(_message, 0 , sizeof(_message));
}

//---------------------------------------------------------------------------------
//-- Initialize
void
MavESP8266Vehicle::begin(MavESP8266Bridge* forwardTo)
{
    MavESP8266Bridge::begin(forwardTo);
    //-- Start UART connected to UAS
    Serial.begin(getWorld()->getParameters()->getUartBaudRate());
    //-- Swap to TXD2/RXD2 (GPIO015/GPIO013) For ESP12 Only
#ifdef DEBUG_PRINT
#ifdef ARDUINO_ESP8266_ESP12
    Serial.swap();
#endif
#endif
}

//---------------------------------------------------------------------------------
//-- Read MavLink message from UAS
void
MavESP8266Vehicle::readMessage()
{
    if(_readMessage()) {
        _queue_count++;
    }
    //-- Do we have a message to send and is it time to forward data?
    if(_queue_count && (_queue_count == UAS_QUEUE_SIZE || (millis() - _queue_time) > UAS_QUEUE_TIMEOUT)) {
        _forwardTo->sendMessage(_message, _queue_count);
        memset(_message, 0, sizeof(_message));
        _queue_count = 0;
        _queue_time  = millis();
    }
}

//---------------------------------------------------------------------------------
//-- Send MavLink message to UAS
void
MavESP8266Vehicle::sendMessage(mavlink_message_t* message, int count) {
    for(int i = 0; i < count; i++) {
        sendMessage(&message[i]);
    }
}

//---------------------------------------------------------------------------------
//-- Send MavLink message to UAS
void
MavESP8266Vehicle::sendMessage(mavlink_message_t* message) {
    // Translate message to buffer
    char buf[300];
    unsigned len = mavlink_msg_to_send_buffer((uint8_t*)buf, message);
    // Send it
    Serial.write((uint8_t*)(void*)buf, len);
    _status.packets_sent++;
}

//---------------------------------------------------------------------------------
//-- We have some special status to compute when asked for
linkStatus*
MavESP8266Vehicle::getStatus()
{
    float buffer_size = (float)UAS_QUEUE_SIZE;
    float buffer_left = (float)(UAS_QUEUE_SIZE - _queue_count);
    _status.queue_status = (uint8_t)((buffer_left / buffer_size) * 100.0f);
    return &_status;
}

//---------------------------------------------------------------------------------
//-- Read MavLink message from UAS
bool
MavESP8266Vehicle::_readMessage()
{
    bool msgReceived = false;
    mavlink_status_t uas_status;
    while(Serial.available())
    {
        int result = Serial.read();
        if (result >= 0)
        {
            // Parsing
            msgReceived = mavlink_parse_char(MAVLINK_COMM_1, result, &_message[_queue_count], &uas_status);
            if(msgReceived) {
                _status.packets_received++;
                //-- Is this the first packet we got?
                if(!_heard_from) {
                    if(_message[_queue_count].msgid == MAVLINK_MSG_ID_HEARTBEAT) {
                        _heard_from     = true;
                        _component_id   = _message[_queue_count].compid;
                        _system_id      = _message[_queue_count].sysid;
                        _seq_expected   = _message[_queue_count].seq + 1;
                    }
                } else {
                    _checkLinkErrors(&_message[_queue_count]);
                }
                break;
            }
        }
    }
    return msgReceived;
}