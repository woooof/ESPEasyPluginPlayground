//#######################################################################################################
//#################################### Plugin 053: Plantower PMS5003ST ####################################
//#######################################################################################################
//
// http://www.aqmd.gov/docs/default-source/aq-spec/resources-page/plantower-pms5003-manual_v2-3.pdf?sfvrsn=2
//
// The PMS5003ST are particle sensors. Particles are measured by blowing air through the enclosure and,
// together with a laser, count the amount of particles. These sensors have an integrated microcontroller
// that counts particles and transmits measurement data over the serial connection.

#ifdef PLUGIN_BUILD_TESTING

#include <ESPeasySoftwareSerial.h>

#define PLUGIN_216
#define PLUGIN_ID_216 216
#define PLUGIN_NAME_216 "Dust - PMS5003ST"
#define PLUGIN_VALUENAME1_216 "pm2.5"
#define PLUGIN_VALUENAME2_216 "TEMP"
#define PLUGIN_VALUENAME3_216 "HUMI"
#define PLUGIN_VALUENAME4_216 "HCHO"
#define PMS5003ST_SIG1 0X42
#define PMS5003ST_SIG2 0X4d
#define PMS5003ST_SIZE 40

ESPeasySoftwareSerial *swSerial = NULL;
boolean Plugin_216_init = false;
boolean values_received = false;

// Read 2 bytes from serial and make an uint16 of it. Additionally calculate
// checksum for PMS5003ST. Assumption is that there is data available, otherwise
// this function is blocking.
void SerialRead16(uint16_t* value, uint16_t* checksum)
{
  uint8_t data_high, data_low;

  // If swSerial is initialized, we are using soft serial
  if (swSerial != NULL)
  {
    data_high = swSerial->read();
    data_low = swSerial->read();
  }
  else
  {
    data_high = Serial.read();
    data_low = Serial.read();
  }

  *value = data_low;
  *value |= (data_high << 8);

  if (checksum != NULL)
  {
    *checksum += data_high;
    *checksum += data_low;
  }

#if 0
  // Low-level logging to see data from sensor
  String log = F("PMS5003ST : byte high=0x");
  log += String(data_high,HEX);
  log += F(" byte low=0x");
  log += String(data_low,HEX);
  log += F(" result=0x");
  log += String(*value,HEX);
  addLog(LOG_LEVEL_INFO, log);
#endif
}

boolean PacketAvailable(void)
{
  boolean success = false;

  if (swSerial != NULL) // Software serial
  {
    // When there is enough data in the buffer, search through the buffer to
    // find header (buffer may be out of sync)
    while (swSerial->available() >= PMS5003ST_SIZE)
    {
      if (swSerial->read() == PMS5003ST_SIG1 && swSerial->read() == PMS5003ST_SIG2)
      {
        success = true;
        break;
      }
    }
  }
  else // Hardware serial
  {
    // When there is enough data in the buffer, search through the buffer to
    // find header (buffer may be out of sync)
    while (Serial.available() >= PMS5003ST_SIZE)
    {
      if (Serial.read() == PMS5003ST_SIG1 && Serial.read() == PMS5003ST_SIG2)
      {
        success = true;
        break;
      }
    }
  }
  return success;
}

