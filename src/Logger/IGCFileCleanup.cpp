/*
  Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2012 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "IGCFileCleanup.hpp"

#include "Util/StaticString.hpp"
#include "LocalPath.hpp"
#include "OS/FileUtil.hpp"
#include "OS/PathName.hpp"
#include "UtilsSystem.hpp"
#include "Compatibility/dirent.h"
#include "LogFile.hpp"

#include <tchar.h>
#include <time.h>
#include <stdio.h>
#include <windef.h>

// JMW note: we want to clear up enough space to save the persistent
// data (85 kb approx) and a new log file
#define LOGGER_MINFREESTORAGE 750

static int
IGCCharToNum(TCHAR c)
{
  if ((c >= _T('1')) && (c <= _T('9')))
    return c - _T('1') + 1;

  if ((c >= _T('A')) && (c <= _T('Z')))
    return c - _T('A') + 10;

  return 0; // Error!
}

static time_t
LogFileDate(unsigned current_year, const TCHAR *filename)
{
  // scan for long filename
  unsigned short year, month, day, num;
  int matches = _stscanf(filename, _T("%hu-%hu-%hu-%*7s-%hu.igc"),
                         &year, &month, &day, &num);

  if (matches == 4) {
    struct tm tm;
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = num;
    tm.tm_mday = day;
    tm.tm_mon = month - 1;
    tm.tm_year = year - 1900;
    tm.tm_isdst = -1;
    return mktime(&tm);
  }

  TCHAR cyear, cmonth, cday, cflight;
  // scan for short filename
  matches = _stscanf(filename, _T("%c%c%c%*4s%c.igc"),
		                 &cyear, &cmonth, &cday,&cflight);

  if (matches == 4) {
    int iyear = (int)current_year;
    int syear = iyear % 10;
    int yearzero = iyear - syear;
    int yearthis = IGCCharToNum(cyear) + yearzero;
    if (yearthis > iyear)
      yearthis -= 10;

    struct tm tm;
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = IGCCharToNum(cflight);
    tm.tm_mday = IGCCharToNum(cday);
    tm.tm_mon = IGCCharToNum(cmonth) - 1;
    tm.tm_year = yearthis - 1900;
    tm.tm_isdst = -1;
    return mktime(&tm);
    /*
      YMDCXXXF.igc
      Y: Year, 0 to 9 cycling every 10 years
      M: Month, 1 to 9 then A for 10, B=11, C=12
      D: Day, 1 to 9 then A for 10, B=....
      C: Manuf. code = X
      XXX: Logger ID Alphanum
      F: Flight of day, 1 to 9 then A through Z
    */
  }

  return 0;
}

static bool
LogFileIsOlder(unsigned current_year,
               const TCHAR *oldestname, const TCHAR *thisname)
{
  return LogFileDate(current_year, oldestname) >
         LogFileDate(current_year, thisname);
}

/**
 * Delete eldest IGC file in the given path
 * @param gps_info Current NMEA_INFO
 * @param pathname Path where to search for the IGC files
 * @return True if a file was found and deleted, False otherwise
 */
static bool
DeleteOldestIGCFile(unsigned current_year, const TCHAR *pathname)
{
  StaticString<MAX_PATH> oldest_name, full_name;

  _TDIR *dir = _topendir(pathname);
  if (dir == NULL)
    return false;

  _tdirent *ent;
  while ((ent = _treaddir(dir)) != NULL) {
    if (!MatchesExtension(ent->d_name, _T(".igc")))
      continue;

    full_name = pathname;
    full_name += ent->d_name;

    if (File::Exists(full_name) &&
        LogFileIsOlder(current_year, oldest_name, ent->d_name))
      // we have a new oldest name
      oldest_name = ent->d_name;
  }

  _tclosedir(dir);

  // now, delete the file...
  full_name.Format(_T("%s%s"), pathname, oldest_name.c_str());
  File::Delete(full_name);

  // did delete one
  return true;
}

bool
IGCFileCleanup(unsigned current_year)
{
  bool found = true;
  unsigned long kbfree = 0;
  const TCHAR *pathname = GetPrimaryDataPath();
  TCHAR subpathname[MAX_PATH];
  int numtries = 0;

  LocalPath(subpathname, _T("logs"));

  while (found && ((kbfree = FindFreeSpace(pathname)) < LOGGER_MINFREESTORAGE)
	       && (numtries++ < 100)) {
    /* JMW asking for deleting old files is disabled now --- system
       automatically deletes old files as required
    */

    // search for IGC files, and delete the oldest one
    found = DeleteOldestIGCFile(current_year, pathname);
    if (!found)
      found = DeleteOldestIGCFile(current_year, subpathname);
  }

  if (kbfree >= LOGGER_MINFREESTORAGE) {
    LogStartUp(_T("LoggerFreeSpace returned: true"));
    return true;
  } else {
    LogStartUp(_T("LoggerFreeSpace returned: false"));
    return false;
  }
}
