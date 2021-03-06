/*
 *   Copyright (C) 2015,2016,2017,2018 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 by Mathis Schmieder DB9MAT
 *   Copyright (C) 2016 by Colin Durbridge G4EML
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "SoundCardReaderWriter.h"
#include "Globals.h"
#include "Thread.h"

#include <sys/types.h>
#include <pwd.h>

// Global variables
MMDVM_STATE m_modemState = STATE_IDLE;

bool m_dstarEnable = true;
bool m_dmrEnable   = true;
bool m_ysfEnable   = true;
bool m_p25Enable   = true;
bool m_nxdnEnable  = true;

bool m_duplex = true;

bool m_tx  = false;
bool m_dcd = false;

CDStarRX   dstarRX;
CDStarTX   dstarTX;

CDMRDMORX  dmrDMORX;
CDMRDMOTX  dmrDMOTX;

CYSFRX     ysfRX;
CYSFTX     ysfTX;

CP25RX     p25RX;
CP25TX     p25TX;

CNXDNRX    nxdnRX;
CNXDNTX    nxdnTX;

CCalDStarRX calDStarRX;
CCalDStarTX calDStarTX;
CCalDMR     calDMR;
CCalP25     calP25;
CCalNXDN    calNXDN;

CCWIdTX cwIdTX;

CSerialPort serial;
CIO io;

void loop()
{
  serial.process();
  
  io.process();

  // The following is for transmitting
  if (m_dstarEnable && m_modemState == STATE_DSTAR)
    dstarTX.process();

  if (m_dmrEnable && m_modemState == STATE_DMR)
    dmrDMOTX.process();

  if (m_ysfEnable && m_modemState == STATE_YSF)
    ysfTX.process();

  if (m_p25Enable && m_modemState == STATE_P25)
    p25TX.process();

  if (m_nxdnEnable && m_modemState == STATE_NXDN)
    nxdnTX.process();

  if (m_modemState == STATE_DSTARCAL)
    calDStarTX.process();

  if (m_modemState == STATE_DMRCAL || m_modemState == STATE_LFCAL || m_modemState == STATE_DMRDMO1K)
    calDMR.process();

  if (m_modemState == STATE_P25CAL1K)
    calP25.process();

  if (m_modemState == STATE_NXDNCAL1K)
    calNXDN.process();

  if (m_modemState == STATE_IDLE)
    cwIdTX.process();
}

int main(int argc, char** argv)
{
  if (argc == 1) {
    ::fprintf(stderr, "Usage: MMDVM [-daemon] <port>\n");
    return 1;
  }

  std::string port;
  bool daemon = false;

  if (argc == 2) {
    port = std::string(argv[1U]);
  } else {
    port = std::string(argv[2U]);
    daemon = true;
  }

  bool ret = serial.open(port);
  if (!ret)
    return 1;

  CSoundCardReaderWriter sound("READ", "WRITE", 24000U, RX_BLOCK_SIZE);
  sound.setCallback(&io);

  ret = sound.open();
  if (!ret)
    return 1;

  if (daemon) {
    // Create new process
    pid_t pid = ::fork();
    if (pid == -1) {
      ::fprintf(stderr, "Couldn't fork() , exiting\n");
      return 1;
    } else if (pid != 0) {
      exit(EXIT_SUCCESS);
    }

    // Create new session and process group
    if (::setsid() == -1) {
      ::fprintf(stderr, "Couldn't setsid(), exiting\n");
      return 1;
    }

    // Set the working directory to the root directory
    if (::chdir("/") == -1) {
      ::fprintf(stderr, "Couldn't cd /, exiting\n");
      return 1;
    }

    ::close(STDIN_FILENO);
    ::close(STDOUT_FILENO);
    ::close(STDERR_FILENO);

    // If we are currently root...
    if (getuid() == 0) {
      struct passwd* user = ::getpwnam("mmdvm");
      if (user == NULL) {
        ::fprintf(stderr, "Could not get the mmdvm user, exiting\n");
        return 1;
      }

      uid_t mmdvm_uid = user->pw_uid;
      gid_t mmdvm_gid = user->pw_gid;

      // Set user and group ID's to mmdvm:mmdvm
      if (setgid(mmdvm_gid) != 0) {
        ::fprintf(stderr, "Could not set mmdvm GID, exiting\n");
        return 1;
      }

      if (setuid(mmdvm_uid) != 0) {
        ::fprintf(stderr, "Could not set mmdvm UID, exiting\n");
        return 1;
      }

      // Double check it worked (AKA Paranoia) 
      if (setuid(0) != -1) {
        ::fprintf(stderr, "It's possible to regain root - something is wrong!, exiting\n");
        return 1;
      }
    }
  }

  for (;;) {
    loop();
    CThread::sleep(5U);
  }

  return 0;
}

