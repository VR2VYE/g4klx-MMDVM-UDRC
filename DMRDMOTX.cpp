/*
 *   Copyright (C) 2009-2018 by Jonathan Naylor G4KLX
 *   Copyright (C) 2016 by Colin Durbridge G4EML
 *   Copyright (C) 2017 by Andy Uribe CA6JAU
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

#include "Globals.h"
#include "DMRSlotType.h"

// Generated using rcosdesign(0.2, 8, 5, 'sqrt') in MATLAB
const float RRC_0_2_FILTER[] = {0.0000000F,  0.0000000F,  0.0000000F,  0.0000000F, 0.0259399F,  0.0066833F, -0.0219727F, -0.0472412F,
			       -0.0547791F, -0.0357666F,  0.0072327F,  0.0588074F, 0.0952148F,  0.0937805F,  0.0441589F, -0.0436707F,
			       -0.1386719F, -0.1965942F, -0.1750183F, -0.0498352F, 0.1724548F,  0.4523315F,  0.7266235F,  0.9267273F,
			        0.9999695F,  0.9267273F,  0.7266235F,  0.4523315F, 0.1724548F, -0.0498352F, -0.1750183F, -0.1965942F,
			       -0.1386719F, -0.0436707F,  0.0441589F,  0.0937805F, 0.0952148F,  0.0588074F,  0.0072327F, -0.0357666F,
			       -0.0547791F, -0.0472412F, -0.0219727F,  0.0066833F, 0.0259399F}; // numTaps = 45, L = 5
const uint16_t RRC_0_2_FILTER_PHASE_LEN = 9U; // phaseLength = numTaps/L

const float DMR_LEVELA =  0.545;
const float DMR_LEVELB =  0.182;
const float DMR_LEVELC = -0.182;
const float DMR_LEVELD = -0.545;

const uint8_t BIT_MASK_TABLE[] = {0x80U, 0x40U, 0x20U, 0x10U, 0x08U, 0x04U, 0x02U, 0x01U};

#define WRITE_BIT1(p,i,b) p[(i)>>3] = (b) ? (p[(i)>>3] | BIT_MASK_TABLE[(i)&7]) : (p[(i)>>3] & ~BIT_MASK_TABLE[(i)&7])
#define READ_BIT1(p,i)    (p[(i)>>3] & BIT_MASK_TABLE[(i)&7])

const uint8_t DMR_SYNC = 0x5FU;

CDMRDMOTX::CDMRDMOTX() :
m_fifo(),
m_modFilter(DMR_RADIO_SYMBOL_LENGTH, RRC_0_2_FILTER_PHASE_LEN, RRC_0_2_FILTER, 4U),
m_poBuffer(),
m_poLen(0U),
m_poPtr(0U),
m_txDelay(240U),       // 200ms
m_cal(false)
{
}

void CDMRDMOTX::process()
{
  if (m_poLen == 0U) {
    if (m_cal) {
      createCal();
    } else if (m_fifo.getData() > 0U) {
      if (!m_tx) {
        for (uint16_t i = 0U; i < m_txDelay; i++)
          m_poBuffer[i] = DMR_SYNC;

        m_poLen = m_txDelay;
      } else {
        for (unsigned int i = 0U; i < 72U; i++)
          m_poBuffer[i] = DMR_SYNC;

        for (unsigned int i = 0U; i < DMR_FRAME_LENGTH_BYTES; i++)
          m_poBuffer[i + 39U] = m_fifo.get();

        m_poLen = 72U;
      }
    }

    m_poPtr = 0U;
  }

  if (m_poLen > 0U) {
    uint16_t space = io.getSpace();
    
    while (space > (4U * DMR_RADIO_SYMBOL_LENGTH)) {
      uint8_t c = m_poBuffer[m_poPtr++];

      writeByte(c);

      space -= 4U * DMR_RADIO_SYMBOL_LENGTH;
      
      if (m_poPtr >= m_poLen) {
        m_poPtr = 0U;
        m_poLen = 0U;
        return;
      }
    }
  }
}

uint8_t CDMRDMOTX::writeData(const uint8_t* data, uint8_t length)
{
  if (length != (DMR_FRAME_LENGTH_BYTES + 1U))
    return 4U;

  uint16_t space = m_fifo.getSpace();
  if (space < DMR_FRAME_LENGTH_BYTES)
    return 5U;

  for (uint8_t i = 0U; i < DMR_FRAME_LENGTH_BYTES; i++)
    m_fifo.put(data[i + 1U]);

  return 0U;
}

void CDMRDMOTX::writeByte(uint8_t c)
{
  float inBuffer[4U];
  float outBuffer[DMR_RADIO_SYMBOL_LENGTH * 4U];

  const uint8_t MASK = 0xC0U;

  for (uint8_t i = 0U; i < 4U; i++, c <<= 2) {
    switch (c & MASK) {
      case 0xC0U:
        inBuffer[i] = DMR_LEVELA;
        break;
      case 0x80U:
        inBuffer[i] = DMR_LEVELB;
        break;
      case 0x00U:
        inBuffer[i] = DMR_LEVELC;
        break;
      default:
        inBuffer[i] = DMR_LEVELD;
        break;
    }
  }

  m_modFilter.process(inBuffer, outBuffer, 4U);

  io.write(STATE_DMR, outBuffer, DMR_RADIO_SYMBOL_LENGTH * 4U);
}

uint8_t CDMRDMOTX::getSpace() const
{
  return m_fifo.getSpace() / (DMR_FRAME_LENGTH_BYTES + 2U);
}

void CDMRDMOTX::setCal(bool start)
{
  m_cal = start;
}

void CDMRDMOTX::setTXDelay(uint8_t delay)
{
  m_txDelay = 600U + uint16_t(delay) * 12U;        // 500ms + tx delay

  if (m_txDelay > 1200U)
    m_txDelay = 1200U;
}

void CDMRDMOTX::createCal()
{
  // 1.2 kHz sine wave generation
  if (m_modemState == STATE_DMRCAL) {
    for (unsigned int i = 0U; i < DMR_FRAME_LENGTH_BYTES; i++) {
      m_poBuffer[i]   = 0x5FU;              // +3, +3, -3, -3 pattern for deviation cal.
    }

    m_poLen = DMR_FRAME_LENGTH_BYTES;
  }

  // 80 Hz square wave generation
  if (m_modemState == STATE_LFCAL) {
    for (unsigned int i = 0U; i < 7U; i++) {
      m_poBuffer[i]   = 0x55U;              // +3, +3, ... pattern
    }

    m_poBuffer[7U]   = 0x5FU;               // +3, +3, -3, -3 pattern

    for (unsigned int i = 8U; i < 15U; i++) {
      m_poBuffer[i]   = 0xFFU;              // -3, -3, ... pattern
    }

    m_poLen = 15U;
  }
}

