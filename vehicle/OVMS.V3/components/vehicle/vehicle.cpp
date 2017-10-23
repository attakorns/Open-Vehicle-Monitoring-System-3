/*
;    Project:       Open Vehicle Monitor System
;    Date:          14th March 2017
;
;    Changes:
;    1.0  Initial release
;
;    (C) 2011       Michael Stegen / Stegen Electronics
;    (C) 2011-2017  Mark Webb-Johnson
;    (C) 2011        Sonny Chen @ EPRO/DX
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in
; all copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
; THE SOFTWARE.
*/

#include "esp_log.h"
static const char *TAG = "vehicle";

#include <stdio.h>
#include <ovms_command.h>
#include <ovms_metrics.h>
#include <metrics_standard.h>
#include "vehicle.h"

OvmsVehicleFactory MyVehicleFactory __attribute__ ((init_priority (2000)));

void vehicle_module(int verbosity, OvmsWriter* writer, OvmsCommand* cmd, int argc, const char* const* argv)
  {
  if (argc == 0)
    {
    MyVehicleFactory.ClearVehicle();
    }
  else
    {
    MyVehicleFactory.SetVehicle(argv[0]);
    }
  }

OvmsVehicleFactory::OvmsVehicleFactory()
  {
  ESP_LOGI(TAG, "Initialising VEHICLE Factory (2000)");

  m_currentvehicle = NULL;

  OvmsCommand* cmd_vehicle = MyCommandApp.RegisterCommand("vehicle","Vehicle framework",NULL,"",1,1);
  cmd_vehicle->RegisterCommand("module","Set (or clear) vehicle module",vehicle_module,"<type>",0,1);
  }

OvmsVehicleFactory::~OvmsVehicleFactory()
  {
  if (m_currentvehicle)
    {
    delete m_currentvehicle;
    m_currentvehicle = NULL;
    }
  }

OvmsVehicle* OvmsVehicleFactory::NewVehicle(std::string VehicleType)
  {
  OvmsVehicleFactory::map_type::iterator iter = m_map.find(VehicleType);
  if (iter != m_map.end())
    {
    return iter->second();
    }
  return NULL;
  }

void OvmsVehicleFactory::ClearVehicle()
  {
  if (m_currentvehicle)
    {
    delete m_currentvehicle;
    m_currentvehicle = NULL;
    StandardMetrics.ms_v_type->SetValue("");
    }
  }

void OvmsVehicleFactory::SetVehicle(std::string type)
  {
  if (m_currentvehicle)
    {
    delete m_currentvehicle;
    m_currentvehicle = NULL;
    }
  m_currentvehicle = NewVehicle(type);
  StandardMetrics.ms_v_type->SetValue(type.c_str());
  }

static void OvmsVehicleRxTask(void *pvParameters)
  {
  OvmsVehicle *me = (OvmsVehicle*)pvParameters;
  me->RxTask();
  }

OvmsVehicle::OvmsVehicle()
  {
  m_can1 = NULL;
  m_can2 = NULL;
  m_can3 = NULL;
  m_ticker = 0;
  m_registeredlistener = false;

  m_poll_state = 0;
  m_poll_bus = NULL;
  m_poll_plist = NULL;
  m_poll_plcur = NULL;
  m_poll_ticker = 0;
  m_poll_moduleid_sent = 0;
  m_poll_moduleid_low = 0;
  m_poll_moduleid_high = 0;
  m_poll_type = 0;
  m_poll_pid = 0;
  m_poll_ml_remain = 0;
  m_poll_ml_offset = 0;
  m_poll_ml_frame = 0;

  m_rxqueue = xQueueCreate(20,sizeof(CAN_frame_t));
  xTaskCreatePinnedToCore(OvmsVehicleRxTask, "Vrx Task", 4096, (void*)this, 5, &m_rxtask, 1);

  using std::placeholders::_1;
  using std::placeholders::_2;
  MyEvents.RegisterEvent(TAG, "ticker.1", std::bind(&OvmsVehicle::VehicleTicker1, this, _1, _2));
  }

