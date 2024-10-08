/*
TinyGPS - a small GPS library for Arduino providing basic NMEA parsing
Based on work by and "distance_to" and "course_to" courtesy of Maarten Lamers.
Suggestion to add satellites(), course_to(), and cardinal(), by Matt Monson.
Precision improvements suggested by Wayne Holder.
Copyright (C) 2008-2013 Mikal Hart
All rights reserved.

10/20/2023  Modifed by Dan McLachlan (drmclach@live.com)
      Applied Kevin Walton's PUBX updates to v13 and improved support for 
      PUBX,00 and PUBX,04 messages.
      Also pulled in changes from the Teensy distribution adding
      GPGSA, GNRMC, GNGNS, GNGSA, GPGSV and GLGSV
      with tracked satelites and constellations 

5/9/2012	Modified by Kevin Walton (kevin@unseen.org)
			Applied Terry Baume's PUBX updates to v12
			Updates are marked //Kevin
			Terrys sats() is replaced by v12's native satellites()
			ToDo - Add Vertical velocity parsing
			Warning, only testing so far is using the example test harness "static_test.pde"

9/8/2010	Modified by Terry Baume (terry@bogaurd.net)
			Support for Ublox NMEA extension PUBX 00
			Method to retrieve number of sats tracked
			Adjusted invalid lock defaults

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "TinyGPS.h"

#define _GPRMC_TERM   "GPRMC"
#define _GPGGA_TERM   "GPGGA"
#define _GPGSA_TERM   "GPGSA"
#define _GNRMC_TERM   "GNRMC"
#define _GNGNS_TERM   "GNGNS"
#define _GNGSA_TERM   "GNGSA"
#define _GPGSV_TERM   "GPGSV"
#define _GLGSV_TERM   "GLGSV"
#define _GPZDA_TERM   "GPZDA"
#define _PUBX_TERM    "PUBX"

TinyGPS::TinyGPS()
  :  _time(GPS_INVALID_TIME)
  ,  _date(GPS_INVALID_DATE)
  ,  _latitude(GPS_INVALID_ANGLE)
  ,  _longitude(GPS_INVALID_ANGLE)
  ,  _altitude(GPS_INVALID_ALTITUDE)
  ,  _speed(GPS_INVALID_SPEED)
  ,  _course(GPS_INVALID_ANGLE)
  ,  _hdop(GPS_INVALID_HDOP)
  ,  _numsats(GPS_INVALID_SATELLITES)
  ,  _last_time_fix(GPS_INVALID_FIX_TIME)
  ,  _last_position_fix(GPS_INVALID_FIX_TIME)
  ,  _year(GPS_INVALID_DATE)
  ,  _month(GPS_INVALID_DATE)
  ,  _day(GPS_INVALID_DATE)
  ,  _last_date_fix(GPS_INVALID_FIX_TIME)
  ,  _parity(0)
  ,  _is_checksum_term(false)
  ,  _sentence_type(_GPS_SENTENCE_OTHER)
  ,  _UBX_message_type(0)
  ,  _term_number(0)
  ,  _term_offset(0)
  ,  _gps_data_good(false)
#ifndef _GPS_NO_STATS
  ,  _encoded_characters(0)
  ,  _good_sentences(0)
  ,  _failed_checksum(0)
#endif
{
  _term[0] = '\0';
}

//
// public methods
//

bool TinyGPS::encode(char c)
{
  bool valid_sentence = false;

#ifndef _GPS_NO_STATS
  ++_encoded_characters;
#endif
  switch(c)
  {
  case ',': // term terminators
    _parity ^= c;
  case '\r':
  case '\n':
  case '*':
    if (_term_offset < sizeof(_term))
    {
      _term[_term_offset] = 0;
      valid_sentence = term_complete();
    }
    ++_term_number;
    _term_offset = 0;
    _is_checksum_term = c == '*';
    return valid_sentence;

  case '$': // sentence begin
    _term_number = _term_offset = 0;
    _parity = 0;
    _sentence_type = _GPS_SENTENCE_OTHER;
    _is_checksum_term = false;
    _gps_data_good = false;
    return valid_sentence;
  }

  // ordinary characters
  if (_term_offset < sizeof(_term) - 1)
    _term[_term_offset++] = c;
  if (!_is_checksum_term)
    _parity ^= c;

  return valid_sentence;
}

#ifndef _GPS_NO_STATS
void TinyGPS::stats(unsigned long *chars, unsigned short *sentences, unsigned short *failed_cs)
{
  if (chars) *chars = _encoded_characters;
  if (sentences) *sentences = _good_sentences;
  if (failed_cs) *failed_cs = _failed_checksum;
}
#endif

//
// internal utilities
//
int TinyGPS::from_hex(char a) 
{
  if (a >= 'A' && a <= 'F')
    return a - 'A' + 10;
  else if (a >= 'a' && a <= 'f')
    return a - 'a' + 10;
  else
    return a - '0';
}

unsigned long TinyGPS::parse_decimal()
{
  char *p = _term;
  bool isneg = *p == '-';
  if (isneg) ++p;
  unsigned long ret = 100UL * gpsatol(p);
  while (gpsisdigit(*p)) ++p;
  if (*p == '.')
  {
    if (gpsisdigit(p[1]))
    {
      ret += 10 * (p[1] - '0');
      if (gpsisdigit(p[2]))
        ret += p[2] - '0';
    }
  }
  return isneg ? -ret : ret;
}

// Parse a string in the form ddmm.mmmmmmm...
unsigned long TinyGPS::parse_degrees()
{
  char *p;
  unsigned long left_of_decimal = gpsatol(_term);
  unsigned long hundred1000ths_of_minute = (left_of_decimal % 100UL) * 100000UL;
  for (p=_term; gpsisdigit(*p); ++p);
  if (*p == '.')
  {
    unsigned long mult = 10000;
    while (gpsisdigit(*++p))
    {
      hundred1000ths_of_minute += mult * (*p - '0');
      mult /= 10;
    }
  }
  return (left_of_decimal / 100) * 1000000 + (hundred1000ths_of_minute + 3) / 6;
}

#define COMBINE(sentence_type, term_number) (((unsigned)(sentence_type) << 5) | term_number)
#define UBX_MESSAGE(message_type) (((unsigned)(_GPS_SENTENCE_PUBX) << 5) | message_type)

// Processes a just-completed term
// Returns true if new sentence has just passed checksum test and is validated
bool TinyGPS::term_complete()
{
  if (_is_checksum_term)
  {
    byte checksum = 16 * from_hex(_term[0]) + from_hex(_term[1]);
    if (checksum == _parity)
    {
      /*
// DEBUG TEMP ADDITION
      Serial.printf("TinyGPS: sentence: ");
      switch(_sentence_type) 
      {
        case _GPS_SENTENCE_GPGGA:   Serial.printf("*%s ", _GPGGA_TERM);    break;
        case _GPS_SENTENCE_GPRMC:   Serial.printf("*%s ", _GPRMC_TERM);    break;
        case _GPS_SENTENCE_GNGNS:   Serial.printf("%s ", _GNGNS_TERM);    break;
        case _GPS_SENTENCE_GNGSA:   Serial.printf("%s ", _GNGSA_TERM);    break;
        case _GPS_SENTENCE_GPGSV:   Serial.printf("%s ", _GPGSV_TERM);    break;
        case _GPS_SENTENCE_GLGSV:   Serial.printf("%s ", _GLGSV_TERM);    break;
        case _GPS_SENTENCE_PUBX:    Serial.printf("%s%02d ", _PUBX_TERM, _UBX_message_type);    break;
        case _GPS_SENTENCE_OTHER:   Serial.printf("OTHER ");    break;
      }
      Serial.printf(" %s", _gps_data_good ? "Fix" : "No Fix - ");
// END DEBUG TEMP ADDITION
*/
     //set the time and date even if not tracking 
     if(_sentence_type == _GPS_SENTENCE_GPRMC || 
        ((_sentence_type == _GPS_SENTENCE_PUBX) && (_UBX_message_type == 4)))   // UBX,04 Time of Day and Clock Information
      {  
          _time      = _new_time;
          _date      = _new_date;
          _last_time_fix = _new_time_fix;
// temp debug
//Serial.printf(" setting time (%ld)/date (%ld)", _new_time, _new_date);
      }
