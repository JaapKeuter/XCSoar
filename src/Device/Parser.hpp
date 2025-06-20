// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "time/Stamp.hpp"

struct NMEAInfo;
class NMEAInputLine;
struct GeoPoint;
struct BrokenDate;
struct BrokenTime;
struct RangeFilter;

class NMEAParser
{
  TimeStamp last_time;

public:
  bool real;

  bool use_geoid;

public:
  NMEAParser();

  /**
   * Resets the NMEAParser
   */
  void Reset();

  void SetReal(bool _real) {
    real = _real;
  }

  void DisableGeoid() {
    use_geoid = false;
  }

  /**
   * Parses a provided NMEA String into a NMEA_INFO struct
   * @param line NMEA string
   * @param info NMEA_INFO output struct
   * @return Parsing success
   */
  bool ParseLine(const char *line, NMEAInfo &info);

public:
  /**
   * Calculates the checksum of the provided NMEA string and
   * compares it to the provided checksum
   * @param String NMEA string
   * @return True if checksum correct
   */
  static bool NMEAChecksum(const char *string);

  /**
   * Checks whether time has advanced since last call and
   * updates the last_time reference if necessary
   * @return True if time has advanced since last call
   */
  static bool TimeHasAdvanced(TimeStamp this_time,
                              TimeStamp &last_time,
                              NMEAInfo &info);

  static bool ReadGeoPoint(NMEAInputLine &line, GeoPoint &value_r);

  static bool ReadDate(NMEAInputLine &line, BrokenDate &date);

  /**
   * Read and parse a time stamp in the form "HHMMSS.SSS".
   */
  static bool ReadTime(NMEAInputLine &line, BrokenTime &broken_time,
                       TimeStamp &time_of_day_s) noexcept;

private:
  /**
   * Checks whether time has advanced since last call and
   * updates the GPS_info if necessary
   * @param ThisTime Current time
   * @param info NMEA_INFO struct to update
   * @return True if time has advanced since last call
   */
  bool TimeHasAdvanced(TimeStamp this_time, NMEAInfo &info) noexcept;

  /**
   * Parses a GLL sentence
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  bool GLL(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a GGA sentence
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  bool GGA(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a GSA sentence
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  bool GSA(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a RMC sentence
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  bool RMC(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a HDM sentence
   */
  bool HDM(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a PGRMZ sentence (Garmin proprietary).
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  bool RMZ(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a PTAS1 sentence (Tasman Instruments proprietary).
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  static bool PTAS1(NMEAInputLine &line, NMEAInfo &info);

  /**
   * Parses a MWV sentence (NMEA Wind information).
   *
   * @param line A NMEAInputLine instance that can be used for parsing
   * @param info NMEA_INFO struct to parse into
   * @return Parsing success
   */
  static bool MWV(NMEAInputLine &line, NMEAInfo &info);
};