OvmsVehicle::~OvmsVehicle()
  {
  if (m_can1) m_can1->SetPowerMode(Off);
  if (m_can2) m_can2->SetPowerMode(Off);
  if (m_can3) m_can3->SetPowerMode(Off);

  if (m_registeredlistener)
    {
    MyCan.DeregisterListener(m_rxqueue);
    m_registeredlistener = false;
    }

  vQueueDelete(m_rxqueue);
  vTaskDelete(m_rxtask);

  MyEvents.DeregisterEvent(TAG);
  }

const std::string OvmsVehicle::VehicleName()
  {
  return std::string("unknown");
  }

void OvmsVehicle::RxTask()
  {
  CAN_frame_t frame;

  while(1)
    {
    if (xQueueReceive(m_rxqueue, &frame, (portTickType)portMAX_DELAY)==pdTRUE)
      {
      if ((frame.origin == m_poll_bus)&&(m_poll_plist))
        {
        // This is intended for our poller
        // ESP_LOGI(TAG, "Poller Rx candidate ID=%03x (expecting %03x-%03x)",frame.MsgID,m_poll_moduleid_low,m_poll_moduleid_high);
        if ((frame.MsgID >= m_poll_moduleid_low)&&(frame.MsgID <= m_poll_moduleid_high))
          {
          PollerReceive(&frame);
          }
        }
      if (m_can1 == frame.origin) IncomingFrameCan1(&frame);
      else if (m_can2 == frame.origin) IncomingFrameCan2(&frame);
      else if (m_can3 == frame.origin) IncomingFrameCan3(&frame);
      }
    }
  }

void OvmsVehicle::IncomingFrameCan1(CAN_frame_t* p_frame)
  {
  }

void OvmsVehicle::IncomingFrameCan2(CAN_frame_t* p_frame)
  {
  }

void OvmsVehicle::IncomingFrameCan3(CAN_frame_t* p_frame)
  {
  }

void OvmsVehicle::IncomingPollReply(canbus* bus, uint16_t type, uint16_t pid, uint8_t* data, uint8_t length, uint16_t mlremain)
  {
  }

void OvmsVehicle::RegisterCanBus(int bus, CAN_mode_t mode, CAN_speed_t speed)
  {
  switch (bus)
    {
    case 1:
      m_can1 = (canbus*)MyPcpApp.FindDeviceByName("can1");
      m_can1->SetPowerMode(On);
      m_can1->Start(mode,speed);
      break;
    case 2:
      m_can2 = (canbus*)MyPcpApp.FindDeviceByName("can2");
      m_can2->SetPowerMode(On);
      m_can2->Start(mode,speed);
      break;
    case 3:
      m_can3 = (canbus*)MyPcpApp.FindDeviceByName("can3");
      m_can3->SetPowerMode(On);
      m_can3->Start(mode,speed);
      break;
    default:
      break;
    }

  if (!m_registeredlistener)
    {
    m_registeredlistener = true;
    MyCan.RegisterListener(m_rxqueue);
    }
  }

void OvmsVehicle::VehicleTicker1(std::string event, void* data)
  {
  m_ticker++;

  if (m_poll_plist)
    {
    PollerSend();
    }

  Ticker1(m_ticker);
  if ((m_ticker % 10) == 0) Ticker10(m_ticker);
  if ((m_ticker % 60) == 0) Ticker60(m_ticker);
  if ((m_ticker % 300) == 0) Ticker300(m_ticker);
  if ((m_ticker % 600) == 0) Ticker600(m_ticker);
  if ((m_ticker % 3600) == 0) Ticker3600(m_ticker);
  }

void OvmsVehicle::Ticker1(uint32_t ticker)
  {
  }

void OvmsVehicle::Ticker10(uint32_t ticker)
  {
  }

void OvmsVehicle::Ticker60(uint32_t ticker)
  {
  }

void OvmsVehicle::Ticker300(uint32_t ticker)
  {
  }