// Temp Debug
//Serial.println();

      if (_sentence_type == _GPS_SENTENCE_GPZDA) // Date and Time information with full year info
      {
        _time = _new_time;
        _last_time_fix = _new_time_fix;
        _day = _new_day;
        _month = _new_month;
        _year = _new_year;
        _last_date_fix = _new_date_fix;
      }

      if (_gps_data_good)
      {
#ifndef _GPS_NO_STATS
        ++_good_sentences;
#endif
        _last_time_fix = _new_time_fix;
        _last_position_fix = _new_position_fix;

        switch(_sentence_type)
        {
        case _GPS_SENTENCE_GPRMC:
          _time      = _new_time;
          _date      = _new_date;
          _latitude  = _new_latitude;
          _longitude = _new_longitude;
          _speed     = _new_speed;
          _course    = _new_course;
          break;
        case _GPS_SENTENCE_GPGGA:
          _altitude  = _new_altitude;
          _time      = _new_time;
          _latitude  = _new_latitude;
          _longitude = _new_longitude;
          _numsats   = _new_numsats;
          _hdop      = _new_hdop;
          break;
        case _GPS_SENTENCE_PUBX:
          switch (_UBX_message_type) {
          case 0:                         // UBX,00 Lat/Long Position Data
            _time      = _new_time;
            _latitude  = _new_latitude;
            _longitude = _new_longitude;
            _speed     = _new_speed;
            _course    = _new_course;
	          _altitude  = _new_altitude;
	          _numsats   = _new_numsats;
            _hdop      = _new_hdop;
            break;
          }
          break;
        }

        return true;
      }
    }