boolean Plugin_216(byte function, struct EventStruct *event, String& string)
{
  String log;
  boolean success = false;

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_216;
        Device[deviceCount].Type = DEVICE_TYPE_TRIPLE;
        Device[deviceCount].VType = SENSOR_TYPE_TRIPLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = true;
        Device[deviceCount].FormulaOption = true;
        Device[deviceCount].ValueCount = 4;
        Device[deviceCount].SendDataOption = true;
        Device[deviceCount].TimerOption = true;
        Device[deviceCount].GlobalSyncOption = true;
        success = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_216);
        success = true;
        break;
      }

    case PLUGIN_GET_DEVICEVALUENAMES:
      {
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_216));
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[1], PSTR(PLUGIN_VALUENAME2_216));
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[2], PSTR(PLUGIN_VALUENAME3_216));
        strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[3], PSTR(PLUGIN_VALUENAME4_216));
        success = true;
        break;
      }

      case PLUGIN_GET_DEVICEGPIONAMES:
        {
          event->String1 = F("GPIO &larr; TX");
          event->String2 = F("GPIO &rarr; RX");
          event->String3 = F("GPIO &rarr; Reset");
          break;
        }

    case PLUGIN_INIT:
      {
        int rxPin = Settings.TaskDevicePin1[event->TaskIndex];
        int txPin = Settings.TaskDevicePin2[event->TaskIndex];
        int resetPin = Settings.TaskDevicePin3[event->TaskIndex];

        log = F("PMS5003ST : config ");
        log += rxPin;
        log += txPin;
        log += resetPin;
        addLog(LOG_LEVEL_DEBUG, log);

        // Hardware serial is RX on 3 and TX on 1
        if (rxPin == 3 && txPin == 1)
        {
          log = F("PMS5003ST : using hardware serial");
          addLog(LOG_LEVEL_INFO, log);
          Serial.begin(9600);
          Serial.flush();
        }
        else
        {
          log = F("PMS5003ST: using software serial");
          addLog(LOG_LEVEL_INFO, log);
          swSerial = new ESPeasySoftwareSerial(rxPin, txPin);
          swSerial->begin(9600);
          swSerial->flush();
        }

        if (resetPin >= 0) // Reset if pin is configured
        {
          // Toggle 'reset' to assure we start reading header
          log = F("PMS5003ST: resetting module");
          addLog(LOG_LEVEL_INFO, log);
          pinMode(resetPin, OUTPUT);
          digitalWrite(resetPin, LOW);
          delay(250);
          digitalWrite(resetPin, HIGH);
          pinMode(resetPin, INPUT_PULLUP);
        }

        Plugin_216_init = true;
        success = true;
        break;
      }
    // The update rate from the module is 200ms .. multiple seconds. Practise
    // shows that we need to read the buffer many times per seconds to stay in
    // sync.
    case PLUGIN_TEN_PER_SECOND:
      {
        if (Plugin_216_init)
        {
          uint16_t checksum = 0, checksum2 = 0;
          uint16_t framelength = 0;
          uint16_t data[17];
          // byte data_low, data_high;
          int i = 0;

          // Check if a packet is available in the UART FIFO.
          if (PacketAvailable())
          {
            log = F("PMS5003ST : Packet available");
            addLog(LOG_LEVEL_DEBUG_MORE, log);
            checksum += PMS5003ST_SIG1 + PMS5003ST_SIG2;
            SerialRead16(&framelength, &checksum);
            if (framelength != (PMS5003ST_SIZE - 4))
            {
              log = F("PMS5003ST : invalid framelength - ");
              log += framelength;
              addLog(LOG_LEVEL_ERROR, log);
              break;
            }

            for (i = 0; i < 17; i++)
              SerialRead16(&data[i], &checksum);

            log = F("PMS5003ST : pm1.0=");
            log += data[0];
            log += F(", pm2.5=");
            log += data[1];
            log += F(", pm10=");
            log += data[2];
            log += F(", pm1.0a=");
            log += data[3];
            log += F(", pm2.5a=");
            log += data[4];
            log += F(", pm10a=");
            log += data[5];
            addLog(LOG_LEVEL_DEBUG, log);

            log = F("PMS5003ST : count/0.1L : 0.3um=");
            log += data[6];
            log += F(", 0.5um=");
            log += data[7];
            log += F(", 1.0um=");
            log += data[8];
            log += F(", 2.5um=");
            log += data[9];
            log += F(", 5.0um=");
            log += data[10];
            log += F(", 10um=");
            log += data[11];
            log += F(", HCHO=");
            log += data[12];
            log += F(", TEMP=");
            log += float(data[13]/10.00);
            log += F(", HUMI=");
            log += float(data[14]/10.00);
            addLog(LOG_LEVEL_DEBUG_MORE, log);

            // Compare checksums
            SerialRead16(&checksum2, NULL);
            if (checksum == checksum2)
            {
              // Data is checked and good, fill in output
              UserVar[event->BaseVarIndex]     = data[4];
              UserVar[event->BaseVarIndex + 1] = float(data[13]/10.00);//data[13]/10;
              UserVar[event->BaseVarIndex + 2] = float(data[14]/10.00);//data[14]/10;
              UserVar[event->BaseVarIndex + 3] = float(data[12]/1000.000);//data[12]/1000;
              values_received = true;
              success = true;
            }
          }
        }
        break;
      }
    case PLUGIN_READ:
      {
        // When new data is available, return true
        success = values_received;
        values_received = false;
      }
  }
  return success;
}
#endif // PLUGIN_BUILD_TESTING