void OvmsVehicle::Ticker600(uint32_t ticker)
  {
  }

void OvmsVehicle::Ticker3600(uint32_t ticker)
  {
  }

void OvmsVehicle::PollSetPidList(canbus* bus, poll_pid_t* plist)
  {
  m_poll_bus = bus;
  m_poll_plist = plist;
  }

void OvmsVehicle::PollSetState(uint8_t state)
  {
  if ((state < VEHICLE_POLL_NSTATES)&&(state != m_poll_state))
    {
    m_poll_state = state;
    m_poll_ticker = 0;
    m_poll_plcur = NULL;
    }
  }

void OvmsVehicle::PollerSend()
  {
  if (m_poll_plcur == NULL) m_poll_plcur = m_poll_plist;

  while (m_poll_plcur->txmoduleid != 0)
    {
    if ((m_poll_plcur->polltime[m_poll_state] > 0)&&
        ((m_poll_ticker % m_poll_plcur->polltime[m_poll_state] ) == 0))
      {
      // We need to poll this one...
      m_poll_type = m_poll_plcur->type;
      m_poll_pid = m_poll_plcur->pid;
      if (m_poll_plcur->rxmoduleid != 0)
        {
        // send to <moduleid>, listen to response from <rmoduleid>:
        m_poll_moduleid_sent = m_poll_plcur->txmoduleid;
        m_poll_moduleid_low = m_poll_plcur->rxmoduleid;
        m_poll_moduleid_high = m_poll_plcur->rxmoduleid;
        }
      else
        {
        // broadcast: send to 0x7df, listen to all responses:
        m_poll_moduleid_sent = 0x7df;
        m_poll_moduleid_low = 0x7e8;
        m_poll_moduleid_high = 0x7ef;
        }

      // ESP_LOGI(TAG, "Polling for %d/%02x (expecting %03x/%03x-%03x)",
      //   m_poll_type,m_poll_pid,m_poll_moduleid_sent,m_poll_moduleid_low,m_poll_moduleid_high);
      CAN_frame_t txframe;
      memset(&txframe,0,sizeof(txframe));
      txframe.origin = m_poll_bus;
      txframe.MsgID = m_poll_moduleid_sent;
      txframe.FIR.B.FF = CAN_frame_std;
      txframe.FIR.B.DLC = 8;
      switch (m_poll_plcur->type)
        {
        case VEHICLE_POLL_TYPE_OBDIICURRENT:
        case VEHICLE_POLL_TYPE_OBDIIFREEZE:
        case VEHICLE_POLL_TYPE_OBDIISESSION:
          // 8 bit PID request for single frame response:
          txframe.data.u8[0] = 0x02;
          txframe.data.u8[1] = m_poll_type;
          txframe.data.u8[2] = m_poll_pid;
          break;
        case VEHICLE_POLL_TYPE_OBDIIVEHICLE:
        case VEHICLE_POLL_TYPE_OBDIIGROUP:
          // 8 bit PID request for multi frame response:
          m_poll_ml_remain = 0;
          txframe.data.u8[0] = 0x02;
          txframe.data.u8[1] = m_poll_type;
          txframe.data.u8[2] = m_poll_pid;
          break;
        case VEHICLE_POLL_TYPE_OBDIIEXTENDED:
          // 16 bit PID request:
          txframe.data.u8[0] = 0x03;
          txframe.data.u8[1] = VEHICLE_POLL_TYPE_OBDIIEXTENDED;    // Get extended PID
          txframe.data.u8[2] = m_poll_pid >> 8;
          txframe.data.u8[3] = m_poll_pid & 0xff;
          break;
        }
      m_poll_bus->Write(&txframe);
      m_poll_plcur++;
      return;
      }
    m_poll_plcur++;
    }

  m_poll_plcur = m_poll_plist;
  m_poll_ticker++;
  if (m_poll_ticker > 3600) m_poll_ticker -= 3600;
  }