#ifndef _GPS_NO_STATS
    else
      ++_failed_checksum;
#endif
    return false;
  }

  // the first term determines the sentence type
  if (_term_number == 0)
  {
    if (!gpsstrcmp(_term, _GPRMC_TERM) || !gpsstrcmp(_term, _GNRMC_TERM))
      _sentence_type = _GPS_SENTENCE_GPRMC;
    else if (!gpsstrcmp(_term, _GPGGA_TERM))
      _sentence_type = _GPS_SENTENCE_GPGGA;
    else if (!gpsstrcmp(_term, _GNGNS_TERM))
      _sentence_type = _GPS_SENTENCE_GNGNS;
    else if (!gpsstrcmp(_term, _GNGSA_TERM) || !gpsstrcmp(_term, _GPGSA_TERM))
      _sentence_type = _GPS_SENTENCE_GNGSA;
    else if (!gpsstrcmp(_term, _GPGSV_TERM))
      _sentence_type = _GPS_SENTENCE_GPGSV;
    else if (!gpsstrcmp(_term, _GLGSV_TERM))
      _sentence_type = _GPS_SENTENCE_GLGSV;
    else if (!gpsstrcmp(_term, _GPZDA_TERM))
      _sentence_type = _GPS_SENTENCE_GPZDA;
    else if (!gpsstrcmp(_term, _PUBX_TERM))
      _sentence_type = _GPS_SENTENCE_PUBX;
    else
      _sentence_type = _GPS_SENTENCE_OTHER;
    return false;
  }

  // PUBX messages, use 1st term to determine the message content 
  // save the message number
  if (_sentence_type == _GPS_SENTENCE_PUBX && _term_number == 1)
  {
    _UBX_message_type = gpsatol(_term);
#ifdef DEBUG    
    Serial.print("_GPS_SENTENCE_PUBX "); Serial.println(_UBX_message_type);
#endif
    return false;
  }

  // Dan - Added encoding of the sub message in the PUBX type
  if (_sentence_type != _GPS_SENTENCE_OTHER && _term[0])
  {
    unsigned int sentence_type = _sentence_type;
    if (_sentence_type == _GPS_SENTENCE_PUBX) {
      sentence_type = UBX_MESSAGE(_UBX_message_type);
    }
    switch(COMBINE(sentence_type, _term_number))
    {
    case COMBINE(_GPS_SENTENCE_GPRMC, 1): // Time in these sentences
    case COMBINE(_GPS_SENTENCE_GPGGA, 1):
    case COMBINE(_GPS_SENTENCE_GNGNS, 1):
    case COMBINE(_GPS_SENTENCE_GPZDA, 1):
    case COMBINE(UBX_MESSAGE(0), 2):      // UBX,00 Lat/Long Position Data
    case COMBINE(UBX_MESSAGE(4), 2):      // UBX,04 Time of Day and Clock Information
//Serial.printf("GPS: capturing time from sentence (%d) term (%d)\n", sentence_type, _term_number);
      _new_time = parse_decimal();
      _new_time_fix = millis();
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 2): // GPRMC validity
      _gps_data_good = _term[0] == 'A';
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 3): // Latitude
    case COMBINE(_GPS_SENTENCE_GPGGA, 2):
    case COMBINE(_GPS_SENTENCE_GNGNS, 2):
    case COMBINE(UBX_MESSAGE(0), 3):      // UBX,00 Lat/Long Position Data
      _new_latitude = parse_degrees();
      _new_position_fix = millis();
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 4): // N/S
    case COMBINE(_GPS_SENTENCE_GPGGA, 3):
    case COMBINE(_GPS_SENTENCE_GNGNS, 3):
    case COMBINE(UBX_MESSAGE(0), 4):      // UBX,00 Lat/Long Position Data
      if (_term[0] == 'S')
        _new_latitude = -_new_latitude;
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 5): // Longitude
    case COMBINE(_GPS_SENTENCE_GPGGA, 4):
    case COMBINE(_GPS_SENTENCE_GNGNS, 4):
    case COMBINE(UBX_MESSAGE(0), 5):      // UBX,00 Lat/Long Position Data
      _new_longitude = parse_degrees();
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 6): // E/W
    case COMBINE(_GPS_SENTENCE_GPGGA, 5):
    case COMBINE(_GPS_SENTENCE_GNGNS, 5):
     case COMBINE(UBX_MESSAGE(0), 6):      // UBX,00 Lat/Long Position Data
      if (_term[0] == 'W')
        _new_longitude = -_new_longitude;
      break;
    case COMBINE(_GPS_SENTENCE_GNGNS, 6):
      strncpy(_constellations, _term, 5);
      _constellations[5] = 0;
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 7): // Speed (GPRMC)
    case COMBINE(UBX_MESSAGE(0), 11):     // UBX,00 Lat/Long Position Data
      _new_speed = parse_decimal();
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 8): // Course (GPRMC)
    case COMBINE(UBX_MESSAGE(0), 12):     // UBX,00 Lat/Long Position Data
      _new_course = parse_decimal();
      break;
    case COMBINE(_GPS_SENTENCE_GPRMC, 9): // Date (GPRMC)
    case COMBINE(UBX_MESSAGE(4), 3):     // UBX,04 Time of Day and Clock Information
