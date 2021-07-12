/*
  TPLinkSmartPlug.h - TP-LINK WiFi Smart Plug functions

  HS-100/HS-105/HS-110 Supported.

  Copyright (c) 2020 Sasapea's Lab. All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General
  Public License along with this library; if not, write to the
  Free Software Foundation, Inc., 59 Temple Place, Suite 330,
  Boston, MA  02111-1307  USA
*/
#ifndef _TPLINKSMARTPLUG_H
#define _TPLINKSMARTPLUG_H

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <Arduino.h>
#include <Client.h>
#include <Udp.h>
#include <IPAddress.h>

#define TPLINK_SMARTPLUG_DISCOVERY_BROADCAST "255.255.255.255"
#define TPLINK_SMARTPLUG_DISCOVERY_INTERVAL  60000
#define TPLINK_SMARTPLUG_DISCOVERY_TIMEOUT   10000
#define TPLINK_SMARTPLUG_PORT 9999
#define TPLINK_SMARTPLUG_KEY  171

class TPLinkSmartPlug
{
  private:
    typedef struct {
      String mac;
      String alias;
      IPAddress addr;
    } device_t;

    Client *_client;
    UDP    *_udp;
    String  _target;
    std::vector<device_t>  _device_list;
    std::vector<IPAddress> _target_list;
    std::unordered_set<std::string> _target_set;
    struct {
      uint32_t size;
      char data[768];
    } _packet;
    uint32_t _interval;

    void _htonl(uint32_t *n, uint32_t h)
    {
      ((uint8_t *)n)[0] = h >> 24;
      ((uint8_t *)n)[1] = h >> 16;
      ((uint8_t *)n)[2] = h >>  8;
      ((uint8_t *)n)[3] = h;
    }

    uint32_t _ntohl(uint32_t *n)
    {
      return ((uint32_t)((uint8_t *)n)[0] << 24)
           | ((uint32_t)((uint8_t *)n)[1] << 16)
           | ((uint16_t)((uint8_t *)n)[2] <<  8)
           | ((uint8_t *)n)[3];
    }

    void encrypt(char *buf, size_t len)
    {
      char key = TPLINK_SMARTPLUG_KEY;
      for (uint32_t i = 0; i < len; ++i)
        buf[i] = key = key ^ buf[i];
    }

    void decrypt(char *buf, size_t len)
    {
      char key = TPLINK_SMARTPLUG_KEY;
      for (uint32_t i = 0; i < len; ++i)
      {
        char b = buf[i];
        buf[i] = key ^ b;
        key = b;
      }
    }

    void encryptPacket()
    {
      encrypt(_packet.data, _packet.size);
      _htonl(&_packet.size, _packet.size);
    }

    void decryptPacket()
    {
      _packet.size = _ntohl(&_packet.size);
      decrypt(_packet.data, _packet.size);
      _packet.data[_packet.size] = 0;
    }

    bool read(uint8_t *buf, size_t size)
    {
      ssize_t n;
      for (size_t i = 0; i < size; i += n)
      {
        n = _client->read(buf + i, size - i);
        if (n <= 0)
        {
          if (!_client->connected())
            return false;
          n = 0;
        }
      }
      return true;
    }

    bool control()
    {
      bool rv = false;
      size_t size = sizeof(_packet.size) + _packet.size;
      encryptPacket();
      if (_client->write((uint8_t *)&_packet, size) == size)
      {
        if (read((uint8_t *)&_packet.size, sizeof(_packet.size)))
        {
          size = _ntohl(&_packet.size);
          if (size < sizeof(_packet.data) - 1)
          {
            if (read((uint8_t *)&_packet.data, size))
            {
              decryptPacket();
              rv = true;
            }
          }
        }
      }
      _client->stop();
      return rv;
    }

    bool connect(IPAddress addr)
    {
      if (_client->connect(addr, TPLINK_SMARTPLUG_PORT))
        return true;
      for (auto it = _device_list.begin(); it != _device_list.end(); ++it)
      {
        if (it->addr == addr)
        {
          _target_set.erase(it->alias.c_str());
          _target_set.erase(it->mac.c_str());
          _device_list.erase(it);
          break;
        }
      }
      return false;
    }

    //
    // TP-Link SmartPlug Discovery Protocol (Broadcast)
    //
    bool discovery_start()
    {
      size_t len = snprintf(_packet.data, sizeof(_packet.data), "%s", "{\"system\":{\"get_sysinfo\":null}}");
      encrypt(_packet.data, len);
      if (_udp->beginPacket(TPLINK_SMARTPLUG_DISCOVERY_BROADCAST, TPLINK_SMARTPLUG_PORT))
      {
        _udp->write((uint8_t *)_packet.data, len);
        return _udp->endPacket();
      }
      return false;
    }