void OvmsVehicle::PollerReceive(CAN_frame_t* frame)
  {
  // ESP_LOGI(TAG, "Receive Poll Response for %d/%02x",m_poll_type,m_poll_pid);
  switch (m_poll_type)
    {
    case VEHICLE_POLL_TYPE_OBDIICURRENT:
    case VEHICLE_POLL_TYPE_OBDIIFREEZE:
    case VEHICLE_POLL_TYPE_OBDIISESSION:
      // 8 bit PID single frame response:
      if ((frame->data.u8[1] == 0x40+m_poll_type)&&
          (frame->data.u8[2] == m_poll_pid))
        {
        IncomingPollReply(m_poll_bus, m_poll_type, m_poll_pid, &frame->data.u8[3], 5, 0);
        return;
        }
      break;
    case VEHICLE_POLL_TYPE_OBDIIVEHICLE:
    case VEHICLE_POLL_TYPE_OBDIIGROUP:
      // 8 bit PID multiple frame response:
      if (((frame->data.u8[0]>>4) == 0x1)&&
          (frame->data.u8[2] == 0x40+m_poll_type)&&
          (frame->data.u8[3] == m_poll_pid))
        {
        // First frame; send flow control frame:
        CAN_frame_t txframe;
        memset(&txframe,0,sizeof(txframe));
        txframe.origin = m_poll_bus;
        txframe.FIR.B.FF = CAN_frame_std;
        txframe.FIR.B.DLC = 8;

        if (m_poll_moduleid_sent == 0x7df)
          {
          // broadcast request: derive module ID from response ID:
          // (Note: this only works for the SAE standard ID scheme)
          txframe.MsgID = frame->MsgID - 8;
          }
        else
          {
          // use known module ID:
          txframe.MsgID = m_poll_moduleid_sent;
          }

        txframe.data.u8[0] = 0x30; // flow control frame type
        txframe.data.u8[1] = 0x00; // request all frames available
        txframe.data.u8[2] = 0x19; // with 25ms send interval
        m_poll_bus->Write(&txframe);

        // prepare frame processing, first frame contains first 3 bytes:
        m_poll_ml_remain = (((uint16_t)(frame->data.u8[0]&0x0f))<<8) + frame->data.u8[1] - 3;
        m_poll_ml_offset = 3;
        m_poll_ml_frame = 0;
        // ESP_LOGI(TAG, "Poll ML first frame (frame=%d, remain=%d)",m_poll_ml_frame,m_poll_ml_remain);
        IncomingPollReply(m_poll_bus, m_poll_type, m_poll_pid, &frame->data.u8[5], 3, m_poll_ml_remain);
        return;
        }
      else if (((frame->data.u8[0]>>4)==0x2)&&(m_poll_ml_remain>0))
        {
        // Consecutive frame (1 control + 7 data bytes)
        uint16_t len;
        if (m_poll_ml_remain>7)
          {
          m_poll_ml_remain -= 7;
          m_poll_ml_offset += 7;
          len = 7;
          }
        else
          {
          len = m_poll_ml_remain;
          m_poll_ml_offset += m_poll_ml_remain;
          m_poll_ml_remain = 0;
          }
        m_poll_ml_frame++;
        // ESP_LOGI(TAG, "Poll ML subsequent frame (frame=%d, remain=%d)",m_poll_ml_frame,m_poll_ml_remain);
        IncomingPollReply(m_poll_bus, m_poll_type, m_poll_pid, &frame->data.u8[1], len, m_poll_ml_remain);
        return;
        }
      break;
    case VEHICLE_POLL_TYPE_OBDIIEXTENDED:
      // 16 bit PID response:
      if ((frame->data.u8[1] == 0x62)&&
          ((frame->data.u8[3]+(((uint16_t) frame->data.u8[2]) << 8)) == m_poll_pid))
        {
        IncomingPollReply(m_poll_bus, m_poll_type, m_poll_pid, &frame->data.u8[4], 4, 0);
        return;
        }
      break;
    }
  }