//Serial.printf("GPS: capturing date from sentence (%d) term (%d)\n", sentence_type, _term_number);
       _new_date = gpsatol(_term);
      break;
    case COMBINE(_GPS_SENTENCE_GPZDA, 2): // Day
      _new_day = gpsatol(_term);
      _new_date_fix = millis();
      break; 
    case COMBINE(_GPS_SENTENCE_GPZDA, 3): // Month
      _new_month = gpsatol(_term);
      _new_date_fix = millis();
      break; 
    case COMBINE(_GPS_SENTENCE_GPZDA, 4): // year
      _new_year = gpsatol(_term);
      _new_date_fix = millis();
      break; 
    case COMBINE(_GPS_SENTENCE_GPGGA, 6): // Fix data (GPGGA)
      _gps_data_good = _term[0] > '0';
      break;
    case COMBINE(_GPS_SENTENCE_GPGGA, 7): // Satellites used (GPGGA): GPS only
    case COMBINE(_GPS_SENTENCE_GNGNS, 7): // GNGNS counts-in all constellations
    case COMBINE(UBX_MESSAGE(0), 18):     // UBX,00 Lat/Long Position Data
      _new_numsats = (unsigned char)atoi(_term);
      break;
    case COMBINE(_GPS_SENTENCE_GPGGA, 8): // HDOP
    case COMBINE(UBX_MESSAGE(0), 15):     // UBX,00 Lat/Long Position Data
      _new_hdop = parse_decimal();
      break;
    case COMBINE(_GPS_SENTENCE_GPGGA, 9): // Altitude (GPGGA)
    case COMBINE(UBX_MESSAGE(0), 7):      // UBX,00 Lat/Long Position Data
      _new_altitude = parse_decimal();
      break;
    case COMBINE(UBX_MESSAGE(0), 8):      // UBX,00 Lat/Long Position Data
      // Checking Navigation Status - ok if G2, G3, D2, D3 not ok on NF, DR, RK, or TT
#ifdef DEBUG
      Serial.print("NavStat: "); Serial.print(_term[0]); Serial.println(_term[1]);