    bool getValue(String& value, const char *name)
    {
      char *p = strstr(_packet.data, name);
      if (p)
      {
        p += strlen(name);
        char *q = strchr(p, '\"');
        if (q)
        {
          *q = 0;
          value = p;
          *q = '\"';
          return true;
        }
      }
      return false;
    }

    //
    // Receive Discovery Response
    //
    bool discovery_response()
    {
      bool rv = false;
      int len;
      while ((len = _udp->parsePacket()) > 0)
      {
        if (_udp->read(_packet.data, len) == len)
        {
          const char MAC[]   = "\"mac\":\"";
          const char ALIAS[] = "\"alias\":\"";
          decrypt(_packet.data, len);
          //
          // Parse JSON String
          //
          device_t info = {"", "", _udp->remoteIP()};
          if (getValue(info.mac, MAC) && getValue(info.alias, ALIAS))
          {
            bool append = true;
            for (auto it = _device_list.begin(); it != _device_list.end(); ++it)
            {
              if (it->mac == info.mac)
              {
                it->alias = info.alias;
                it->addr = info.addr;
                append = false;
                break;
              }
              else if (it->mac > info.mac)
              {
                _device_list.insert(it, info);
                append = false;
                break;
              }
            }
            if (append)
              _device_list.push_back(info);
            if ((info.alias == _target) || (info.mac == _target))
              rv = true;
          }
        }
      }
      return rv;
    }

    void discovery_wait()
    {
      uint32_t t = millis();
      while (millis() - t < TPLINK_SMARTPLUG_DISCOVERY_TIMEOUT)
      {
        if (discovery_response())
            break;
      }
    }

    size_t collect()
    {
      _target_list.clear();
      for(auto it = _device_list.begin(); it != _device_list.end(); ++it)
      {
        if ((it->alias == _target) || (it->mac == _target))
          _target_list.push_back(it->addr);
      }
      return _target_list.size();
    }

    size_t targets()
    {
      IPAddress addr;
      if (addr.fromString(_target))
      {
        _target_list.clear();
        _target_list.push_back(addr);
        return 1;
      }
      return collect();
    }

  public:
    TPLinkSmartPlug()
    {
    }

    void begin(Client& client, UDP& udp)
    {
      _client = &client;
      _udp = &udp;
      udp.begin(0);
      _interval = millis();
    }

    void setTarget(const char *target)
    {
      IPAddress addr;
      _target = target;
      if (!addr.fromString(target) && (_target_set.count(target) == 0))
      {
        _target_set.insert(target);
        if (collect() == 0)
        {
          if (discovery_start())
            discovery_wait();
        }
      }
    }

    void setTarget(const String target)
    {
      setTarget(target.c_str());
    }

    char *getSysInfo()
    {
      char *rv = NULL;
      if (targets() == 1)
      {
        if (connect(_target_list[0]))
        {
          _packet.size = snprintf(_packet.data, sizeof(_packet.data), "{\"system\":{\"get_sysinfo\":null}}");
          if (control())
            rv = _packet.data;
        }
      }
      return rv;
    }

    size_t reboot(uint8_t delay = 1)
    {
      size_t rv = 0;
      if (targets())
      {
        for(auto it = _target_list.begin(); it != _target_list.end(); ++it)
        {
          if (connect(*it))
          {
            _packet.size = snprintf(_packet.data, sizeof(_packet.data), "{\"system\":{\"reboot\":{\"delay\":%d}}}", delay);
            if (control())
              ++rv;
          }
        }
      }
      return rv;
    }

    size_t setRelayState(bool on)
    {
      size_t rv = 0;
      if (targets())
      {
        for(auto it = _target_list.begin(); it != _target_list.end(); ++it)
        {
          if (connect(*it))
          {
            _packet.size = snprintf(_packet.data, sizeof(_packet.data), "{\"system\":{\"set_relay_state\":{\"state\":%c}}}", on ? '1' : '0');
            if (control())
              ++rv;
          }
        }
      }
      return rv;
    }

    size_t setLedOff(bool off)
    {
      size_t rv = 0;
      if (targets())
      {
        for(auto it = _target_list.begin(); it != _target_list.end(); ++it)
        {
          if (connect(*it))
          {
            _packet.size = snprintf(_packet.data, sizeof(_packet.data), "{\"system\":{\"set_led_off\":{\"off\":%c}}}", off ? '1' : '0');
            if (control())
              ++rv;
          }
        }
      }
      return rv;
    }

    void handle()
    {
      if (millis() - _interval >= TPLINK_SMARTPLUG_DISCOVERY_INTERVAL)
      {
        _interval += TPLINK_SMARTPLUG_DISCOVERY_INTERVAL;
        discovery_start();
      }
      discovery_response();
    }

    void delay(uint32_t ms)
    {
      uint32_t t = millis();
      while (millis() - t < ms)
        handle();
    }
};

#endif // _TPLINKSMARTPLUG_H
