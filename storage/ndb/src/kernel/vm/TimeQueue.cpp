/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA */

#include "TimeQueue.hpp"
#include <ErrorHandlingMacros.hpp>
#include <GlobalData.hpp>
#include <FastScheduler.hpp>
#include <VMSignal.hpp>

static const int MAX_TIME_QUEUE_VALUE = 32000;

TimeQueue::TimeQueue()
{
  clear();
}

TimeQueue::~TimeQueue()
{
}

void 
TimeQueue::clear()
{
  globalData.theNextTimerJob = 65535;
  globalData.theCurrentTimer = 0;
  globalData.theShortTQIndex = 0;
  globalData.theLongTQIndex = 0;
  for (int i = 0; i < MAX_NO_OF_TQ; i++)
    theFreeIndex[i] = i+1;
  theFreeIndex[MAX_NO_OF_TQ - 1] = NULL_TQ_ENTRY;
  globalData.theFirstFreeTQIndex = 0;
}

void 
TimeQueue::insert(Signal* signal, BlockNumber bnr, 
		  GlobalSignalNumber gsn, Uint32 delayTime)
{
  if (delayTime == 0)
    delayTime = 1;
  register Uint32 regCurrentTime = globalData.theCurrentTimer;
  register Uint32 i;
  register Uint32 regSave;
  register TimerEntry newEntry;
  
  newEntry.time_struct.delay_time = regCurrentTime + delayTime;
  newEntry.time_struct.job_index = getIndex();
  regSave = newEntry.copy_struct;
  
  globalScheduler.insertTimeQueue(signal, bnr, gsn, 
				  newEntry.time_struct.job_index);
  
  if (newEntry.time_struct.delay_time < globalData.theNextTimerJob)
    globalData.theNextTimerJob = newEntry.time_struct.delay_time;
  if (delayTime < 100){
    register Uint32 regShortIndex = globalData.theShortTQIndex;
    if (regShortIndex == 0){
      theShortQueue[0].copy_struct = newEntry.copy_struct;
    } else if (regShortIndex >= MAX_NO_OF_SHORT_TQ - 1) {
      ERROR_SET(ecError, NDBD_EXIT_TIME_QUEUE_SHORT, 
		"Too many in Short Time Queue", "TimeQueue.C" );
    } else {
      for (i = 0; i < regShortIndex; i++) {
        if (theShortQueue[i].time_struct.delay_time > 
	    newEntry.time_struct.delay_time)  {
	  
          regSave = theShortQueue[i].copy_struct;
          theShortQueue[i].copy_struct = newEntry.copy_struct;
          break;
	}
      }
      if (i == regShortIndex) {
        theShortQueue[regShortIndex].copy_struct = regSave;
      } else {
        for (i++; i < regShortIndex; i++) {
	  register Uint32 regTmp = theShortQueue[i].copy_struct;
	  theShortQueue[i].copy_struct = regSave;
	  regSave = regTmp;
        }
        theShortQueue[regShortIndex].copy_struct = regSave;
      }
    }
    globalData.theShortTQIndex = regShortIndex + 1;
  } else if (delayTime <= (unsigned)MAX_TIME_QUEUE_VALUE) {
    register Uint32 regLongIndex = globalData.theLongTQIndex;
    if (regLongIndex == 0) {
      theLongQueue[0].copy_struct = newEntry.copy_struct;
    } else if (regLongIndex >= MAX_NO_OF_LONG_TQ - 1) {
      ERROR_SET(ecError, NDBD_EXIT_TIME_QUEUE_LONG, 
		"Too many in Long Time Queue", "TimeQueue.C" );
    } else {
      for (i = 0; i < regLongIndex; i++) {
        if (theLongQueue[i].time_struct.delay_time > 
	    newEntry.time_struct.delay_time) {
	  
          regSave = theLongQueue[i].copy_struct;
          theLongQueue[i].copy_struct = newEntry.copy_struct;
          break;
        }
      }
      if (i == regLongIndex) {
        theLongQueue[regLongIndex].copy_struct = regSave;
      } else {
        for (i++; i < regLongIndex; i++) {
          register Uint32 regTmp = theLongQueue[i].copy_struct;
          theLongQueue[i].copy_struct = regSave;
          regSave = regTmp;
        }
        theLongQueue[regLongIndex].copy_struct = regSave;
      }
    }
    globalData.theLongTQIndex = regLongIndex + 1;
  } else {
    ERROR_SET(ecError, NDBD_EXIT_TIME_QUEUE_DELAY, 
	      "Too long delay for Time Queue", "TimeQueue.C" );
  }
}

// executes the expired signals;
void
TimeQueue::scanTable()
{
  register Uint32 i, j;
  
  globalData.theCurrentTimer++;
  if (globalData.theCurrentTimer == 32000)
    recount_timers();
  if (globalData.theNextTimerJob > globalData.theCurrentTimer)
    return;
  globalData.theNextTimerJob = 65535; // If no more timer jobs
  for (i = 0; i < globalData.theShortTQIndex; i++) {
    if (theShortQueue[i].time_struct.delay_time > globalData.theCurrentTimer){
      break;
    } else {
      releaseIndex((Uint32)theShortQueue[i].time_struct.job_index);
      globalScheduler.scheduleTimeQueue(theShortQueue[i].time_struct.job_index);
    }
  }
  if (i > 0) {
    for (j = i; j < globalData.theShortTQIndex; j++)
      theShortQueue[j - i].copy_struct = theShortQueue[j].copy_struct;
    globalData.theShortTQIndex -= i;
  }
  if (globalData.theShortTQIndex != 0) // If not empty
    globalData.theNextTimerJob = theShortQueue[0].time_struct.delay_time;
  for (i = 0; i < globalData.theLongTQIndex; i++) {
    if (theLongQueue[i].time_struct.delay_time > globalData.theCurrentTimer) {
      break;
    } else {
      releaseIndex((Uint32)theLongQueue[i].time_struct.job_index);
      globalScheduler.scheduleTimeQueue(theLongQueue[i].time_struct.job_index);
    }
  }
  if (i > 0) {
    for (j = i; j < globalData.theLongTQIndex; j++)
      theLongQueue[j - i].copy_struct = theLongQueue[j].copy_struct;
    globalData.theLongTQIndex -= i;
  }  
  if (globalData.theLongTQIndex != 0) // If not empty
    if (globalData.theNextTimerJob > theLongQueue[0].time_struct.delay_time)
      globalData.theNextTimerJob = theLongQueue[0].time_struct.delay_time;
}

void
TimeQueue::recount_timers()
{
  Uint32 i;

  globalData.theCurrentTimer = 0;
  globalData.theNextTimerJob -= 32000;

  for (i = 0; i < globalData.theShortTQIndex; i++)
    theShortQueue[i].time_struct.delay_time -= 32000;
  for (i = 0; i < globalData.theLongTQIndex; i++)
    theLongQueue[i].time_struct.delay_time -= 32000;
}

Uint32
TimeQueue::getIndex()
{
  Uint32 retValue = globalData.theFirstFreeTQIndex;
  globalData.theFirstFreeTQIndex = (Uint32)theFreeIndex[retValue];
  if (retValue >= MAX_NO_OF_TQ)
    ERROR_SET(fatal, NDBD_EXIT_TIME_QUEUE_INDEX, 
	      "Index out of range", "TimeQueue.C" );
  return retValue;
}

void
TimeQueue::releaseIndex(Uint32 aIndex)
{
  theFreeIndex[aIndex] = globalData.theFirstFreeTQIndex;
  globalData.theFirstFreeTQIndex = aIndex;
}