#endif
      _gps_data_good = (_term[0] == 'G' || (_term[0] == 'D' && _term[1] != 'R'));
      break;
    case COMBINE(_GPS_SENTENCE_GNGSA, 3): //satellites used in solution: 3 to 15
      //_sats_used[
      break;
    case COMBINE(_GPS_SENTENCE_GPGSV, 2):   //beginning of sequence
    case COMBINE(_GPS_SENTENCE_GLGSV, 2):   //beginning of sequence
    {
      uint8_t msgId = atoi(_term)-1;  //start from 0
      if(msgId == 0) {
        //http://geostar-navigation.com/file/geos3/geos_nmea_protocol_v3_0_eng.pdf
        if(_sentence_type == _GPS_SENTENCE_GPGSV) {
          //reset GPS & WAAS trackedSatellites
          for(uint8_t x=0;x<12;x++)
          {
            tracked_sat_rec[x] = 0;
          }
        } else {
          //reset GLONASS trackedSatellites: range starts with 23
          for(uint8_t x=12;x<24;x++)
          {
            tracked_sat_rec[x] = 0;
          }
        }
      }
      _sat_index = msgId*4;   //4 sattelites/line
      if(_sentence_type == _GPS_SENTENCE_GLGSV)
      {
        _sat_index = msgId*4 + 12;   //Glonass offset by 12
      }
      break;
    }
    case COMBINE(_GPS_SENTENCE_GPGSV, 4):   //satellite #
    case COMBINE(_GPS_SENTENCE_GPGSV, 8):
    case COMBINE(_GPS_SENTENCE_GPGSV, 12):
    case COMBINE(_GPS_SENTENCE_GPGSV, 16):
    case COMBINE(_GPS_SENTENCE_GLGSV, 4):
    case COMBINE(_GPS_SENTENCE_GLGSV, 8):
    case COMBINE(_GPS_SENTENCE_GLGSV, 12):
    case COMBINE(_GPS_SENTENCE_GLGSV, 16):
      _tracked_satellites_index = atoi(_term);
      break;
    case COMBINE(_GPS_SENTENCE_GPGSV, 7):   //strength
    case COMBINE(_GPS_SENTENCE_GPGSV, 11):
    case COMBINE(_GPS_SENTENCE_GPGSV, 15):
    case COMBINE(_GPS_SENTENCE_GPGSV, 19):
    case COMBINE(_GPS_SENTENCE_GLGSV, 7):   //strength
    case COMBINE(_GPS_SENTENCE_GLGSV, 11):
    case COMBINE(_GPS_SENTENCE_GLGSV, 15):
    case COMBINE(_GPS_SENTENCE_GLGSV, 19):
      uint8_t stren = (uint8_t)atoi(_term);
      if(stren == 0)  //remove the record, 0dB strength
      {
        tracked_sat_rec[_sat_index + (_term_number-7)/4] = 0;
      }
      else
      {
        tracked_sat_rec[_sat_index + (_term_number-7)/4] = _tracked_satellites_index<<8 | stren<<1;
      }
      break;
    } 
  }
 
  return false;
}

long TinyGPS::gpsatol(const char *str)
{
  long ret = 0;
  while (gpsisdigit(*str))
    ret = 10 * ret + *str++ - '0';
  return ret;
}

int TinyGPS::gpsstrcmp(const char *str1, const char *str2)
{
  while (*str1 && *str1 == *str2)
    ++str1, ++str2;
  return *str1;
}

/* static */
float TinyGPS::distance_between (float lat1, float long1, float lat2, float long2) 
{
  // returns distance in meters between two positions, both specified 
  // as signed decimal-degrees latitude and longitude. Uses great-circle 
  // distance computation for hypothetical sphere of radius 6372795 meters.
  // Because Earth is no exact sphere, rounding errors may be up to 0.5%.
  // Courtesy of Maarten Lamers
  float delta = radians(long1-long2);
  float sdlong = sin(delta);
  float cdlong = cos(delta);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  float slat1 = sin(lat1);
  float clat1 = cos(lat1);
  float slat2 = sin(lat2);
  float clat2 = cos(lat2);
  delta = (clat1 * slat2) - (slat1 * clat2 * cdlong); 
  delta = sq(delta); 
  delta += sq(clat2 * sdlong); 
  delta = sqrt(delta); 
  float denom = (slat1 * slat2) + (clat1 * clat2 * cdlong); 
  delta = atan2(delta, denom); 
  return delta * 6372795; 
}

float TinyGPS::course_to (float lat1, float long1, float lat2, float long2) 
{
  // returns course in degrees (North=0, West=270) from position 1 to position 2,
  // both specified as signed decimal-degrees latitude and longitude.
  // Because Earth is no exact sphere, calculated course may be off by a tiny fraction.
  // Courtesy of Maarten Lamers
  float dlon = radians(long2-long1);
  lat1 = radians(lat1);
  lat2 = radians(lat2);
  float a1 = sin(dlon) * cos(lat2);
  float a2 = sin(lat1) * cos(lat2) * cos(dlon);
  a2 = cos(lat1) * sin(lat2) - a2;
  a2 = atan2(a1, a2);
  if (a2 < 0.0)
  {
    a2 += TWO_PI;
  }
  return degrees(a2);
}

const char *TinyGPS::cardinal (float course)
{
  static const char* directions[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};

  int direction = (int)((course + 11.25f) / 22.5f);
  return directions[direction % 16];
}

// lat/long in MILLIONTHs of a degree and age of fix in milliseconds
// (note: versions 12 and earlier gave this value in 100,000ths of a degree.
void TinyGPS::get_position(long *latitude, long *longitude, unsigned long *fix_age)
{
  if (latitude) *latitude = _latitude;
  if (longitude) *longitude = _longitude;
  if (fix_age) *fix_age = _last_position_fix == GPS_INVALID_FIX_TIME ? 
   GPS_INVALID_AGE : millis() - _last_position_fix;
}

// date as ddmmyy, time as hhmmsscc, and age in milliseconds
void TinyGPS::get_datetime(unsigned long *date, unsigned long *time, unsigned long *age)
{
  if (date) *date = _date;
  if (time) *time = _time;
  if (age) *age = _last_time_fix == GPS_INVALID_FIX_TIME ? 
   GPS_INVALID_AGE : millis() - _last_time_fix;
}

void TinyGPS::get_datetime(int *year, byte *month, byte *day, 
    byte *hour, byte *minute, byte *second, byte *hundredths = 0, unsigned long *age)
{
  if (year) *year = _year;
  if (month) *month = _month;
  if (day) *day = _day;

  if (hour) *hour = _time / 1000000;
  if (minute) *minute = (_time / 10000) % 100;
  if (second) *second = (_time / 100) % 100;
  if (hundredths) *hundredths = _time % 100;
  if (age) *age = _last_date_fix == GPS_INVALID_FIX_TIME ? 
   GPS_INVALID_AGE : millis() - _last_date_fix;

}
void TinyGPS::f_get_position(float *latitude, float *longitude, unsigned long *fix_age)
{
  long lat, lon;
  get_position(&lat, &lon, fix_age);
  *latitude = lat == GPS_INVALID_ANGLE ? GPS_INVALID_F_ANGLE : (lat / 1000000.0);
  *longitude = lat == GPS_INVALID_ANGLE ? GPS_INVALID_F_ANGLE : (lon / 1000000.0);
}

void TinyGPS::crack_datetime(int *year, byte *month, byte *day, 
  byte *hour, byte *minute, byte *second, byte *hundredths, unsigned long *age)
{
  unsigned long date, time;
  get_datetime(&date, &time, age);
  if (year) 
  {
    *year = date % 100;
    *year += *year > 80 ? 1900 : 2000;
  }
  if (month) *month = (date / 100) % 100;
  if (day) *day = date / 10000;
  if (hour) *hour = time / 1000000;
  if (minute) *minute = (time / 10000) % 100;
  if (second) *second = (time / 100) % 100;
  if (hundredths) *hundredths = time % 100;
}

float TinyGPS::f_altitude()    
{
  return _altitude == GPS_INVALID_ALTITUDE ? GPS_INVALID_F_ALTITUDE : _altitude / 100.0;
}

float TinyGPS::f_course()
{
  return _course == GPS_INVALID_ANGLE ? GPS_INVALID_F_ANGLE : _course / 100.0;
}

float TinyGPS::f_speed_knots() 
{
  return _speed == GPS_INVALID_SPEED ? GPS_INVALID_F_SPEED : _speed / 100.0;
}

float TinyGPS::f_speed_mph()   
{ 
  float sk = f_speed_knots();
  return sk == GPS_INVALID_F_SPEED ? GPS_INVALID_F_SPEED : _GPS_MPH_PER_KNOT * sk; 
}

float TinyGPS::f_speed_mps()   
{ 
  float sk = f_speed_knots();
  return sk == GPS_INVALID_F_SPEED ? GPS_INVALID_F_SPEED : _GPS_MPS_PER_KNOT * sk; 
}

float TinyGPS::f_speed_kmph()  
{ 
  float sk = f_speed_knots();
  return sk == GPS_INVALID_F_SPEED ? GPS_INVALID_F_SPEED : _GPS_KMPH_PER_KNOT * sk; 
}

const float TinyGPS::GPS_INVALID_F_ANGLE = 1000.0;
const float TinyGPS::GPS_INVALID_F_ALTITUDE = 1000000.0;
const float TinyGPS::GPS_INVALID_F_SPEED = -1.0;
